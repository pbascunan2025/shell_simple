#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>
#include <signal.h>

 
#define MAX_LINEAS 1024 //Tamaño maximo del buffer para la linea de entrada
#define MAX_TOKENS 12   //Tamaño maximo de tokens perimitidos por comando
#define MAX_JOBS 64     //Cantidad maxima de procesos
 
// Estructura para los jobs
typedef struct {
    pid_t pid;
    int activo;
    char comando[MAX_LINEAS];

    unsigned long pmon_ultimo_tiempo_usuario;
    unsigned long pmon_ultimo_tiempo_sistema;
    int pmon_iniciado;
} Job;

//parser.c 
int parse_line(char *line, char *tokens[], int max_tokens);
int parse_pipes(char *line, char *comandos[], int max_comandos);
 
//jobs.c 
extern Job jobs[MAX_JOBS];
extern volatile sig_atomic_t hay_hijos_terminados;
 
void manejar_sigchld(int signal_number);
void registrar_job(pid_t pid, const char *comando);
void recolectar_jobs(void);
void listar_jobs(void);
 
//exec.c
void ejecutar_comando(char *tokens[], int background, const char *comando);
void ejecutar_pipes(char *comandos_pipe[], int num_comandos);
 
//builtins.c
void comando_cd(char *tokens[]);
 
//pmon.c 
void pmon_run(int intervalo_segundo);
 
#endif