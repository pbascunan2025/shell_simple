#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "shell.h"

static volatile sig_atomic_t pmon_refrescar = 0;
static volatile sig_atomic_t pmon_salir = 0;

//struct temporal para ordenar los procesos en pmon
typedef struct {
    pid_t pid;
    char comando[256];
    const char *estado;
    double cpu;
    long rss;
} PmonRow;

//funcion para comparar dos procesos por %CPU (de mayor a menor)
static int comparar_cpu(const void *a, const void *b) {
    const PmonRow *ra = a;
    const PmonRow *rb = b;
    if (ra->cpu < rb->cpu) return 1;
    if (ra->cpu > rb->cpu) return -1;
    return (ra->pid > rb->pid) - (ra->pid < rb->pid);
}

static void pmon_manejar_alarma(int signo) {

    (void)signo;
    pmon_refrescar = 1;
}

static void pmon_manejar_sigint(int signo) {

    (void)signo;
    pmon_salir = 1;
}

static int leer_proc_stat(pid_t pid, char *estado,
                          unsigned long *utime, unsigned long *stime) {
    char ruta[64];
    snprintf(ruta, sizeof(ruta), "/proc/%d/stat", pid);

    FILE *archivo = fopen(ruta, "r");

    if (!archivo) return -1;

    int pid_leido;
    char comando[256];
    char estado_leido;
    int ok = fscanf(archivo,
                    "%d (%255[^)]) %c "
                    "%*d %*d %*d %*d %*d %*u %*u %*u %*u %*u "
                    "%lu %lu",
                    &pid_leido, comando, &estado_leido, utime, stime);
    fclose(archivo);
    if (ok != 5) return -1;
    *estado = estado_leido;
    return 0;
}

static long leer_memoria_rss(pid_t pid) {

    char ruta[64];
    snprintf(ruta, sizeof(ruta), "/proc/%d/status", pid);

    FILE *archivo = fopen(ruta, "r");

    if (!archivo) return -1;

    char linea[256];
    long memoria_rss = -1;

    while (fgets(linea, sizeof(linea), archivo)) {
        if (strncmp(linea, "VmRSS:", 6) == 0) {
            sscanf(linea + 6, "%ld", &memoria_rss);
            break;
        }
    }
    fclose(archivo);
    return memoria_rss;
}

static const char *nombre_estado(char estado) {
    switch (estado) {
        case 'R': return "ejecutando";
        case 'S': return "durmiendo";
        case 'D': return "esperando";
        case 'Z': return "zombie";
        case 'T': return "detenido";
        default: return "desconocido";
    }
}


static void dibujar_tabla(void) {

    long ticks_por_segundo = sysconf(_SC_CLK_TCK);

    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);

    PmonRow filas[MAX_JOBS];
    int count = 0;

    printf("%-7s %-15s %-12s %-13s %-9s\n",
           "PID", "COMANDO", "ESTADO", "%CPU(aprox)", "RSS(KB)");

    for (int i = 0; i < MAX_JOBS && count < MAX_JOBS; i++) {

        if (!jobs[i].activo || jobs[i].restantes <= 0) {
            continue;
        }

        pid_t pid = jobs[i].pgid;
        char estado;
        unsigned long utime, stime;

        if (leer_proc_stat(pid, &estado, &utime, &stime) != 0) {
            continue;
        }

        double cpu = 0.0;
        if (jobs[i].pmon_iniciado && ticks_por_segundo > 0) {

            unsigned long ahora_ticks = utime + stime;
            unsigned long antes_ticks = jobs[i].pmon_ultimo_tiempo_usuario + jobs[i].pmon_ultimo_tiempo_sistema;
            unsigned long delta_ticks = (ahora_ticks >= antes_ticks) ? ahora_ticks - antes_ticks : 0;
            double tiempo_transcurrido = (double)(ahora.tv_sec - jobs[i].pmon_ultimo_tiempo_real.tv_sec) +
                             (double)(ahora.tv_nsec - jobs[i].pmon_ultimo_tiempo_real.tv_nsec) / 1e9;
            if (tiempo_transcurrido > 0.0) {
                cpu = ((double)delta_ticks / (double)ticks_por_segundo) /
                      tiempo_transcurrido * 100.0;
            }
        }

        jobs[i].pmon_ultimo_tiempo_usuario = utime;
        jobs[i].pmon_ultimo_tiempo_sistema = stime;
        jobs[i].pmon_ultimo_tiempo_real = ahora;
        jobs[i].pmon_iniciado = 1;

        //guarda los datos en el array
        filas[count].pid = pid;
        snprintf(filas[count].comando, sizeof(filas[count].comando), "%.255s", jobs[i].comando);
        filas[count].estado = nombre_estado(estado);
        filas[count].cpu = cpu;
        filas[count].rss = leer_memoria_rss(pid);
        count++;

    }

    //ordena el array usando qsort y la funcion de comparar
    qsort(filas, (size_t)count, sizeof(PmonRow), comparar_cpu);

    //imprime el arreglo ya ordenado
    for (int i = 0; i < count; i++) {
        //resalta el primer proceso si es que está consumiendo CPU
        if (i == 0 && filas[i].cpu > 0.0) {
            printf("\033[1;32m%-7d %-15s %-12s %-13.1f %-9ld\033[0m\n",
                   filas[i].pid, filas[i].comando, filas[i].estado,
                   filas[i].cpu, filas[i].rss);
        } else {
            printf("%-7d %-15s %-12s %-13.1f %-9ld\n",
                   filas[i].pid, filas[i].comando, filas[i].estado,
                   filas[i].cpu, filas[i].rss);
        }
    }

    fflush(stdout);
}

void pmon_run(int intervalo_segundo) {
    if (intervalo_segundo <= 0) intervalo_segundo = 2;

    struct sigaction alarma, anterior_alarma;
    struct sigaction sigint, anterior_sigint;
    memset(&alarma, 0, sizeof(alarma));
    memset(&sigint, 0, sizeof(sigint));
    alarma.sa_handler = pmon_manejar_alarma;
    sigint.sa_handler = pmon_manejar_sigint;
    sigemptyset(&alarma.sa_mask);
    sigemptyset(&sigint.sa_mask);

    sigaction(SIGALRM, &alarma, &anterior_alarma);
    sigaction(SIGINT, &sigint, &anterior_sigint);

    pmon_refrescar = 0;
    pmon_salir = 0;

    recolectar_jobs();
    dibujar_tabla();
    alarm((unsigned int)intervalo_segundo);

    while (!pmon_salir) {

        pause();

        if (pmon_salir) {
            break;
        }

        if (pmon_refrescar) {
            pmon_refrescar = 0;
            // Recoge (waitpid) cualquier job que haya terminado desde el
            // ultimo refresco, para no dejarlo colgado como zombie
            // mientras seguimos dentro de pmon.
            recolectar_jobs();
            printf("\n");
            dibujar_tabla();
            alarm((unsigned int)intervalo_segundo);
        }
    }

    alarm(0);

    sigaction(SIGALRM, &anterior_alarma, NULL);
    sigaction(SIGINT, &anterior_sigint, NULL);
    
    printf("\n");
}