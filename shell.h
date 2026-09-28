#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>
#include <signal.h>
#include <time.h>

#define MAX_LINEAS 1024 //Tamaño maximo del buffer para la linea de entrada
#define MAX_TOKENS 128  //Tamaño maximo de tokens permitidos por comando
#define MAX_COMANDOS 64 
#define MAX_JOBS 64     //Cantidad maxima de procesos     
#define MAX_PIDS_POR_JOB 64

//Estructura para los jobs
typedef struct {
    pid_t pid;
    int terminado;
} JobProcess;

typedef struct {
    pid_t pgid;
    JobProcess procesos[MAX_PIDS_POR_JOB];
    int nprocesos;
    int restantes;
    int activo;
    int detenido;
    char comando[MAX_LINEAS];

    unsigned long pmon_ultimo_tiempo_usuario;
    unsigned long pmon_ultimo_tiempo_sistema;
    struct timespec pmon_ultimo_tiempo_real;
    int pmon_iniciado;
} Job;

//parse.c
int parse_line(char *line, char *tokens[], int max_tokens);
int parse_pipes(char *line, char *comandos[], int max_comandos);
int expandir_variables(char *line, size_t capacidad);
int normalizar_operadores(char *line, size_t capacidad);

//jobs.c
extern Job jobs[MAX_JOBS];
extern volatile sig_atomic_t hay_hijos_terminados;

void manejar_sigchld(int signal_number);
int registrar_job(const pid_t pids[], int npids, pid_t pgid, const char *comando, int detenido);
void recolectar_jobs(void);
void listar_jobs(void);
Job *buscar_job(char *especificador);
void marcar_job_completado(Job *job);

//exec.c
void ejecutar_comando(char *tokens[], int background, const char *comando);
void ejecutar_pipes(char *comandos_pipe[], int num_comandos, int background, const char *comando);

//builtins.c
void comando_cd(char *tokens[]);
void comando_fg(char *tokens[]);
void comando_bg(char *tokens[]);

//pmon.c
void pmon_run(int intervalo_segundo);

#endif