#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_LINEAS 1024 //Tamaño maximo del buffer para la linea de entrada
#define MAX_TOKENS 12 //Tamaño maximo de tokens perimitidos por comando

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

        //3. Parsea la linea ingresada separandola en tokens.
        int ntokens = parse_line(linea, tokens, MAX_TOKENS);

        //Si la linea esta vacia, se ignora.
        if (ntokens == 0) {
            continue;        
        }

        //Comando interno "exit" para salir de la Shell.
        if (strcmp(tokens[0], "exit") == 0) {
            break;
        }


        //Comando interno "cd".
        if (strcmp(tokens[0], "cd") == 0) {
            comando_cd(tokens);
            continue;
        }

        //5. Ejecuta los comandos externos.
        ejecutar_comando(tokens);
    }

    return 0;
}