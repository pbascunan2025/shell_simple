# Shell - Grupo 4 - Sistemas Operativos

## Integrantes
* Cristobal Ignacio Valenzuela Escobar
* Javiera Antonia Diaz Grandon
* Martin Ignacio Bastias Neira
* Pablo Sebastian Bascuñan Espina

## Requisitos previos
* Tener instalado 'gcc'
* La herramienta 'make'
* La biblioteca 'readline'

---

### Compilación mediante Makefile

El repositorio incluye un `Makefile` con las banderas `-Wall -Wextra -std=gnu11`. Para compilar todos los módulos, ejecuta en la raíz del proyecto:

```bash
make
```

---

### Compilación manual directa con GCC

Si no deseas utilizar `make`, puedes compilar y enlazar todos los archivos con el compilador `gcc` ejecutando en la raíz del proyecto:

```bash
gcc -Wall -Wextra -std=gnu11 -o mishell mishell.c parser.c exec.c jobs.c builtins.c pmon.c -lreadline
```

---


## Ejecución

Una vez generado el ejecutable `mishell`, ejecutar la shell con:

```bash
./mishell
```

---

