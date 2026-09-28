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
    if (max_tokens < 2) return -1;

    int ntokens = 0; //Cantidad total de tokens extraidos.
    char *p = line; 

    while (*p != '\0') {

        while (isspace((unsigned char)*p)) { 
            p++;
        }

        if (*p == '\0') {
            break;
        }    
        if (ntokens >= max_tokens - 1) {
            return -1;
        }

        char *inicio = p;
        char *write = p;
        int dentro = 0;
        char comilla = 0;

        while (*p != '\0') {
            if (dentro) {
                if (*p == comilla) {
                    dentro = 0;
                    p++;
                } else {
                    *write++ = *p++;
                }
                continue;
            }

            if (*p == '\'' || *p == '"') {
                dentro = 1;
                comilla = *p++;
                continue;
            }
            if (isspace((unsigned char)*p)) {
                p++;
                break;
            }
            *write++ = *p++;
        }

        if (dentro) return -1;
        *write = '\0';
        tokens[ntokens++] = inicio;
    }

    //Se añade el NULL al final del arreglo (execvp lo requiere asi).
    tokens[ntokens] = NULL;

    return ntokens;
}

int normalizar_operadores(char *line, size_t capacidad) {
    char salida[MAX_LINEAS];
    size_t i = 0, j = 0;
    char comilla = 0;

    while (line[i] != '\0') {
        char c = line[i];
        if (comilla) {
            if (c == comilla) comilla = 0;
            if (j + 1 >= sizeof(salida)) return -1;
            salida[j++] = line[i++];
            continue;
        }
        if (c == '\'' || c == '"') {
            comilla = c;
            if (j + 1 >= sizeof(salida)) return -1;
            salida[j++] = line[i++];
            continue;
        }
        if (c == '>' && line[i + 1] == '>') {
            if (j + 3 >= sizeof(salida)) return -1;
            if (j > 0 && salida[j - 1] != ' ' && salida[j - 1] != '\t') salida[j++] = ' ';
            salida[j++] = '>'; salida[j++] = '>';
            i += 2;
            if (line[i] != '\0' && line[i] != ' ' && line[i] != '\t') salida[j++] = ' ';
            continue;
        }
        if (c == '<' || c == '>' || c == '|' || c == '&') {
            if (j + 2 >= sizeof(salida)) return -1;
            if (j > 0 && salida[j - 1] != ' ' && salida[j - 1] != '\t') salida[j++] = ' ';
            salida[j++] = c;
            i++;
            if (line[i] != '\0' && line[i] != ' ' && line[i] != '\t') salida[j++] = ' ';
            continue;
        }
        if (j + 1 >= sizeof(salida)) return -1;
        salida[j++] = c;
        i++;
    }
    if (j >= capacidad || j >= sizeof(salida)) return -1;
    salida[j] = '\0';
    memcpy(line, salida, j + 1);
    return 0;
}

/**
 * Comando Pipes
 * este divide la cadena original en comandos separados por el carácter "|"
 */
int parse_pipes(char *line, char *comandos[], int max_comandos) {
    
    if (max_comandos < 2) {
        return -1;
    }

    int n_comandos = 0;
    char *inicio = line;
    char comilla = 0;

    for (char *p = line; ; p++) {
        char c = *p;

        if (comilla) {
            if (c == comilla) {
                comilla = 0;
            }

            if (c == '\0') {
                return -1;
            }

            continue;
        }

        if (c == '\'' || c == '"') {
            comilla = c;
            continue;
        }

        if (c == '|' || c == '\0') {
            char *fin = p;

            while (inicio < fin && isspace((unsigned char)fin[-1])) {
                fin--;
            }

            while (*inicio && isspace((unsigned char)*inicio)) {
                inicio++;
            }

            if (inicio == fin) {
                return -1;
            }

            if (n_comandos >= max_comandos - 1) {
                return -1;
            }

            *fin = '\0';
            comandos[n_comandos++] = inicio;

            if (c == '\0') {
                break;
            }

            inicio = p + 1;
        }
    }

    //Cerramos el arreglo con un NULL por seguridad
    comandos[n_comandos] = NULL;

    return n_comandos;
}

int expandir_variables(char *linea, size_t capacidad) {

    char buffer[MAX_LINEAS];
    size_t i = 0, j = 0;
    int comilla_simple = 0;

    while (linea[i] != '\0') {
        char c = linea[i];

        if (c == '\'' ) {
            comilla_simple = !comilla_simple;

            if (j + 1 >= sizeof(buffer)) {
                return -1;
            }

            buffer[j++] = linea[i++];
            continue;
        }

        if (!comilla_simple && c == '$') {
            size_t k = i + 1;

            if (linea[k] == '{') {
                k++;
                size_t inicio = k;

                while (isalnum((unsigned char)linea[k]) || linea[k] == '_') {
                    k++;
                }

                if (linea[k] == '}' && k > inicio) {
                    char nombre[128];
                    size_t n = k - inicio;

                    if (n >= sizeof(nombre)) {
                        return -1;
                    }

                    memcpy(nombre, linea + inicio, n);
                    nombre[n] = '\0';
                    const char *valor = getenv(nombre);

                    if (!valor) {
                        valor = "";
                    }

                    size_t nv = strlen(valor);

                    if (j + nv >= sizeof(buffer)) {
                        return -1;
                    }

                    memcpy(buffer + j, valor, nv);
                    j += nv;
                    i = k + 1;
                    continue;
                }
            } else if (isalpha((unsigned char)linea[k]) || linea[k] == '_') {
                size_t inicio = k;

                while (isalnum((unsigned char)linea[k]) || linea[k] == '_') {
                    k++;
                }

                char nombre[128];
                size_t n = k - inicio;

                if (n >= sizeof(nombre)) {
                    return -1;
                }
                
                memcpy(nombre, linea + inicio, n);
                nombre[n] = '\0';
                const char *valor = getenv(nombre);

                if (!valor) {
                    valor = "";
                }
                
                size_t nv = strlen(valor);

                if (j + nv >= sizeof(buffer)) {
                    return -1;
                }

                memcpy(buffer + j, valor, nv);
                j += nv;
                i = k;
                continue;
            }
        }

        if (j + 1 >= sizeof(buffer)) {
            return -1;
        }

        buffer[j++] = linea[i++];
    }

    if (j >= capacidad) {
        return -1;
    }

    buffer[j] = '\0';
    memcpy(linea, buffer, j + 1);
    return 0;
}
