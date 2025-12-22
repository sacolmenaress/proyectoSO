#include "log.h"
#include "architecture.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>



static FILE *log_file = NULL;

void abrir_log(void) {
    log_file = fopen("log.txt", "a");  // No borrar contenido previo del log
    if (log_file == NULL) {
        perror("Error abriendo log.txt");
        exit(EXIT_FAILURE);
    }
    
    time_t ahora;
    time(&ahora);
    fprintf(log_file, "\nInicio de sesión: %s", ctime(&ahora));
    fflush(log_file);
}

void escribir_log(const char *mensaje) {
    if (log_file != NULL) {
        fprintf(log_file, "%s\n", mensaje);
        fflush(log_file);  
    }
}

void cerrar_log(void) {
    if (log_file != NULL) {
        time_t ahora;
        time(&ahora);
        fprintf(log_file, "Fin de sesión: %s\n", ctime(&ahora));
        fclose(log_file);
        log_file = NULL;
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
