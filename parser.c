#include <string.h>
#include "shell.h"
#include <stdlib.h>
#include <ctype.h>

/**
 * Divide la cadena de entrada en tokens separados por espacios o tabulaciones.
 * Respeta comillas simples y dobles: el texto entre comillas forma parte de un
 * mismo token y las comillas se eliminan. La cadena se modifica en el lugar y
 * los tokens apuntan dentro de ella.
 * 
 * @param line Cadena original leida desde el teclado.
 * @param tokens Arreglo dende se almacenan los punteros a cada argumento.
 * @param max_tokens Cantidad maxima de elementos que puede almacenar un arreglo de tokens.
 *
 * @return int Cantidad de tokens extraidos; -1 si hay comillas sin cerrar,
 *             si se excede el maximo de tokens o si max_tokens < 2.
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

        // 'inicio' marca el comienzo del token; 'write' es la posicion donde se
        // reescribe el token sin comillas (siempre <= p, por lo que es seguro).
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

/**
 * Inserta espacios alrededor de los operadores <, >, >>, | y & (fuera de
 * comillas) para que parse_line los reconozca como tokens independientes
 * aunque el usuario los escriba pegados (ej. "ls>out.txt" o "a|b").
 *
 * @param line Linea a normalizar; se sobrescribe con el resultado.
 * @param capacidad Tamaño maximo disponible en line (incluido el '\0').
 *
 * @return int 0 si tuvo exito; -1 si el resultado no cabe en el buffer.
 */
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
        // ">>" se trata como un unico operador (append), antes que el ">" simple.
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
 * ignorando los "|" que esten dentro de comillas. Recorta los espacios de cada
 * comando y modifica la cadena en el lugar (los comandos apuntan dentro de ella).
 *
 * @param line Linea completa a dividir.
 * @param comandos Arreglo donde se guardan los punteros a cada comando.
 * @param max_comandos Tamaño del arreglo comandos (se reserva un lugar para el NULL final).
 *
 * @return int Cantidad de comandos; -1 si hay error de sintaxis (comillas sin
 *             cerrar, comando vacio entre pipes o demasiados comandos).
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

/**
 * Expande las referencias a variables de entorno $VAR y ${VAR} dentro de la
 * linea, usando getenv(). Una variable inexistente se reemplaza por cadena
 * vacia. No se expande dentro de comillas simples.
 *
 * @param linea Linea a expandir; se sobrescribe con el resultado.
 * @param capacidad Tamaño maximo disponible en linea (incluido el '\0').
 *
 * @return int 0 si tuvo exito; -1 si el resultado no cabe en el buffer o el
 *             nombre de variable es demasiado largo.
 */
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

            // Forma ${VAR}
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
            // Forma $VAR
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