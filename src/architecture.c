#include "architecture.h"
#include <stdlib.h>
#include <stdio.h>

// Memoria principal
Word RAM[RAM_SIZE];

// CPU global
CPU_t cpu;

// Obtener valor real de la palabra (signo + magnitud)
int obtenerValorReal(Word w) {
    return (w.sign == 1) ? -(w.value) : w.value;
}

// Asignar valor a la palabra (controlando signo y 7 dígitos)
void asignarValor(Word *w, int resultado) {
    if (resultado < 0) {
        w->sign = 1;
        w->value = abs(resultado) % 10000000;
    } else {
        w->sign = 0;
        w->value = resultado % 10000000;
    }
}

// Cambiar código de condición (0-3)
void cambiarCodCond(int codigo) {
    if (codigo >= 0 && codigo <= 3)
        cpu.PSW.condition = codigo;
}

// Cambiar modo de operación (Kernel=1 / Usuario=0)
void cambiarModoOp(int modo) {
    if (modo == MODE_KERNEL || modo == MODE_USER)
        cpu.PSW.mode = modo;
}

// Habilitar/deshabilitar interrupciones
void habilitarInterrupciones(int valor) {
    cpu.PSW.interrupt = valor ? 1 : 0;
}

// Validar PC
int validarPC(int pc) {
    if (pc < 0 || pc >= RAM_SIZE) {
        printf("Interrupción: Direccionamiento inválido (PC=%d)\n", pc);
        cpu.PSW.interrupt = 1;
        return 0;
    }
    return 1;
}

// Incrementar PC y validar límites
void incrementarPC() {
    cpu.PSW.pc++;
    validarPC(cpu.PSW.pc);
}

// Leer memoria (respetando RB y RL)
int leerMemoria(int direccion, Word *w) {
    int dirReal = (cpu.PSW.mode == MODE_KERNEL) ? direccion : direccion + cpu.mp.base;

    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
        printf("Interrupción: Direccionamiento inválido (direccion=%d)\n", dirReal);
        cpu.PSW.interrupt = 1;
        return 0;
    }

    *w = RAM[dirReal];
    return 1;
}

// Escribir memoria (respetando RB y RL)
int escribirMemoria(int direccion, Word w) {
    int dirReal = (cpu.PSW.mode == MODE_KERNEL) ? direccion : direccion + cpu.mp.base;

    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
        printf("Interrupción: Direccionamiento inválido (direccion=%d)\n", dirReal);
        cpu.PSW.interrupt = 1;
        return 0;
    }

    RAM[dirReal] = w;
    return 1;
}

// Inicializar CPU y RAM
void inicializarCPU() {
    cpu.AC.sign = 0;
    cpu.AC.value = 0;
    cpu.MAR.address = 0;
    cpu.MDR.data.sign = 0;
    cpu.MDR.data.value = 0;
    cpu.IR.opcode = 0;
    cpu.IR.addressing = 0;
    cpu.IR.operand = 0;
    cpu.mp.base = OS_RESERVED;
    cpu.mp.limit = RAM_SIZE - 1;
    cpu.stack.rx = 0;
    cpu.stack.sp = OS_RESERVED;
    cpu.PSW.condition = 0;
    cpu.PSW.mode = MODE_USER;
    cpu.PSW.interrupt = 0;
    cpu.PSW.pc = 0;

    for (int i = 0; i < RAM_SIZE; i++) {
        RAM[i].sign = 0;
        RAM[i].value = 0;
    }
}
