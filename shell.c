#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "shell.h"

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
 * Punto de entrada princinpal del interprete de comandos.
 */
int main(void) {
    char linea[MAX_LINEAS];
    char *tokens[MAX_TOKENS];
    struct sigaction sigchld_action;

    memset(&sigchld_action, 0, sizeof(sigchld_action));
    sigchld_action.sa_handler = manejar_sigchld;
    sigemptyset(&sigchld_action.sa_mask);
    sigchld_action.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sigchld_action, NULL);

    //Bucle principal de ejecucion de la Shell.
    while (1) {

        recolectar_jobs();

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