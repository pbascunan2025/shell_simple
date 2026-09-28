#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "shell.h"


/**
 * Muestra en la pantalla el prompt de la Shell.
 * Obtiene la ruta del directorio de trabajo actual y la imprime.
 */
static void obtener_prompt(char *buffer, size_t size) {
    char cwd[MAX_LINEAS];

    //Obtiene la ruta del directorio actual.
    //Si falla (retorna null), se coloca un "?" como marcador de posicion.
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "?");
    }

    snprintf(buffer, size, "Shell:\001\033[38;5;117m\002%s\001\033[0m\002$ ", cwd);
}

/* Elimina & solo si es el último carácter no escapado y está fuera de comillas. */
static int quitar_background(char *linea) {
    char comilla = 0;
    char *ultimo = NULL;
    for (char *p = linea; *p; p++) {
        if (comilla) {
            if (*p == comilla) comilla = 0;
        } else if (*p == '\'' || *p == '"') {
            comilla = *p;
        } else if (*p == '&') {
            ultimo = p;
        }
    }

    if (comilla || ultimo == NULL) return 0;

    char *fin = ultimo + 1;
    while (*fin && isspace((unsigned char)*fin)) fin++;
    if (*fin != '\0') return 0;

    *ultimo = '\0';
    while (ultimo > linea && isspace((unsigned char)ultimo[-1])) --ultimo;
    *ultimo = '\0';
    return 1;
}

static int es_asignacion(const char *token) {
    if (!token || (!isalpha((unsigned char)token[0]) && token[0] != '_')) return 0;
    const char *p = token + 1;
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
    return *p == '=';
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
    sigaction(SIGINT, &sa_ignore, NULL);
    sigaction(SIGQUIT, &sa_ignore, NULL);
    sigaction(SIGTSTP, &sa_ignore, NULL); //ignorar ctrlz
    sigaction(SIGTTIN, &sa_ignore, NULL); //ignorar lectura en background
    sigaction(SIGTTOU, &sa_ignore, NULL);

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
        if (*input_line != '\0') add_history(input_line);

        char linea[MAX_LINEAS];
        snprintf(linea, sizeof(linea), "%s", input_line);
        free(input_line);

        if (expandir_variables(linea, sizeof(linea)) < 0) {
            fprintf(stderr, "linea demasiado larga despues de expandir variables\n");
            continue;
        }

        int background = quitar_background(linea);
        if (normalizar_operadores(linea, sizeof(linea)) < 0) {
            fprintf(stderr, "linea demasiado larga\n");
            continue;
        }
        if (linea[0] == '\0') continue;

        char linea_original[MAX_LINEAS];
        snprintf(linea_original, sizeof(linea_original), "%s", linea);

        char *comandos_pipe[MAX_COMANDOS];
        int num_comandos = parse_pipes(linea, comandos_pipe, MAX_COMANDOS);
        if (num_comandos < 0) {
            fprintf(stderr, "error de sintaxis: comillas o pipeline invalido\n");
            continue;
        }
        if (num_comandos == 0) continue;

        //for(int i = 0; i < num_comandos; i++) {
        //    printf("Comando %d encontrado: [%s]\n", i, comandos_pipe[i]);
        //}

        //3. Parsea la linea ingresada separandola en tokens.
        if (num_comandos == 1) {

            int ntokens = parse_line(comandos_pipe[0], tokens, MAX_TOKENS);
            if (ntokens < 0) {
                fprintf(stderr, "error de sintaxis o demasiados tokens\n");
                continue;
            }

            if (ntokens == 0) continue;

            if (es_asignacion(tokens[0])) {
                char *igual = strchr(tokens[0], '=');
                *igual = '\0';
                if (setenv(tokens[0], igual + 1, 1) != 0) perror("setenv");
                continue;
            }

            if (strcmp(tokens[0], "exit") == 0) {
                int codigo = 0;
                if (tokens[1]) codigo = atoi(tokens[1]);
                return codigo;
            }

            if (strcmp(tokens[0], "cd") == 0) {
                comando_cd(tokens);
                continue;
            }

            if (strcmp(tokens[0], "jobs") == 0) {
                listar_jobs();
                continue;
            }
            if (strcmp(tokens[0], "pmon") == 0) {
                int intervalo = tokens[1] ? atoi(tokens[1]) : 2;
                pmon_run(intervalo);
                continue;
            }
            
            if (strcmp(tokens[0], "fg") == 0) {
                comando_fg(tokens);
                continue;
            }

            if (strcmp(tokens[0], "bg") == 0) {
                comando_bg(tokens);
                continue;
            }

            ejecutar_comando(tokens, background, linea_original);
        } else {

            //Si hay más de 1 comando (pipes), llamamos a la función
            ejecutar_pipes(comandos_pipe, num_comandos, background, linea_original);
        }
    }

    return 0;
}