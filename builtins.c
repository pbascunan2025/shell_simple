#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include "shell.h"



//===================//
//    Comando cd     //
//===================//


// Comando interno "cd".
void comando_cd(char *tokens[]) {
    const char *directorio = tokens[1];

    //Si no se especifica un directorio, se cambia al directorio home.
    if (directorio == NULL) {
        directorio = getenv("HOME");
        if (directorio == NULL) {
            fprintf(stderr, "cd: HOME no esta definido\n");
            return;
        }
    }

    // Imprime error en color rojo si no se encuentra el directorio.
    if (chdir(directorio) != 0) {
        perror("cd");
    }
}

static int esperar_job(Job *job) {
    int restantes = job->restantes;

    while (restantes > 0) {
        int status;
        pid_t pid = waitpid(-job->pgid, &status, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
            perror("waitpid");
            break;
        }

        int idx = -1;
        for (int i = 0; i < job->nprocesos; i++) {
            if (job->procesos[i].pid == pid) {
                idx = i;
                break;
            }
        }

        if (WIFSTOPPED(status)) {
            job->detenido = 1;
            return 1;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (idx >= 0 && !job->procesos[idx].terminado) {
                job->procesos[idx].terminado = 1;
                restantes--;
                job->restantes--;
            }
        }
    }

    if (job->restantes == 0) {
        marcar_job_completado(job);
    }
    return 0;
}

//====================//
//    Comandos fg/bg  //
//====================//

void comando_fg(char *tokens[]) {
    Job *job = buscar_job(tokens[1]);
    
    if (job == NULL) {
        fprintf(stderr, "fg: trabajo no encontrado\n");
        return;
    }

    job->detenido = 0;
    if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, job->pgid);

    if (kill(-job->pgid, SIGCONT) < 0 && errno != ESRCH) {
        perror("fg");
    }

    int detenido = esperar_job(job);

    if (detenido) {
        printf("\n[Detenido] %d %s\n", job->pgid, job->comando);
    }

    if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, getpgrp());
}

void comando_bg(char *tokens[]) {
    Job *job = buscar_job(tokens[1]);
    if (job == NULL) {
        fprintf(stderr, "bg: trabajo no encontrado\n");
        return;
    }

    if (kill(-job->pgid, SIGCONT) < 0) {
        if (errno == ESRCH) {
            recolectar_jobs();
            return;
        }
        perror("bg");
        return;
    }

    job->detenido = 0;
    printf("[%d] %d continua en background\n", (int)(job - jobs) + 1, job->pgid);
}