#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "shell.h"

/**
 * Crea proceso hijo para ejecutar un comando externo mediante execvp.
 * El proceso padre espera a que el hijo termine su ejecucion antes de continuar.
 * 
 * @param tokens Arreglo de cadenas terminado en NULL. 
 */
void ejecutar_comando(char *tokens[], int background, const char *comando) {

    //Clona el proceso actual creando un proceso hijo.
    pid_t pid = fork();

    //Evalua si es que se dio un error en la creacion del proceso hijo.
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    //CODIGO DEL PROCESO HIJO
    if (pid == 0) {
        
        int i = 0;
        int idxCorte = -1;

        while (tokens[i]!=NULL){
                if (strcmp(tokens[i], ">") == 0 ||
                strcmp(tokens[i], ">>") == 0 ||
                strcmp(tokens[i], "<") == 0) {

                //se guarda el primer indice donde se encuentra una redireccion
                if (idxCorte ==-1){
                    idxCorte = i;
                }

                if(strcmp(tokens[i], ">") == 0) {
                    //redireccion de salida
                    int fd = open(tokens[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0664);
                    if (fd < 0) {
                        perror("open >");
                        _exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                else if (strcmp(tokens[i], ">>") == 0) {
                    //redireccion de salida (append)
                    int fd = open(tokens[i+1], O_WRONLY | O_CREAT | O_APPEND, 0664);
                    if (fd < 0) {
                        perror("open >>");
                        _exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                else if (strcmp(tokens[i], "<") == 0) {
                    //redireccion de entrada
                    int fd = open(tokens[i+1], O_RDONLY);
                    if (fd < 0){
                        perror("open <");
                        _exit(1);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
            }
            i++;
        }

        //si se encuentra una redireccion, se corta el arreglo
        if(idxCorte !=-1) {
            tokens[idxCorte] = NULL;
        }
        //reemplaza la imagen del proceso hijo por la del binario indicado en tokens[0].
        execvp(tokens[0], tokens);

        //Si execvp retorna, significa que ocurrio un error. 
        perror(tokens[0]);

        //Finaliza el hijo directamente con _exit sin limpiar el buffer.
        _exit(127);
    }

    //CODIGO DEL PROCESO PADRE
    if (background) {
        registrar_job(pid, comando);
        return;
    }

    int status;

    //Bloquea la ejecucion del padre hasta que el hijo termine.
    waitpid(pid, &status, 0);
}


void ejecutar_pipes(char *comandos_pipe[], int num_comandos) {
    int in_fd = 0; //Lee la entrada estandar
    int fd[2];     //Arreglo para el pipe
    pid_t pids[MAX_TOKENS];

    for (int i = 0; i < num_comandos; i++) {
        //Separamos el comando por espacios para obtener sus argumentos
        char *tokens[MAX_TOKENS];
        int ntokens = parse_line(comandos_pipe[i], tokens, MAX_TOKENS);
        
        if (ntokens == 0) continue;

        //Si no es el último comando, se crea un pipe
        if (i < num_comandos - 1) {
            if (pipe(fd) < 0) {
                perror("pipe");
                return;
            }
        }

        pids[i] = fork();

        if (pids[i] < 0) {
            perror("fork");
            return;
        }

        //Proceso de hijo
        if (pids[i] == 0) { 
            
            //Redirigir la ENTRADA
            if (in_fd != 0) {
                dup2(in_fd, STDIN_FILENO); //STDIN ahora es el extremo de lectura del pipe anterior
                close(in_fd);
            }

            //Redirigir la SALIDA (si no es el último comando)
            if (i < num_comandos - 1) {
                dup2(fd[1], STDOUT_FILENO); //STDOUT ahora apunta al extremo de escritura del pipe actual
                close(fd[0]);               //El hijo no lee de su propio pipe, lo cerramos
                close(fd[1]);
            }

            // Ejecutamos el comando
            execvp(tokens[0], tokens);
            perror(tokens[0]);
            _exit(127);
        } else {          
            if (in_fd != 0) {
                close(in_fd); //El padre ya le pasó este in_fd al hijo
            }

            if (i < num_comandos - 1) {
                close(fd[1]);  //Cerramos la entrada
                in_fd = fd[0]; 
            }
        }
    }

    //Esperar a todos los hijos
    for (int i = 0; i < num_comandos; i++) {
        int status;
        waitpid(pids[i], &status, 0); 
    }
}
