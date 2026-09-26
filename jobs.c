#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
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


// Registra un nuevo job en el arreglo de jobs.
void registrar_job(pid_t pid, const char *comando) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].activo) {
            jobs[i].pid = pid;
            jobs[i].activo = 1;
            snprintf(jobs[i].comando, sizeof(jobs[i].comando), "%s", comando);
            
            jobs[i].pmon_ultimo_tiempo_usuario = 0;
            jobs[i].pmon_ultimo_tiempo_sistema = 0;
            jobs[i].pmon_iniciado = 0;
            printf("[%d] %d\n", i + 1, pid);
            return;
        }
    }

    fprintf(stderr, "jobs: se alcanzo el limite de %d procesos\n", MAX_JOBS);
}


// Recolecta los jobs terminados y los elimina del arreglo de jobs.
void recolectar_jobs(void) {
    int status;
    pid_t pid;

    if (!hay_hijos_terminados) {
        return;
    }

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].activo && jobs[i].pid == pid) {
                printf("[%d]+ Done %s\n", i + 1, jobs[i].comando);
                jobs[i].activo = 0;
                break;
            }
        }
    }

    hay_hijos_terminados = 0;
}

// Obtiene el estado de un job a partir de su PID.
static const char *obtener_estado_job(pid_t pid) {
    char ruta[64];
    snprintf(ruta, sizeof(ruta), "/proc/%d/stat", pid);

    // Abre el archivo /proc/[pid]/stat para leer el estado del proceso.
    FILE *archivo = fopen(ruta, "r");
    if (archivo == NULL) {
        return "Desconocido";
    }

    int pid_leido;
    char comando[256];
    char estado;
    int campos_leidos = fscanf(archivo, "%d (%255[^)]) %c",
                               &pid_leido, comando, &estado);
    fclose(archivo);

    if (campos_leidos != 3) {
        return "Desconocido";
    }

    // Retorna el estado
    switch (estado) {
        case 'R': return "Ejecutando";
        case 'S': return "Durmiendo";
        case 'D': return "Esperando (I/O)";
        case 'Z': return "Zombie";
        case 'T': return "Detenido";
        default: return "Desconocido";
    }
}

// Escribe en pantalla la lista de jobs activos.
void listar_jobs(void) {
    recolectar_jobs();

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].activo) {
            const char *estado = obtener_estado_job(jobs[i].pid);
            printf("[%d] %-15s %d %s\n", i + 1, estado, jobs[i].pid,
                   jobs[i].comando);
        }
    }
}