#ifndef LOG_H
#define LOG_H

#include "architecture.h"


void abrir_log(void);
void escribir_log(const char *mensaje);
void cerrar_log(void);
void log_inicio_instruccion(int pc, int opcode);
void log_resultado_instruccion(Word AC, int sp, int condition);

#endif