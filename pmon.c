#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "shell.h"

static volatile sig_atomic_t pmon_refrescar = 0;
static volatile sig_atomic_t pmon_salir = 0;

static void pmon_manejar_alarma(int num_senal) {

    (void)num_senal;
    pmon_refrescar = 1;
}

static void pmon_manejar_signit(int numm_senal) {

    (void)numm_senal;
    pmon_salir = 1;
}

static int leer_proc_stat(pid_t pid, char *estado, unsigned long *tiempo_usuario, unsigned long *tiempo_sistema) {

    char ruta[64];
    snprintf(ruta, sizeof(ruta), "/proc/%d/stat", pid);

    FILE *archivo = fopen(ruta, "r");

    if (archivo == NULL) {
        return -1;
    }

    int pid_leido;
    char comando[256];
    char estado_leido;

    int campos_leidos = fscanf(archivo, "%d (%255[^)]) %c " "%*d %*d %*d %*d %*d %*u %*u %*u %*u %*u " "%lu %lu", &pid_leido, comando, &estado_leido, tiempo_usuario, tiempo_sistema);

    fclose(archivo);

    if (campos_leidos != 5) {
        return -1;
    }

    *estado = estado_leido;
    return 0;
}

static long leer_memoria_rss(pid_t pid) {

    char ruta[64];
    snprintf(ruta, sizeof(ruta), "/proc/%d/status", pid);

    FILE *archivo = fopen(ruta, "r");

    if ( archivo == NULL) {
        return -1;
    }

    char linea[256];
    long memoria_rss = -1;

    while (fgets(linea, sizeof(linea), archivo) != NULL) {

        if (strncmp(linea, "VmRSS:", 6) == 0) {
            sscanf(linea + 6, "%ld", &memoria_rss);
            break;
        }
    }

    fclose(archivo);
    return memoria_rss;
}

static const char *nombre_estado(char c) {
    switch (c)
    {
    case 'R': return "ejecutando";
    case 'S': return "durmiendo";
    case 'D': return "esperando";
    case 'Z': return "zombie";
    case 'T': return "detenido";
    default: return "desconocido";
    }
}

static void dibujar_tabla(double intervalo_segundos) {

    long ticks_por_segundos = sysconf(_SC_CLK_TCK);

    printf("%-7s %-15s %-12s %-13s %-9s\n", "PID", "COMANDO", "ESTADO", "%CPU(aprox)", "RSS(KB)");

    for (int i = 0; i < MAX_JOBS; i++) {

        if (!jobs[i].activo) {
            continue;
        }

        char estado;
        unsigned long tiempo_usuario, tiempo_sistema;

        if (leer_proc_stat(jobs[i].pid, &estado, &tiempo_usuario, &tiempo_sistema) != 0) {

            jobs[i].activo = 0;
            continue;
        }

        long memoria_rss = leer_memoria_rss(jobs[i].pid);

        double porcentaje_cpu = 0.0;

        if (jobs[i].pmon_iniciado && intervalo_segundos > 0 && ticks_por_segundos > 0) {

            unsigned long ticks_ahora = tiempo_usuario + tiempo_sistema;
            unsigned long ticks_antes = jobs[i].pmon_ultimo_tiempo_usuario + jobs[i].pmon_ultimo_tiempo_sistema;
            unsigned long delta_ticks = ticks_ahora - ticks_antes;
            double delta_cpu_segundos = (double)delta_ticks / (double)ticks_por_segundos;
            porcentaje_cpu = (delta_cpu_segundos / intervalo_segundos) * 100.0;
        }

        jobs[i].pmon_ultimo_tiempo_usuario = tiempo_usuario;
        jobs[i].pmon_ultimo_tiempo_sistema = tiempo_sistema;
        jobs[i].pmon_iniciado = 1;

        printf("%-7d %-15s %-12s %-13.1f %-9ld\n", jobs[i].pid, jobs[i].comando, nombre_estado(estado), porcentaje_cpu, memoria_rss);

    }

    fflush(stdout);   
}

void pmon_run(int intervalo_segundo) {

    if (intervalo_segundo <= 0) {
        intervalo_segundo = 2;
    }

    struct sigaction accion_alarma, accion_int, accion_alarma_anterior, accion_int_anterior;
    
    memset(&accion_alarma, 0, sizeof(accion_alarma));
    accion_alarma.sa_handler = pmon_manejar_alarma;
    sigemptyset(&accion_alarma.sa_mask);
    accion_alarma.sa_flags = 0;
    sigaction(SIGALRM, &accion_alarma, &accion_alarma_anterior);

    memset(&accion_int, 0, sizeof(accion_int));
    accion_int.sa_handler = pmon_manejar_signit;
    sigemptyset(&accion_int.sa_mask);
    accion_int.sa_flags = 0;
    sigaction(SIGINT, &accion_int, &accion_int_anterior);

    pmon_salir = 0;

    // Limpia jobs que ya hayan terminado antes de entrar, para no
    // arrastrar zombies de antes de llamar a pmon.
    recolectar_jobs();
    dibujar_tabla(0);
    alarm(intervalo_segundo);

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
            dibujar_tabla((double)intervalo_segundo);
            alarm(intervalo_segundo);
        }
    }

    alarm(0);

    sigaction(SIGALRM, &accion_alarma_anterior, NULL);
    sigaction(SIGINT, &accion_int_anterior, NULL);

    printf("\n");

}