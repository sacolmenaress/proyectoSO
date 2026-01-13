#include "log.h"
#include "architecture.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>



static const char* LOG_FILENAME = "log.txt";

void abrir_log(void) {
    FILE *f = fopen(LOG_FILENAME, "a");
    if (f == NULL) {
        perror("Error abriendo log.txt");
        exit(EXIT_FAILURE);
    }
    time_t ahora;
    time(&ahora);
    fprintf(f, "\n--- Nueva Sesión del Simulador: %s", ctime(&ahora));
    fclose(f);
    printf("Sistema de log activo (log.txt)\n");
}

void escribir_log(const char *mensaje) {
    FILE *f = fopen(LOG_FILENAME, "a");
    if (f != NULL) {
        fprintf(f, "%s\n", mensaje);
        fclose(f);
    } else {
        perror("Fallo en escribir_log al abrir log.txt");
    }
}

void cerrar_log(void) {
    FILE *f = fopen(LOG_FILENAME, "a");
    if (f != NULL) {
        time_t ahora;
        time(&ahora);
        fprintf(f, "--- Fin de Sesión: %s\n", ctime(&ahora));
        fclose(f);
    }
}

void log_inicio_instruccion(int pc, int opcode) {
    char mensaje[200];
    snprintf(mensaje, sizeof(mensaje), "PC=%d, Ejecutando opcode=%d", pc, opcode);
    escribir_log(mensaje);
}

void log_resultado_instruccion(Word AC, int sp, int condition) {
    char mensaje[200];
    snprintf(mensaje, sizeof(mensaje), "AC=%d, SP=%d, Cond=%d", obtenerValorReal(AC), sp, condition);
    escribir_log(mensaje);
}
