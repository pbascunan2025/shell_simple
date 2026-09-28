#include <string.h>
#include "shell.h"
#include <stdlib.h>
#include <ctype.h>

/**
 * Divide la cadena de entrada en tokens separados por espacios o tabulaciones.
 * 
 * @param line Cadena original leida desde el teclado.
 * @param tokens Arreglo dende se almacenan los punteros a cada argumento.
 * @param max_tokens Cantidad maxima de elementos que puede almacenar un arreglo de tokens.
 *
 * @return int Cantidad de tokens extraidos 
 */
int parse_line(char *line, char *tokens[], int max_tokens) {
    
    int ntokens = 0; //Cantidad total de tokens extraidos.
    char *saveptr;  //Puntero interno necesario para la reentrancia de strtok_r

    //Elimina el salto de linea que fgets añade al final.
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    //Extrae el primer token delimitado por espacios o tabulaciones.
    char *tok = strtok_r(line, " \t", &saveptr);
    
    //Iteracion para estraer el resto de los tokens.
    //Se reserva el ultimo elemento para el puntero NUll obligatorio.
    while (tok != NULL && ntokens < max_tokens - 1) {
        tokens[ntokens++] = tok;
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    //Se añade el NULL al final del arreglo (execvp lo requiere asi).
    tokens[ntokens] = NULL;

    return ntokens;
}

/**
 * Comando Pipes
 * este divide la cadena original en comandos separados por el carácter "|"
 */
int parse_pipes(char *line, char *comandos[], int max_comandos) {
    int n_comandos = 0;
    char *saveptr; //Puntero interno que strtok_r usa para saber dónde quedó

    //Extraemos el primer trozo de comandos separados por "|"
    char *tok = strtok_r(line, "|", &saveptr);

    //Iteramos mientras siga encontrando el delimitador "|"
    while (tok != NULL && n_comandos < max_comandos - 1) {
        comandos[n_comandos++] = tok;
        
        //Aca strtok_r buscara desde donde se quedó la última vez
        tok = strtok_r(NULL, "|", &saveptr);
    }

    //Cerramos el arreglo con un NULL por seguridad
    comandos[n_comandos] = NULL;

    return n_comandos;
}

void expandir_variables(char *linea) {
    char buffer[MAX_LINEAS] = {0};
    int i = 0, j = 0;

    while (linea[i] != '\0') {
        //si encuentra un $, intentamos expandir
        if (linea[i] == '$') {
            i++;
            char nombre_var[256] = {0};
            int k = 0;

            //extrae el nombre de la variable (letras, numeros, guion bajo)
            while (linea[i] != '\0' && (isalnum(linea[i]) || linea[i] == '_')) {
                nombre_var[k++] = linea[i++];
            }
            nombre_var[k] = '\0';

            //busca el valor en el entorno del sistema/shell
            char *valor = getenv(nombre_var);
            if (valor != NULL) {
                int v = 0;
                //copia el valor al buffer nuevo
                while (valor[v] != '\0' && j < MAX_LINEAS - 1) {
                    buffer[j++] = valor[v++];
                }
            }
            //si getenv retorna NULL, la variable no existe
        } else {
            //si no es un $, copia la letra normal
            buffer[j++] = linea[i++];
        }
    }
    buffer[j] = '\0';
    
    //se sobrescribe la linea original con la version ya expandida
    strcpy(linea, buffer);
}