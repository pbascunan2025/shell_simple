#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "shell.h"
#include <signal.h>
#include <sys/wait.h>


//===================//
//    Comando cd     //
//===================//


// Comando interno "cd".
void comando_cd(char *tokens[]) {
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
//    Comandos fg/bg  //
//====================//

void comando_fg(char *tokens[]) {
    pid_t pid_objetivo = -1;
    
    //Si el usuario entrega un pid
    if (tokens[1] != NULL) {
        pid_objetivo = atoi(tokens[1]);
    } else {
        // Si no, buscamos el último proceso registrado en la tabla jobs
        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            if (jobs[i].activo) {
                pid_objetivo = jobs[i].pid;
                break;
            }
        }
    }

    if (pid_objetivo == -1) {
        printf("fg: no hay trabajos detenidos o en background\n");
        return;
    }

    //devolvemos el control de la terminal al proceso
    tcsetpgrp(STDIN_FILENO, pid_objetivo);
    
    //enviamos la señal para que se despierte
    kill(pid_objetivo, SIGCONT);

    //esperamos a que termine o vuelva a ser pausado
    int status;
    waitpid(pid_objetivo, &status, WUNTRACED);

    if (WIFSTOPPED(status)) {
        printf("\n[Detenido] %d\n", pid_objetivo);
    } else {
        //si terminó normalmente, lo borramos de la tabla jobs
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].pid == pid_objetivo) {
                jobs[i].activo = 0;
                break;
            }
        }
    }

    //la shell recupera el control de la terminal
    tcsetpgrp(STDIN_FILENO, getpgrp());
}

void comando_bg(char *tokens[]) {
    pid_t pid_objetivo = -1;
    
    if (tokens[1] != NULL) {
        pid_objetivo = atoi(tokens[1]);
    } else {
        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            if (jobs[i].activo) {
                pid_objetivo = jobs[i].pid;
                break;
            }
        }
    }

    if (pid_objetivo == -1) {
        printf("bg: no hay trabajos detenidos o en background\n");
        return;
    }


    kill(pid_objetivo, SIGCONT);
    printf("[%d] continua ejecutándose en background\n", pid_objetivo);
}