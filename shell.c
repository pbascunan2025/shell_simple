#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "shell.h"

/**
 * Muestra en la pantalla el prompt de la Shell.
 * Obtiene la ruta del directorio de trabajo actual y la imprime.
 */
static void obtener_prompt(char *buffer, size_t size) {
    char cwd[1024];

    //Obtiene la ruta del directorio actual.
    //Si falla (retorna null), se coloca un "?" como marcador de posicion.
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "?");
    }

    snprintf(buffer, size, "Shell:\001\033[38;5;117m\002%s\001\033[0m\002$ ", cwd);
}

/**
 * Punto de entrada princinpal del interprete de comandos.
 */
int main(void) {
    char prompt_str[1200];
    char *input_line;
    char *tokens[MAX_TOKENS];
    struct sigaction sigchld_action;

    memset(&sigchld_action, 0, sizeof(sigchld_action));
    sigchld_action.sa_handler = manejar_sigchld;
    sigemptyset(&sigchld_action.sa_mask);
    sigchld_action.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sigchld_action, NULL);


    // Ignora las señales SIGINT y SIGQUIT en el proceso padre (la Shell).
    struct sigaction sa_ignore;
    memset(&sa_ignore, 0, sizeof(sa_ignore));
    sa_ignore.sa_handler = SIG_IGN;
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_flags = 0;

    sigaction(SIGINT, &sa_ignore, NULL);
    sigaction(SIGQUIT, &sa_ignore, NULL);

    //Bucle principal de ejecucion de la Shell.
    while (1) {

        recolectar_jobs();

        // Obtiene la linea de entrada del usuario con readline y la almacena en input_line.
        obtener_prompt(prompt_str, sizeof(prompt_str));
        input_line = readline(prompt_str);

        // Si el usuario presiona Ctrl+D (EOF), readline retorna NULL y se sale del bucle.
        if (input_line == NULL) {
            printf("\n");
            break;
        }

        if (*input_line != '\0') {
            add_history(input_line);
        }

        char linea[MAX_LINEAS];
        strncpy(linea, input_line, sizeof(linea) - 1);
        linea[sizeof(linea) - 1] = '\0';
        free(input_line);

        char *comandos_pipe[MAX_TOKENS];
        int num_comandos = parse_pipes(linea, comandos_pipe, MAX_TOKENS);

        if (num_comandos == 0) {
            continue;
        }

        
        //for(int i = 0; i < num_comandos; i++) {
        //    printf("Comando %d encontrado: [%s]\n", i, comandos_pipe[i]);
        //}

        //3. Parsea la linea ingresada separandola en tokens.
        if (num_comandos == 1) {
            int ntokens = parse_line(comandos_pipe[0], tokens, MAX_TOKENS);
            //Si la linea esta vacia, se ignora.
            if (ntokens == 0) {
                continue;
            }

            int background = 0;
            if (strcmp(tokens[ntokens - 1], "&") == 0) {
                background = 1;
                tokens[--ntokens] = NULL;
                if (ntokens == 0) {
                    continue;
                }
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
            } else if (strcmp(tokens[0], "jobs") == 0) {
                listar_jobs();
                continue;
            } else if (strcmp(tokens[0], "pmon") == 0) {
                ejecutar_pmon_basico();
                continue;
            }

            ejecutar_comando(tokens, background, comandos_pipe[0]);
            
        } else {
            //Si hay más de 1 comando (pipes), llamamos a la función
            ejecutar_pipes(comandos_pipe, num_comandos);
        }
    }

    return 0;
}