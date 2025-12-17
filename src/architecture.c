#include "architecture.h"
#include <stdlib.h>
#include <stdio.h>

// Memoria principal 
Word RAM[RAM_SIZE]; //RAM de 2000 posiciones
CPU_t cpu; //CPU global


//Para saber si es positivo o negativo, antes de hacer la operación
int obtenerValorReal(Word w) {
    if (w.sign == 1) return -(w.value); //Si el sign es 1, es negativo
    return w.value; //DLC si es 0, es positivo
}

//Pasa el resultado obtenido a el registro (8 digitos, 1 de signo y 7 de valor)
void asignarValor(Word *w, int resultado) {
    //Verifica si el resultado es negativo o positivo
    if (resultado < 0) {
        w->sign = 1;
        w->value = abs(resultado) % 10000000; //Nos quedamos con los 8 primeros digitos
    } else {
        w->sign = 0;
        w->value = resultado % 10000000; //Nos quedamos con los 8 primeros digitos
    }
}

//Cambiar código de condición (0-3)
void cambiarCodCond (int codigo) {
    if (codigo >= 0 && codigo <= 3) cpu.cod_cond = codigo;
}

//Cambiar modo de operación (0-3)
void cambiarModoOp (int modo) {
    if (modo == MODE_KERNEL || modo == MODE_USER) cpu.PSW.mode = modo;
}

//Habilitar/deshabilitar interrupciones
void habilitarInterruociones (int valor){
    cpu.PSW.interrupts = valor ? 1 : 0;
}

int validarPC(int pc) {
    if (pc < 0 || pc >= RAM_SIZE) {
        printf("Interrupcion: Direccionamiento invalido (PC=%d)\n", pc);
        return 0;
    }
    return 1;
}

//Incrementa PC y validar que no se salga de los límites de la RAM
void incrementarPC() {
    cpu.PSW.pc++;
    if (!validarPC(cpu.PSW.pc)) {
        cpu.PSW.interrupt = 1; // Activar interrupción
    }
}

//Leer una instrucción de la RAM 
int leerMemoria(int direccion, Word *w) {
    int dirReal = (cpu.PSW.mode == MODE_KERNEL) ? direccion : direccion + cpu.mp.base;

    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
        printf("Interrupcion: Direccionamiento invalido (direccion=%d)\n", dirReal);
        cpu.PSW.interrupt = 1;
        return 0; // error
    }

    *w = RAM[dirReal];
    return 1; // éxito
}

//Escribir una instrucción en la RAM
int escribirMemoria(int direccion, Word w) {
    int dirReal = (cpu.PSW.mode == MODE_KERNEL) ? direccion : direccion + cpu.mp.base;

    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
        printf("Interrupcion: Direccionamiento invalido (direccion=%d)\n", dirReal);
        cpu.PSW.interrupt = 1;
        return 0; // error
    }

    RAM[dirReal] = w;
    return 1; // éxito
}

//Inicializar la CPU y la RAM
void inicializarCPU() {
    cpu.AC.sign = 0;
    cpu.AC.value = 0;
    cpu.MAR.address = 0;
    cpu.MDR.data.sign = 0;
    cpu.MDR.data.value = 0;
    cpu.IR.opcode = 0;
    cpu.IR.addressing = 0;
    cpu.IR.operand = 0;
    cpu.mp.base = OS_RESERVED;   // Reservado para SO
    cpu.mp.limit = RAM_SIZE - 1; // Límite de usuario
    cpu.stack.rx = 0;
    cpu.stack.sp = OS_RESERVED; // SP inicial
    cpu.PSW.condition = 0;
    cpu.PSW.mode = MODE_USER;
    cpu.PSW.interrupt = 0;
    cpu.PSW.pc = 0;

    // Inicializar RAM
    for (int i = 0; i < RAM_SIZE; i++) {
        RAM[i].sign = 0;
        RAM[i].value = 0;
    }
}