#include <stdio.h>
#include "shell.h"



    //====================//
    //    Comando pmon    //
    //====================//


    void ejecutar_pmon_basico(void) {
    printf("%-7s | %-20s\n", "PID", "COMANDO");
    printf("--------+----------------------\n");

    for (int i = 0; i < MAX_JOBS; i++) {
        // Solo revisamos los jobs que la shell registró como activos
        if (jobs[i].activo == 1) {
            char path[256];
            // Construimos la ruta
            snprintf(path, sizeof(path), "/proc/%d/stat", jobs[i].pid);
            
            FILE *f_stat = fopen(path, "r");
            if (f_stat != NULL) {
                int pid_leido;
                char comando[256];
                
                // Usamos una expresión regular básica en fscanf para leer hasta el paréntesis de cierre
                if (fscanf(f_stat, "%d (%255[^)])", &pid_leido, comando) == 2) {
                    // Imprimimos los datos
                    printf("%-7d | %-20s\n", pid_leido, comando);
                }
                
                fclose(f_stat);
            } else {
                // Si fopen falla 
                // Lo marcamos como inactivo para no volver a leerlo
                jobs[i].activo = 0;
            }
        }
    }
}
