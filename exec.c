#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "shell.h"


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
 * El proceso padre espera a que el hijo termine su ejecucion antes de continuar.
 * 
 * @param tokens Arreglo de cadenas terminado en NULL. 
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

    setpgid(pid, pid);
    pid_t pids[1] = {pid};

    if (background) {
        registrar_job(pids, 1, pid, comando, 0);
        return;
    }

    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, pid);
    }

    int status;

    while (waitpid(pid, &status, WUNTRACED) < 0 && errno == EINTR) {}

    if (WIFSTOPPED(status)) {
        registrar_job(pids, 1, pid, comando, 1);
        printf("\n[Detenido] %d %s\n", pid, comando);
    }

    if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, getpgrp());
}

void ejecutar_pipes(char *comandos_pipe[], int num_comandos, int background, const char *comando) {
    int in_fd = STDIN_FILENO;
    pid_t pids[MAX_PIDS_POR_JOB];
    pid_t pgid = 0;
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

            pid_t grupo = (pgid == 0) ? getpid() : pgid;
            setpgid(0, grupo);

            if (in_fd != STDIN_FILENO) {

                if (dup2(in_fd, STDIN_FILENO) < 0) { 
                    perror("dup2"); _exit(2); 
                }
            }

            if (i < num_comandos - 1) {

                if (dup2(pipefd[1], STDOUT_FILENO) < 0) { 
                    perror("dup2"); _exit(2); 
                }
            }

            if (in_fd != STDIN_FILENO) {
                close(in_fd);
            }
            
            if (pipefd[0] != -1) {
                close(pipefd[0]);
            }
            
            if (pipefd[1] != -1) {
                close(pipefd[1]);
            }

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
