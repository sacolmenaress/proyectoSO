#include "architecture.h"
#include "log.h" 
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

void fetch() {
    if (!leerMemoria(cpu.PSW.pc, &cpu.MDR.data)) { // Interrupción ya activada en leerMemoria
        return;
    }

    // Cargar instrucción en IR
    Word w = cpu.MDR.data;
    // Convertimos la palabra en 8 dígitos a los campos de IR
    int valor = obtenerValorReal(w);
    cpu.IR.opcode      = valor / 1000000;        // 2 dígitos
    cpu.IR.addressing  = (valor / 100000) % 10;  // 1 dígito
    cpu.IR.operand     = valor % 100000;         // 5 dígitos
}

//Funcion que interpreta el opcode y llama a la función de ejecución correspondiente

void decodeExecute() {
switch(cpu.IR.opcode) {

    case OPC_SUM: { // 0
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int valor = (cpu.IR.addressing == ADDR_IMMEDIATE) 
                    ? cpu.IR.operand 
                    : obtenerValorReal(RAM[cpu.IR.operand]);
        asignarValor(&cpu.AC, obtenerValorReal(cpu.AC) + valor);
        cambiarCodCond((obtenerValorReal(cpu.AC) == 0) ? 0 : (obtenerValorReal(cpu.AC) < 0 ? 1 : 2));
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_RES: { // 1
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int valor = (cpu.IR.addressing == ADDR_IMMEDIATE) 
                    ? cpu.IR.operand 
                    : obtenerValorReal(RAM[cpu.IR.operand]);
        asignarValor(&cpu.AC, obtenerValorReal(cpu.AC) - valor);
        cambiarCodCond((obtenerValorReal(cpu.AC) == 0) ? 0 : (obtenerValorReal(cpu.AC) < 0 ? 1 : 2));
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_MULT: { // 2
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int valor = (cpu.IR.addressing == ADDR_IMMEDIATE) 
                    ? cpu.IR.operand 
                    : obtenerValorReal(RAM[cpu.IR.operand]);
        asignarValor(&cpu.AC, obtenerValorReal(cpu.AC) * valor);
        cambiarCodCond((obtenerValorReal(cpu.AC) == 0) ? 0 : (obtenerValorReal(cpu.AC) < 0 ? 1 : 2));
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_DIVI: { // 3
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int valor = (cpu.IR.addressing == ADDR_IMMEDIATE) 
                    ? cpu.IR.operand 
                    : obtenerValorReal(RAM[cpu.IR.operand]);
        if(valor == 0) {
            printf("Interrupción: División por cero\n");
            cpu.PSW.interrupt = 1;
        } else {
            asignarValor(&cpu.AC, obtenerValorReal(cpu.AC) / valor);
            cambiarCodCond((obtenerValorReal(cpu.AC) == 0) ? 0 : (obtenerValorReal(cpu.AC) < 0 ? 1 : 2));
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_LOAD: { // 4
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int dir = cpu.IR.operand;
        if(leerMemoria(dir, &cpu.AC)) {
            log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        }
        break;
    }

    case OPC_STR: { // 5
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int dir = cpu.IR.operand;
        if(escribirMemoria(dir, cpu.AC)) {
            log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        }
        break;
    }

    case OPC_LOADRX: { // 6
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        asignarValor(&cpu.AC, cpu.stack.rx);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_STRRX: { // 7
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.stack.rx = obtenerValorReal(cpu.AC);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_COMP: { // 8
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int valor = (cpu.IR.addressing == ADDR_IMMEDIATE) 
                    ? cpu.IR.operand 
                    : obtenerValorReal(RAM[cpu.IR.operand]);
        cambiarCodCond((obtenerValorReal(cpu.AC) == valor) ? 0 : (obtenerValorReal(cpu.AC) < valor ? 1 : 2));
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_JMPE: { // 9
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        if(obtenerValorReal(cpu.AC) == obtenerValorReal(RAM[cpu.stack.sp])) 
            cpu.PSW.pc = cpu.IR.operand;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_JMPNE: { // 10
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        if(obtenerValorReal(cpu.AC) != obtenerValorReal(RAM[cpu.stack.sp])) 
            cpu.PSW.pc = cpu.IR.operand;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_JMPLT: { // 11
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        if(obtenerValorReal(cpu.AC) < obtenerValorReal(RAM[cpu.stack.sp])) 
            cpu.PSW.pc = cpu.IR.operand;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_JMPLGT: { // 12
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        if(obtenerValorReal(cpu.AC) > obtenerValorReal(RAM[cpu.stack.sp])) 
            cpu.PSW.pc = cpu.IR.operand;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_SVC: { // 13
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        printf("Llamada al sistema: código AC=%d\n", obtenerValorReal(cpu.AC));
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_RETRN: { // 14
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.PSW.pc = cpu.stack.sp;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_HAB: { // 15
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        habilitarInterrupciones(1);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_DHAB: { // 16
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        habilitarInterrupciones(0);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_TTI: { // 17
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        printf("Simulación de temporizador TTI: %d ciclos\n", cpu.IR.operand);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_CHMOD: { // 18
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cambiarModoOp(cpu.IR.operand);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_LOADRB: { // 19
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        asignarValor(&cpu.AC, cpu.mp.base);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_STRRB: { // 20
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.mp.base = obtenerValorReal(cpu.AC);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_LOADRL: { // 21
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        asignarValor(&cpu.AC, cpu.mp.limit);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_STRRL: { // 22
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.mp.limit = obtenerValorReal(cpu.AC);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_LOADSP: { // 23
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        asignarValor(&cpu.AC, cpu.stack.sp);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_STRSP: { // 24
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.stack.sp = obtenerValorReal(cpu.AC);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_PSH: { // 25
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        RAM[cpu.stack.sp++] = cpu.AC;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_POP: { // 26
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.AC = RAM[--cpu.stack.sp];
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_J: { // 27
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.PSW.pc = cpu.IR.operand;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_SDMAP:   // 28
    case OPC_SDMAC:   // 29
    case OPC_SDMAS:   // 30
    case OPC_SDMAIO:  // 31
    case OPC_SDMAM:   // 32
    case OPC_SDMAON:  // 33
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        printf("Instrucción DMA simulada: opcode=%d\n", cpu.IR.opcode);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;

    default: {
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        printf("Interrupción: Instrucción inválida (opcode=%d)\n", cpu.IR.opcode);
        cpu.PSW.interrupt = 1;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }
}
}


void ejecutarInst() {
    if(cpu.PSW.interrupt) return;  // No ejecuta instrucción si hay interrupción activa
    fetch();
    decodeExecute();
    incrementarPC();
}


