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
// Debe ejecutarse en la propia shell (no en un hijo) para que el cambio de
// directorio afecte a la shell.
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

    // Si chdir falla (ej. el directorio no existe), informa el error con perror.
    if (chdir(directorio) != 0) {
        perror("cd");
    }
}

/**
 * Espera en foreground a que terminen los procesos restantes de un job
 * (usado por fg). Usa waitpid sobre el grupo de procesos del job con
 * WUNTRACED para detectar si el job vuelve a detenerse.
 *
 * @param job Job a esperar.
 *
 * @return int 1 si el job se detuvo (queda marcado como detenido);
 *             0 si terminaron todos sus procesos (el job se marca como completado).
 */
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

/**
 * Comando interno "fg [%n | pgid]": trae un job a foreground.
 * Le cede el terminal (tcsetpgrp), lo reanuda con SIGCONT y espera a que
 * termine o se detenga de nuevo. Sin argumento usa el job mas reciente.
 * Al terminar devuelve el terminal a la shell.
 */
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

/**
 * Comando interno "bg [%n | pgid]": reanuda en background un job detenido.
 * Envia SIGCONT a todo el grupo de procesos sin ceder el terminal ni esperar.
 * Sin argumento usa el job mas reciente.
 */
void comando_bg(char *tokens[]) {
    Job *job = buscar_job(tokens[1]);
    if (job == NULL) {
        fprintf(stderr, "bg: trabajo no encontrado\n");
        return;
    }

    if (kill(-job->pgid, SIGCONT) < 0) {
        // ESRCH: el grupo ya no existe (el job termino); se limpia la lista.
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