#include <stdio.h>
#include "architecture.h"
#include "log.h"

int main() {
    // Abrir log
    abrir_log();

    // Inicializar CPU y RAM
    inicializarCPU();

    // Cargar instrucciones de prueba en RAM
    // Formato de palabra: signo + 7 dígitos (opcode(2)+addressing(1)+operand(4))
    // Por simplicidad, usamos direccionamiento inmediato (addressing=0)
    RAM[0].sign = 0; RAM[0].value = 0000000 + OPC_SUM * 1000000 + 0 * 100000 + 5;   // SUM AC +5
    RAM[1].sign = 0; RAM[1].value = 0000000 + OPC_RES * 1000000 + 0 * 100000 + 2;   // RES AC -2
    RAM[2].sign = 0; RAM[2].value = 0000000 + OPC_MULT * 1000000 + 0 * 100000 + 3;  // MULT AC *3
    RAM[3].sign = 0; RAM[3].value = 0000000 + OPC_DIVI * 1000000 + 0 * 100000 + 2;  // DIVI AC /2
    RAM[4].sign = 0; RAM[4].value = 0000000 + OPC_SDMAP * 1000000;                    // DMA simulada
    RAM[5].sign = 0; RAM[5].value = 0000000 + OPC_HAB * 1000000;                      // Habilitar interrupciones
    RAM[6].sign = 0; RAM[6].value = 0000000 + OPC_DHAB * 1000000;                     // Deshabilitar interrupciones
    RAM[7].sign = 0; RAM[7].value = 0000000 + OPC_J * 1000000 + 0 * 100000 + 2;       // JMP a PC=2

    // Ejecutar instrucciones hasta la 8va
    cpu.PSW.pc = 0;
    for(int i = 0; i < 8; i++) {
        ejecutarInst();
    }

    // Cerrar log
    cerrar_log();

    printf("Prueba finalizada. Revisar log.txt para verificar ejecución.\n");

    return 0;
}
