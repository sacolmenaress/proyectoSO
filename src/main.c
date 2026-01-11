#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "architecture.h"
#include "log.h"

// Función para mostrar ayuda, mini menú en terminal
void command_help() {
    printf("Comandos disponibles:\n");
    printf("  load <archivo>       : Cargar un programa en memoria\n");
    printf("  mem <inicio> <cant>  : Ver contenido de memoria\n");
    printf("  reg                  : Ver registros del CPU\n");
    printf("  run                  : Ejecutar el programa hasta finalizar\n");
    printf("  step                 : Ejecutar una instruccion (paso a paso)\n");
    printf("  reset                : Reiniciar CPU y Memoria\n");
    printf("  exit                 : Salir del simulador\n");
}

// Función para cargar programa desde archivo
void command_load(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Error: No se pudo abrir el archivo '%s'.\n", filename);
        return;
    }

    int address = 0;
    char line[256];
    
    // Leer linea por linea
    while (fgets(line, sizeof(line), file)) {
        // Eliminar saltos de linea (handling \n and \r)
        line[strcspn(line, "\r\n")] = 0;
        
        // Ignorar lineas vacias
        if (strlen(line) == 0) continue;
        
        // Ignorar comentarios (//) y metadatos (. o _)
        if (line[0] == '/' || line[0] == '.' || line[0] == '_') continue;

        // Intentar leer un numero de la linea
        int raw_val;
        if (sscanf(line, "%d", &raw_val) == 1) {
             if (address >= RAM_SIZE) {
                 printf("Advertencia: Programa excede tamano de memoria RAM.\n");
                 break;
             }
             
             if (raw_val < 0) {
                RAM[address].sign = 1;
                RAM[address].value = -raw_val;
             } else {
                RAM[address].sign = 0;
                RAM[address].value = raw_val;
             }
             address++;
        }
    }
    
    printf("Programa cargado exitosamente. %d instrucciones/datos leidos.\n", address);
    fclose(file);
    cpu.PSW.pc = 0; // Reset PC to start
}

void command_mem(int start, int count) {
    if (start < 0 || start >= RAM_SIZE) {
        printf("Error: Direccion de inicio invalida.\n");
        return;
    }
    
    printf("Memoria [%d - %d]:\n", start, start + count - 1);
    for (int i = 0; i < count && (start + i) < RAM_SIZE; i++) {
        int idx = start + i;
        int val = RAM[idx].value;
        if(RAM[idx].sign == 1) val = -val;
        printf("  [%04d]: %d (Signo: %d, Valor: %d)\n", idx, val, RAM[idx].sign, RAM[idx].value);
    }
}

void command_reg() {
    printf("=== Registros del CPU ===\n");
    int ac_val = cpu.AC.value;
    if(cpu.AC.sign) ac_val = -ac_val;
    
    printf("  AC : %d\n", ac_val);
    printf("  PC : %d\n", cpu.PSW.pc);
    printf("  MAR: %d\n", cpu.MAR.address);
    printf("  MDR: %d (Signo: %d, Valor: %d)\n", obtenerValorReal(cpu.MDR.data), cpu.MDR.data.sign, cpu.MDR.data.value);
    printf("  SP : %d\n", cpu.stack.sp);
    printf("  IR : OPC=%d, Addr=%d, Op=%d\n", cpu.IR.opcode, cpu.IR.addressing, cpu.IR.operand);
    printf("  PSW: Cond=%d, Mode=%d, Int=%d\n", cpu.PSW.condition, cpu.PSW.mode, cpu.PSW.interrupt);
}

void command_step() {
    if (cpu.PSW.pc >= RAM_SIZE) {
        printf("PC fuera de rango.\n");
        return;
    }
    printf("Ejecutando instruccion en PC=%d...\n", cpu.PSW.pc);
    ejecutarInst();
    command_reg(); 
}

void command_run() {
    printf("Ejecutando programa...\n");
    // Ejecutar hasta que se encuentre una condición de parada o error?
    // Por ahora, pondremos un límite de seguridad o hasta interrupción.
    int max_cycles = 1000;
    int cycles = 0;
    while(cycles < max_cycles) {
        if(cpu.halted) {
            printf("Ejecucion detenida por instruccion Halt o error.\n");
            break;
        }
        ejecutarInst();
        cycles++;
    }
    if (cycles >= max_cycles) {
        printf("Ejecucion detenida: Limite de ciclos alcanzado (%d).\n", max_cycles);
    }
    printf("Ejecucion finalizada.\n");
}

int main() {
    char input[100];
    char *cmd;
    char *arg1;
    char *arg2;

    abrir_log();
    inicializarCPU();

    printf("=== Simulador de SO Fase I ===\n");
    printf("Escriba 'help' para ver los comandos.\n");

    while (1) {
        printf("OS> ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;

        // Parse command
        cmd = strtok(input, " ");
        if (cmd == NULL) continue;

        if (strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0) {
            command_help();
        } else if (strcmp(cmd, "load") == 0) {
            arg1 = strtok(NULL, " ");
            if (arg1) command_load(arg1);
            else printf("Uso: load <archivo>\n");
        } else if (strcmp(cmd, "mem") == 0) {
            arg1 = strtok(NULL, " ");
            arg2 = strtok(NULL, " ");
            if (arg1 && arg2) command_mem(atoi(arg1), atoi(arg2));
            else printf("Uso: mem <inicio> <cantidad>\n"); 
        } else if (strcmp(cmd, "reg") == 0) {
            command_reg();
        } else if (strcmp(cmd, "step") == 0) {
            command_step();
        } else if (strcmp(cmd, "run") == 0) {
            command_run();
        } else if (strcmp(cmd, "reset") == 0) {
            inicializarCPU();
            printf("Sistema reiniciado.\n");
        } else {
            printf("Comando desconocido: %s\n", cmd);
        }
    }

    cerrar_log();
    printf("Simulador terminado.\n");
    return 0;
}
