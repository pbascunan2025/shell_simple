#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "shell.h"


//===================//
//    Comando cd     //
//===================//


// Comando interno "cd".
void comando_cd(char *tokens[]) {
    const char *directorio;

    //Si no se especifica un directorio, se cambia al directorio home.
    if (tokens[1] == NULL) {
        directorio = getenv("HOME");
        if (directorio == NULL) {
            fprintf(stderr, "cd: No se pudo acceder a directorio HOME\n");
            return;
        }
    } else {
        directorio = tokens[1];
    }

    // Imprime error en color rojo si no se encuentra el directorio.
    if (chdir(directorio) != 0) {
        printf("\033[91mcd: No se encontró el directorio\033[0m '%s'\n", directorio); 
    }

}
