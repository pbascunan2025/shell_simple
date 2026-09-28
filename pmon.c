/**
 * pmon.c: monitor de procesos en background ("mini-top").
 *
 * Lee directamente /proc/[pid]/stat y /proc/[pid]/status de los jobs
 * registrados por la shell y muestra una tabla que se refresca
 * periodicamente con alarm() + SIGALRM. Termina con Ctrl+C sin cerrar
 * la shell.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "shell.h"

// Banderas modificadas por los manejadores de señal (deben ser sig_atomic_t).
static volatile sig_atomic_t pmon_refrescar = 0; // SIGALRM: hay que redibujar la tabla
static volatile sig_atomic_t pmon_salir = 0;     // SIGINT: hay que salir de pmon

//struct temporal para ordenar los procesos en pmon
typedef struct {
    pid_t pid;
    char comando[256];
    const char *estado;
    double cpu;
    long rss;
} PmonRow;

//funcion para comparar dos procesos por %CPU (de mayor a menor)
//ante empate en %CPU, desempata por PID ascendente. Usada por qsort.
static int comparar_cpu(const void *a, const void *b) {
    const PmonRow *ra = a;
    const PmonRow *rb = b;
    if (ra->cpu < rb->cpu) return 1;
    if (ra->cpu > rb->cpu) return -1;
    return (ra->pid > rb->pid) - (ra->pid < rb->pid);
}

/**
 * Manejador de SIGALRM: solo levanta la bandera para que el ciclo principal
 * de pmon_run redibuje la tabla (el trabajo pesado no se hace en el handler).
 */
static void pmon_manejar_alarma(int signo) {

    (void)signo;
    pmon_refrescar = 1;
}

/**
 * Manejador de SIGINT (Ctrl+C) mientras pmon esta activo: levanta la bandera
 * de salida para volver al prompt de la shell sin cerrarla.
 */
static void pmon_manejar_sigint(int signo) {

    (void)signo;
    pmon_salir = 1;
}

/**
 * Lee /proc/[pid]/stat y extrae el estado y los tiempos de CPU del proceso.
 * El nombre del comando (entre parentesis) se lee pero se descarta.
 *
 * @param pid   Proceso a consultar.
 * @param estado Recibe el caracter de estado (R, S, D, Z, T...).
 * @param utime Recibe el tiempo en modo usuario (en ticks de reloj).
 * @param stime Recibe el tiempo en modo kernel (en ticks de reloj).
 *
 * @return int 0 si se leyo correctamente; -1 si el proceso ya no existe
 *             (por ejemplo, termino entre dos lecturas) o el formato es invalido.
 */
static int leer_proc_stat(pid_t pid, char *estado,
                          unsigned long *utime, unsigned long *stime) {
    char ruta[64];
    snprintf(ruta, sizeof(ruta), "/proc/%d/stat", pid);

    FILE *archivo = fopen(ruta, "r");

    if (!archivo) return -1;

    int pid_leido;
    char comando[256];
    char estado_leido;
    // Se saltan los campos 4 al 13 (ppid ... cmajflt) hasta llegar a utime (14) y stime (15).
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

/**
 * Lee la memoria residente (campo VmRSS) desde /proc/[pid]/status.
 *
 * @param pid Proceso a consultar.
 *
 * @return long Memoria residente en KB; -1 si no se pudo leer el archivo
 *              o el campo no existe (ej. procesos zombie).
 */
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

/**
 * Traduce el caracter de estado de /proc al texto que se muestra en la tabla.
 */
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

/**
 * Dibuja una vez la tabla de jobs en background activos.
 *
 * Por cada job activo lee su estado, CPU y RSS desde /proc. El %CPU se calcula
 * como el delta de (utime+stime) entre la lectura anterior y la actual, dividido
 * por el tiempo real transcurrido (CLOCK_MONOTONIC). La lectura anterior se
 * guarda en los campos pmon_* de cada Job, por lo que la primera vez que se
 * muestra un job su %CPU es 0.0.
 *
 * Se monitorea el proceso lider del job (jobs[i].pgid). Si su /proc ya no
 * se puede leer (el proceso termino), el job simplemente no se lista.
 * Las filas se ordenan por %CPU descendente y la primera se resalta.
 */
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

/**
 * Ejecuta el modo de monitoreo pmon (comando interno de la shell).
 *
 * Instala temporalmente manejadores para SIGALRM (refresco periodico con
 * alarm()) y SIGINT (salida limpia), dibuja la tabla y espera con pause()
 * hasta que llegue una señal. Al salir restaura las disposiciones anteriores,
 * de modo que Ctrl+C vuelve a ser ignorado por la shell.
 *
 * @param intervalo_segundo Segundos entre refrescos; si es <= 0 se usa 2.
 */
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

    // Se guardan las disposiciones anteriores para restaurarlas al terminar.
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

    // Cancela cualquier alarma pendiente y restaura los manejadores originales.
    alarm(0);

    sigaction(SIGALRM, &anterior_alarma, NULL);
    sigaction(SIGINT, &anterior_sigint, NULL);
    
    printf("\n");
}