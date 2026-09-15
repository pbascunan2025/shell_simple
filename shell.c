#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINEAS 1024 //Tamaño maximo del buffer para la linea de entrada
#define MAX_TOKENS 12 //Tamaño maximo de tokens perimitidos por comando
#define MAX_JOBS 64 //Cantidad maxima de procesos 

typedef struct {
    pid_t pid;
    int activo; // 1 activo, 0 si ya termino
} Job;

Job jobs[MAX_JOBS]; //Arreglo de jobs

/**
 * Muestra en la pantalla el prompt de la Shell.
 * Obtiene la ruta del directorio de trabajo actual y la imprime.
 */
static void imprimir_prompt(void) {
    char cwd[1024];

    //Obtiene la ruta del directorio actual.
    //Si falla (retorna null), se coloca un "?" como marcador de posicion.
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "?");
    }

    //Imprime el prompt con la ruta actual en color celeste.
    printf("Shell:\033[38;5;117m%s\033[0m$ ", cwd);

    //Fuerza el vacciado del buffer de salida para asegurar que el prompt
    //se dibuje inmediatamente en la terminal sin esperar un "\n"
    fflush(stdout);
}

/**
 * Divide la cadena de entrada en tokens separados por espacios o tabulaciones.
 * 
 * @param line Cadena original leida desde el teclado.
 * @param tokens Arreglo dende se almacenan los punteros a cada argumento.
 * @param max_tokens Cantidad maxima de elementos que puede almacenar un arreglo de tokens.
 *
 * @return int Cantidad de tokens extraidos 
 */
static int parse_line(char *line, char *tokens[], int max_tokens) {
    
    int ntokens = 0; //Cantidad total de tokens extraidos.
    char *saveptr;  //Puntero interno necesario para la reentrancia de strtok_r

    //Elimina el salto de linea que fgets añade al final.
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    //Extrae el primer token delimitado por espacios o tabulaciones.
    char *tok = strtok_r(line, " \t", &saveptr);
    
    //Iteracion para estraer el resto de los tokens.
    //Se reserva el ultimo elemento para el puntero NUll obligatorio.
    while (tok != NULL && ntokens < max_tokens - 1) {
        tokens[ntokens++] = tok;
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    //Se añade el NULL al final del arreglo (execvp lo requiere asi).
    tokens[ntokens] = NULL;

    return ntokens;
}

/**
 * Comando Pipes
 * este divide la cadena original en comandos separados por el carácter "|"
 */
static int parse_pipes(char *line, char *comandos[], int max_comandos) {
    int n_comandos = 0;
    char *saveptr; //Puntero interno que strtok_r usa para saber dónde quedó

    //Extraemos el primer trozo de comandos separados por "|"
    char *tok = strtok_r(line, "|", &saveptr);

    //Iteramos mientras siga encontrando el delimitador "|"
    while (tok != NULL && n_comandos < max_comandos - 1) {
        comandos[n_comandos++] = tok;
        
        //Aca strtok_r buscara desde donde se quedó la última vez
        tok = strtok_r(NULL, "|", &saveptr);
    }

    //Cerramos el arreglo con un NULL por seguridad
    comandos[n_comandos] = NULL;

    return n_comandos;
}

static void ejecutar_pipes(char *comandos_pipe[], int num_comandos) {
    int in_fd = 0; //Lee la entrada estandar
    int fd[2];     //Arreglo para el pipe
    pid_t pids[MAX_TOKENS];

    for (int i = 0; i < num_comandos; i++) {
        //Separamos el comando por espacios para obtener sus argumentos
        char *tokens[MAX_TOKENS];
        int ntokens = parse_line(comandos_pipe[i], tokens, MAX_TOKENS);
        
        if (ntokens == 0) continue;

        //Si no es el último comando, se crea un pipe
        if (i < num_comandos - 1) {
            if (pipe(fd) < 0) {
                perror("pipe");
                return;
            }
        }

        pids[i] = fork();

        if (pids[i] < 0) {
            perror("fork");
            return;
        }

        //Proceso de hijo
        if (pids[i] == 0) { 
            
            //Redirigir la ENTRADA
            if (in_fd != 0) {
                dup2(in_fd, STDIN_FILENO); //STDIN ahora es el extremo de lectura del pipe anterior
                close(in_fd);
            }

            //Redirigir la SALIDA (si no es el último comando)
            if (i < num_comandos - 1) {
                dup2(fd[1], STDOUT_FILENO); //STDOUT ahora apunta al extremo de escritura del pipe actual
                close(fd[0]);               //El hijo no lee de su propio pipe, lo cerramos
                close(fd[1]);
            }

            // Ejecutamos el comando
            execvp(tokens[0], tokens);
            perror(tokens[0]);
            _exit(127);
        } else {          
            if (in_fd != 0) {
                close(in_fd); //El padre ya le pasó este in_fd al hijo
            }

            if (i < num_comandos - 1) {
                close(fd[1]);  //Cerramos la entrada
                in_fd = fd[0]; 
            }
        }
    }

    //Esperar a todos los hijos
    for (int i = 0; i < num_comandos; i++) {
        int status;
        waitpid(pids[i], &status, 0); 
    }
}

/**
 * Crea proceso hijo para ejecutar un comando externo mediante execvp.
 * El proceso padre espera a que el hijo termine su ejecucion antes de continuar.
 * 
 * @param tokens Arreglo de cadenas terminado en NULL. 
 */
static void ejecutar_comando(char *tokens[]) {

    //Clona el proceso actual creando un proceso hijo.
    pid_t pid = fork();

    //Evalua si es que se dio un error en la creacion del proceso hijo.
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    //CODIGO DEL PROCESO HIJO
    if (pid == 0) {
        
        int i = 0;
        int idxCorte = -1;

        while (tokens[i]!=NULL){
                if (strcmp(tokens[i], ">") == 0 ||
                strcmp(tokens[i], ">>") == 0 ||
                strcmp(tokens[i], "<") == 0) {

                //se guarda el primer indice donde se encuentra una redireccion
                if (idxCorte ==-1){
                    idxCorte = i;
                }

                if(strcmp(tokens[i], ">") == 0) {
                    //redireccion de salida
                    int fd = open(tokens[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0664);
                    if (fd < 0) {
                        perror("open >");
                        _exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                else if (strcmp(tokens[i], ">>") == 0) {
                    //redireccion de salida (append)
                    int fd = open(tokens[i+1], O_WRONLY | O_CREAT | O_APPEND, 0664);
                    if (fd < 0) {
                        perror("open >>");
                        _exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                else if (strcmp(tokens[i], "<") == 0) {
                    //redireccion de entrada
                    int fd = open(tokens[i+1], O_RDONLY);
                    if (fd < 0){
                        perror("open <");
                        _exit(1);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
            }
            i++;
        }

        //si se encuentra una redireccion, se corta el arreglo
        if(idxCorte !=-1) {
            tokens[idxCorte] = NULL;
        }
        //reemplaza la imagen del proceso hijo por la del binario indicado en tokens[0].
        execvp(tokens[0], tokens);

        //Si execvp retorna, significa que ocurrio un error. 
        perror(tokens[0]);

        //Finaliza el hijo directamente con _exit sin limpiar el buffer.
        _exit(127);
    }

    //CODIGO DEL PROCESO PADRE
    int status;

    //Bloquea la ejecucion del padre hasta que el hijo termine.
    waitpid(pid, &status, 0);
}



    //===================//
    //    Comando cd     //
    //===================//


    // Comando interno "cd".
    static void comando_cd(char *tokens[]) {
        const char *directorio;

        //Si no se especifica un directorio, se cambia al directorio home.
        if (tokens[1] == NULL) {
            directorio = getenv("HOME");
            if (directorio == NULL) {
                fprintf(stderr, "cd: No se pudo acceder a directorio HOME\n");
                return;
            }
        } else {
            directorio = tokens[1];
        }

        // Imprime error en color rojo si no se encuentra el directorio.
        if (chdir(directorio) != 0) {
            printf("\033[91mcd: No se encontró el directorio\033[0m '%s'\n", directorio); 
        }

    }


    //====================//
    //    Comando pmon    //
    //====================//


    void ejecutar_pmon_basico() {
    printf("%-7s | %-20s\n", "PID", "COMANDO");
    printf("--------+----------------------\n");

    for (int i = 0; i < MAX_JOBS; i++) {
        // Solo revisamos los jobs que la shell registró como activos
        if (jobs[i].activo == 1) {
            char path[256];
            // Construimos la ruta
            snprintf(path, sizeof(path), "/proc/%d/stat", jobs[i].pid);
            
            FILE *f_stat = fopen(path, "r");
            if (f_stat != NULL) {
                int pid_leido;
                char comando[256];
                
                // Usamos una expresión regular básica en fscanf para leer hasta el paréntesis de cierre
                if (fscanf(f_stat, "%d (%255[^)])", &pid_leido, comando) == 2) {
                    // Imprimimos los datos
                    printf("%-7d | %-20s\n", pid_leido, comando);
                }
                
                fclose(f_stat);
            } else {
                // Si fopen falla 
                // Lo marcamos como inactivo para no volver a leerlo
                jobs[i].activo = 0;
            }
        }
    }
}








/**
 * Punto de entrada princinpal del interprete de comandos.
 */
int main(void) {
    char linea[MAX_LINEAS];
    char *tokens[MAX_TOKENS];

    //Bucle principal de ejecucion de la Shell.
    while (1) {

        //1. Muestra el prompt.
        imprimir_prompt();

        //2. Lee la entrada del usuario desde el input estandar.
        //Detecta EOF (Ctrl+D) cuando fgets retorna NULL.
        if (fgets(linea, sizeof(linea), stdin) == NULL) {
            printf("\n");
            break; //Sale del bucle y finaliza la Shell.
        }

        //Arreglo para guardar los comandos separados por "|"
        char *comandos_pipe[MAX_TOKENS]; 
        int num_comandos = parse_pipes(linea, comandos_pipe, MAX_TOKENS);

        if (num_comandos == 0) {
            continue;
        }
        for(int i = 0; i < num_comandos; i++) {
            printf("Comando %d encontrado: [%s]\n", i, comandos_pipe[i]);
        }

        //3. Parsea la linea ingresada separandola en tokens.
        if (num_comandos == 1) {
            int ntokens = parse_line(comandos_pipe[0], tokens, MAX_TOKENS);
            //Si la linea esta vacia, se ignora.
            if (ntokens == 0) {
                continue;        
            }

            if (strcmp(tokens[0], "exit") == 0) {
              int exitCode = 0;
              if (tokens[1]!=NULL) {
                  exitCode = atoi(tokens[1]);
              }
              exit(exitCode);
            } else if (strcmp(tokens[0], "cd") == 0) {
                comando_cd(tokens);
                continue;
            } else if (strcmp(tokens[0], "pmon") == 0) {
                ejecutar_pmon_basico();
                continue;
            }

            ejecutar_comando(tokens);
            
        } else {
            //Si hay más de 1 comando (pipes), llamamos a la función
            ejecutar_pipes(comandos_pipe, num_comandos);
        }
    }

    return 0;
}