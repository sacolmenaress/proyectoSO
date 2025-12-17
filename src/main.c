#include <stdio.h>
#include "architecture.h"

int main() {
    // Inicializar CPU
    cpu.AC.sign = 0;
    cpu.AC.value = 0;
    cpu.PSW.condition = 0;
    cpu.PSW.mode = MODE_KERNEL;
    cpu.PSW.interrupt = 0;
    cpu.PSW.unused = 0;
    cpu.PSW.pc = 0;

    // Inicializar RAM
    for(int i = 0; i < RAM_SIZE; i++) {
        RAM[i].sign = 0;
        RAM[i].value = 0;
    }

    printf("Sistema de Arquitectura Virtual inicializado.\n");
    return 0;
}
