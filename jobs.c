#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include "shell.h"


//==================//
//       Jobs       //
//==================//



Job jobs[MAX_JOBS]; //Arreglo de jobs
volatile sig_atomic_t hay_hijos_terminados = 0;


// Manejador de la señal SIGCHLD para indicar que hay hijos terminados.
void manejar_sigchld(int signal_number) {
    (void)signal_number;
    hay_hijos_terminados = 1;
}

static Job *buscar_por_pid(pid_t pid) {

    for (int i = 0; i < MAX_JOBS; i++) {

        if (!jobs[i].activo) {
            continue;
        }

        for (int j = 0; j < jobs[i].nprocesos; j++) {

            if (jobs[i].procesos[j].pid == pid) {
                return &jobs[i];
            }
        }
    }

    return NULL;
}

static int indice_proceso(Job *job, pid_t pid) {

    for (int i = 0; i < job->nprocesos; i++) {

        if (job->procesos[i].pid == pid) {
            return i;
        }
    }
    
    return -1;
}

int registrar_job(const pid_t pids[], int npids, pid_t pgid, const char *comando, int detenido) {
    if (npids <= 0 || npids > MAX_PIDS_POR_JOB) {
        return -1;
    }

    for (int i = 0; i < MAX_JOBS; i++) {

        if (!jobs[i].activo) {
            memset(&jobs[i], 0, sizeof(jobs[i]));
            jobs[i].pgid = pgid;
            jobs[i].nprocesos = npids;
            jobs[i].restantes = npids;
            jobs[i].activo = 1;
            jobs[i].detenido = detenido;
            snprintf(jobs[i].comando, sizeof(jobs[i].comando), "%s", comando);
            
            for (int j = 0; j < npids; j++) {
                jobs[i].procesos[j].pid = pids[j];
                jobs[i].procesos[j].terminado = 0;
            }
            
            printf("[%d] %d\n", i + 1, pgid);
            fflush(stdout);
            return i;
        }
    }

    fprintf(stderr, "jobs: se alcanzo el limite de %d jobs\n", MAX_JOBS);
    return -1;
}

void marcar_job_completado(Job *job) {
    if (!job) {
        return;
    }
    
    job->activo = 0;
    job->detenido = 0;
    job->restantes = 0;
}

// Recolecta los jobs terminados y los elimina del arreglo de jobs.
void recolectar_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        Job *job = buscar_por_pid(pid);
        if (!job) {
            continue;
        }

        int idx = indice_proceso(job, pid);

        if (idx < 0) { 
            continue;
        }

        if (WIFSTOPPED(status)) {
            job->detenido = 1;
        } else if (WIFCONTINUED(status)) {
            job->detenido = 0;
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {

            if (!job->procesos[idx].terminado) {
                job->procesos[idx].terminado = 1;

                if (job->restantes > 0) {
                    job->restantes--;
                }
            }

            if (job->restantes == 0) {
                printf("[%d]+ Done %s\n", (int)(job - jobs) + 1, job->comando);
                marcar_job_completado(job);
            }
        }
    }

    if (pid < 0 && errno != ECHILD && errno != EINTR) {
    } 
}

// Obtiene el estado de un job a partir de su PID. 
static const char *obtener_estado_job(const Job *job) {
    if (job->detenido) {
        return "Detenido";
    }
    
    if (job->restantes > 0) {
        return "Ejecutando";
    }
    
    return "Terminado";
}

// Escribe en pantalla la lista de jobs activos.
void listar_jobs(void) {
    recolectar_jobs();

    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].activo) {
            continue;
        }    
        printf("[%d] %-12s %d %s\n", i + 1,
               obtener_estado_job(&jobs[i]), jobs[i].pgid, jobs[i].comando);
    }
}

Job *buscar_job(char *especificador) {
    recolectar_jobs();

    if (especificador == NULL || *especificador == '\0') {

        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            if (jobs[i].activo) return &jobs[i];
        }

        return NULL;
    }

    char *fin = NULL;
    long valor = strtol(especificador[0] == '%' ? especificador + 1 : especificador, &fin, 10);
    
    if (fin == NULL || *fin != '\0' || valor <= 0) {
        return NULL;
    }

    if (especificador[0] == '%') {

        if (valor > MAX_JOBS || !jobs[valor - 1].activo) {
            return NULL;
        }
        
        return &jobs[valor - 1];
    }

    for (int i = 0; i < MAX_JOBS; i++) {

        if (jobs[i].activo && jobs[i].pgid == (pid_t)valor) {
        return &jobs[i];
        }
    }

    return NULL;
}