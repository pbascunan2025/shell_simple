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
// Bandera que el manejador de SIGCHLD levanta cuando llega la señal.
volatile sig_atomic_t hay_hijos_terminados = 0;


// Manejador de la señal SIGCHLD para indicar que hay hijos terminados.
// Solo levanta la bandera; la recoleccion (waitpid) se hace en recolectar_jobs().
void manejar_sigchld(int signal_number) {
    (void)signal_number;
    hay_hijos_terminados = 1;
}

/**
 * Busca el job activo que contiene a un proceso.
 *
 * @param pid PID del proceso a buscar.
 *
 * @return Job* Puntero al job, o NULL si el PID no pertenece a ningun job activo.
 */
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

/**
 * Obtiene la posicion de un proceso dentro de la lista de procesos de un job.
 *
 * @return int Indice en job->procesos, o -1 si el PID no pertenece al job.
 */
static int indice_proceso(Job *job, pid_t pid) {

    for (int i = 0; i < job->nprocesos; i++) {

        if (job->procesos[i].pid == pid) {
            return i;
        }
    }
    
    return -1;
}

/**
 * Registra un nuevo job en la primera posicion libre del arreglo de jobs e
 * imprime su numero y PID de grupo (ej. "[1] 4821").
 *
 * @param pids Arreglo con los PIDs de los procesos del job (uno por comando).
 * @param npids Cantidad de procesos (entre 1 y MAX_PIDS_POR_JOB).
 * @param pgid ID del grupo de procesos del job.
 * @param comando Texto de la linea de comando, para mostrarlo en jobs/pmon.
 * @param detenido 1 si el job se registra ya detenido (Ctrl+Z), 0 si esta corriendo.
 *
 * @return int Indice del job en el arreglo; -1 si los parametros son invalidos
 *             o se alcanzo el limite de MAX_JOBS.
 */
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

/**
 * Marca un job como completado, liberando su posicion en el arreglo de jobs
 * para que pueda ser reutilizada por registrar_job.
 */
void marcar_job_completado(Job *job) {
    if (!job) {
        return;
    }
    
    job->activo = 0;
    job->detenido = 0;
    job->restantes = 0;
}

// Recolecta los jobs terminados y los elimina del arreglo de jobs.
// Usa waitpid(-1, WNOHANG | WUNTRACED | WCONTINUED) en un ciclo para atender
// todos los cambios de estado pendientes sin bloquear la shell: actualiza el
// estado detenido/continuado de los jobs y, cuando terminan todos los procesos
// de un job, imprime "[n]+ Done <comando>" y libera su lugar.
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
        // Otros errores de waitpid no se tratan: se ignoran.
    } 
}

// Obtiene el estado (texto) de un job: Detenido, Ejecutando o Terminado.
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
// Formato: [n] <estado> <pgid> <comando>. Antes de listar, actualiza el
// estado de los jobs con recolectar_jobs().
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

/**
 * Busca un job a partir de un especificador, usado por fg y bg.
 *
 * @param especificador NULL o vacio: el job activo mas reciente (mayor numero);
 *                      "%n": el job numero n; "n": el job cuyo PGID es n.
 *
 * @return Job* Puntero al job, o NULL si el especificador es invalido o no existe.
 */
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