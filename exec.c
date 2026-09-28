#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "shell.h"


/**
 * Restaura la disposicion por defecto de las señales de terminal en el hijo
 * (la shell las ignora y esa disposicion se hereda tras fork). Asi Ctrl+C,
 * Ctrl+\ y Ctrl+Z afectan al comando en foreground y no a la shell.
 * Debe llamarse despues del fork() y antes de execvp().
 */
static void restaurar_senales_hijo(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);
    sigaction(SIGTTOU, &sa, NULL);
}

/**
 * Aplica las redirecciones de E/S (<, > y >>) indicadas en los tokens.
 * Para cada operador hace open() del archivo, dup2() sobre stdin/stdout
 * y close() del descriptor original. Se llama en el proceso hijo.
 * Si hay varias redirecciones del mismo tipo, prevalece la ultima.
 *
 * @param tokens Arreglo de tokens terminado en NULL.
 *
 * @return int 0 si todas las redirecciones se aplicaron; -1 si falta el
 *             archivo o falla open()/dup2() (el error se informa por stderr).
 */
static int aplicar_redirecciones(char *tokens[]) {
    for (int i = 0; tokens[i] != NULL; i++) {
        int destino = -1;
        int flags = 0;

        if (strcmp(tokens[i], "<") == 0) {
            destino = STDIN_FILENO;
            flags = O_RDONLY;
        } else if (strcmp(tokens[i], ">") == 0) {
            destino = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        } else if (strcmp(tokens[i], ">>") == 0) {
            destino = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_APPEND;
        } else {
            continue;
        }

        if (tokens[i + 1] == NULL || tokens[i + 1][0] == '\0') {
            fprintf(stderr, "redireccion: falta archivo\n");
            return -1;
        }

        int fd = open(tokens[i + 1], flags, 0664);
        if (fd < 0) {
            perror(tokens[i + 1]);
            return -1;
        }
        if (dup2(fd, destino) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
        i++;
    }
    return 0;
}

/**
 * Construye el argv para execvp a partir de los tokens, omitiendo los
 * operadores de redireccion (<, >, >>) junto con el archivo que los sigue.
 *
 * @param tokens Arreglo de tokens terminado en NULL.
 * @param argv Arreglo de salida, terminado en NULL.
 * @param max_argv Capacidad de argv (incluye el NULL final).
 *
 * @return int Cantidad de argumentos; -1 si un operador no tiene archivo
 *             o si hay demasiados argumentos.
 */
static int preparar_argv(char *tokens[], char *argv[], int max_argv) {
    int n = 0;
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
            if (tokens[i + 1] == NULL) return -1;
            i++;
            continue;
        }
        if (n >= max_argv - 1) return -1;
        argv[n++] = tokens[i];
    }
    argv[n] = NULL;
    return n;
}

/**
 * Crea proceso hijo para ejecutar un comando externo mediante execvp.
 * El hijo se coloca en su propio grupo de procesos, restaura las señales y
 * aplica las redirecciones antes del exec.
 * En foreground, el padre cede el terminal al hijo (tcsetpgrp) y espera a que
 * termine o se detenga (Ctrl+Z; en ese caso se registra como job detenido).
 * En background, el padre registra el job y retorna de inmediato.
 * 
 * @param tokens Arreglo de cadenas terminado en NULL. 
 * @param background 1 si el comando se ejecuta en background, 0 si en foreground.
 * @param comando Texto de la linea de comando, usado al registrar el job.
 */
void ejecutar_comando(char *tokens[], int background, const char *comando) {

    //Clona el proceso actual creando un proceso hijo.
    pid_t pid = fork();

    //Evalua si es que se dio un error en la creacion del proceso hijo.
    if (pid < 0) {
        perror("fork");
        return;
    }

    //CODIGO DEL PROCESO HIJO
    if (pid == 0) {

        // Nuevo grupo de procesos, para poder controlar el job como una unidad.
        setpgid(0, 0);
        restaurar_senales_hijo();

        if (aplicar_redirecciones(tokens) < 0) _exit(2);

        char *argv[MAX_TOKENS];
        if (preparar_argv(tokens, argv, MAX_TOKENS) < 0 || argv[0] == NULL) {
            fprintf(stderr, "comando invalido\n");
            _exit(2);
        }
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }

    // El padre tambien llama a setpgid para evitar una condicion de carrera
    // con el hijo (el grupo debe existir antes de usar tcsetpgrp).
    setpgid(pid, pid);
    pid_t pids[1] = {pid};

    if (background) {
        registrar_job(pids, 1, pid, comando, 0);
        return;
    }

    // Foreground: el hijo pasa a ser el grupo con control del terminal.
    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, pid);
    }

    int status;

    // WUNTRACED permite detectar que el hijo fue detenido (Ctrl+Z).
    while (waitpid(pid, &status, WUNTRACED) < 0 && errno == EINTR) {}

    if (WIFSTOPPED(status)) {
        registrar_job(pids, 1, pid, comando, 1);
        printf("\n[Detenido] %d %s\n", pid, comando);
    }

    // Devuelve el control del terminal a la shell.
    if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, getpgrp());
}

/**
 * Ejecuta una tuberia de N comandos (cmd1 | cmd2 | ... | cmdN).
 * Cada comando corre en su propio proceso hijo, todos dentro de un mismo grupo
 * de procesos (el del primer hijo). La salida de cada comando se conecta a la
 * entrada del siguiente con pipe() + dup2(), y todos los descriptores de pipe
 * sobrantes se cierran en hijos y padre. Cada comando admite redirecciones.
 * En foreground espera a todos los procesos (o a que se detengan); en
 * background registra el job y retorna.
 *
 * @param comandos_pipe Arreglo con el texto de cada comando de la tuberia.
 * @param num_comandos Cantidad de comandos (maximo MAX_PIDS_POR_JOB).
 * @param background 1 si la tuberia completa va en background, 0 si en foreground.
 * @param comando Texto de la linea completa, usado al registrar el job.
 */
void ejecutar_pipes(char *comandos_pipe[], int num_comandos, int background, const char *comando) {
    int in_fd = STDIN_FILENO; // extremo de lectura del pipe anterior (entrada del proximo comando)
    pid_t pids[MAX_PIDS_POR_JOB];
    pid_t pgid = 0; // grupo de procesos de la tuberia (0 hasta crear el primer hijo)
    int creados = 0;

    if (num_comandos <= 0 || num_comandos > MAX_PIDS_POR_JOB) {
        fprintf(stderr, "pipeline demasiado largo\n");
        return;
    }

    for (int i = 0; i < num_comandos; i++) {
        char *tokens[MAX_TOKENS];
        char *argv[MAX_TOKENS];
        int ntokens = parse_line(comandos_pipe[i], tokens, MAX_TOKENS);
        
        if (ntokens <= 0) {
            fprintf(stderr, "comando invalido en pipeline\n");
            goto error;
        }

        if (preparar_argv(tokens, argv, MAX_TOKENS) < 0 || argv[0] == NULL) {
            fprintf(stderr, "comando invalido en pipeline\n");
            goto error;
        }

        int pipefd[2] = {-1, -1};

        // Se crea un pipe por cada comando excepto el ultimo.
        if (i < num_comandos - 1 && pipe(pipefd) < 0) {
            perror("pipe");
            goto error;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");

            if (pipefd[0] != -1) { 
                close(pipefd[0]); 
                close(pipefd[1]); 
            }

            goto error;
        }

        if (pid == 0) {
            restaurar_senales_hijo();

            // El primer hijo crea el grupo; los demas se unen a el.
            pid_t grupo = (pgid == 0) ? getpid() : pgid;
            setpgid(0, grupo);

            // Entrada: extremo de lectura del pipe anterior.
            if (in_fd != STDIN_FILENO) {

                if (dup2(in_fd, STDIN_FILENO) < 0) { 
                    perror("dup2"); _exit(2); 
                }
            }

            // Salida: extremo de escritura del pipe actual (si no es el ultimo).
            if (i < num_comandos - 1) {

                if (dup2(pipefd[1], STDOUT_FILENO) < 0) { 
                    perror("dup2"); _exit(2); 
                }
            }

            // Cierra los descriptores originales de pipe, que ya no se necesitan.
            if (in_fd != STDIN_FILENO) {
                close(in_fd);
            }
            
            if (pipefd[0] != -1) {
                close(pipefd[0]);
            }
            
            if (pipefd[1] != -1) {
                close(pipefd[1]);
            }

            // Las redirecciones explicitas (<, >, >>) tienen prioridad sobre los pipes.
            if (aplicar_redirecciones(tokens) < 0) {
                _exit(2);
            }
            
            execvp(argv[0], argv);
            perror(argv[0]);
            _exit(127);
        }

        pids[creados++] = pid;
        
        if (pgid == 0) {
            pgid = pid;
        }
        
        setpgid(pid, pgid);

        // El padre cierra los extremos que ya no usa para que los lectores
        // reciban EOF: la lectura anterior y la escritura del pipe actual.
        if (in_fd != STDIN_FILENO) {
            close(in_fd);
        }
        
        if (i < num_comandos - 1) {
            close(pipefd[1]);
            in_fd = pipefd[0];
        }
    }

    if (in_fd != STDIN_FILENO) {
        close(in_fd);
    }

    if (background) {
        registrar_job(pids, creados, pgid, comando, 0);
        return;
    }

    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, pgid);
    }

    // Espera a todos los procesos del grupo (waitpid(-pgid)) o a que se detengan.
    int restantes = creados;
    int detenido = 0;

    while (restantes > 0) {
        int status;
        pid_t pid = waitpid(-pgid, &status, WUNTRACED);
        
        if (pid < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
            perror("waitpid");
            break;
        }

        if (WIFSTOPPED(status)) {
            detenido = 1;
            break;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            restantes--;
        }
    }

    if (detenido) {
        registrar_job(pids, creados, pgid, comando, 1);
        printf("\n[Detenido] %d %s\n", pgid, comando);
    }
    
    if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, getpgrp());
    return;

// Manejo de errores: cierra el pipe pendiente, termina (SIGTERM) los hijos ya
// creados y los recoge para no dejar procesos zombie.
error:
    if (in_fd != STDIN_FILENO) {
        close(in_fd);
    }
    
    for (int i = 0; i < creados; i++) {
        kill(pids[i], SIGTERM);
    }
    
    for (int i = 0; i < creados; i++) {
        waitpid(pids[i], NULL, 0);
    }
}