#include "architecture.h"
#include "log.h" 
#include <stdlib.h>
#include <stdio.h>

// Memoria principal
Word RAM[RAM_SIZE];
// Disco Virtual
Word DISK[DISK_SIZE];

// CPU global
CPU_t cpu;

// Obtener valor real de la palabra (signo + magnitud)
int obtenerValorReal(Word w) {
    return (w.sign == 1) ? -(w.value) : w.value;
}

// Asignar valor a la palabra (controlando signo y magnitud)
// Formato: 1 dígito signo + 7 dígitos magnitud = 8 dígitos totales
void asignarValor(Word *w, int resultado) {
    if (resultado < 0) {
        w->sign = 1; //Signo negativo
        w->value = abs(resultado) % 10000000;  // 7 dígitos: 0-9999999
    } else {
        w->sign = 0; //Signo positivo
        w->value = resultado % 10000000;  // 7 dígitos: 0-9999999
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
        cpu.halted = 1;
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
    //Sumamos la direccion base si estamos en modo usuario (+ 300)
    int dirReal = (cpu.PSW.mode == MODE_KERNEL) ? direccion : direccion + cpu.mp.base;

    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
        printf("Interrupción: Direccionamiento inválido (direccion=%d)\n", dirReal);
        cpu.halted = 1;
        return 0;
    }

    *w = RAM[dirReal];
    return 1;
}

// Escribir memoria (respetando RB y RL)
int escribirMemoria(int direccion, Word w) {
    //Sumamos la direccion base si estamos en modo usuario (+ 300)
    int dirReal = (cpu.PSW.mode == MODE_KERNEL) ? direccion : direccion + cpu.mp.base;

    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
        printf("Interrupción: Direccionamiento inválido (direccion=%d)\n", dirReal);
        cpu.halted = 1;
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
    cpu.PSW.mode = MODE_USER; //Siempre inicia en modo usuario
    cpu.PSW.interrupt = 0;
    cpu.PSW.pc = 0;
    cpu.halted = 0; // Inicializar flag de parada

    // Inicializar DMA
    cpu.dma.track = 0;
    cpu.dma.cylinder = 0;
    cpu.dma.sector = 0;
    cpu.dma.io_type = 0;
    cpu.dma.mem_addr = 0;

    for (int i = 0; i < RAM_SIZE; i++) {
        RAM[i].sign = 0;
        RAM[i].value = 0;
    }

    // Inicializar DISCO con ceros
    for (int i = 0; i < DISK_SIZE; i++) {
        DISK[i].sign = 0;
        DISK[i].value = 0;
    }
}

void fetch() {
    // 1. Fase de Búsqueda: MAR <- PC
    cpu.MAR.address = cpu.PSW.pc;

    // 2. Leer Memoria: MDR <- Mem[MAR]
    if (!leerMemoria(cpu.MAR.address, &cpu.MDR.data)) { // Interrupción ya activada en leerMemoria
        cpu.halted = 1; // Detener si falla el fetch
        return;
    }


    Word w = cpu.MDR.data;
    // Combinar signo (1 dígito) y magnitud (7 dígitos) para obtener los 8 dígitos totales
    long long total = (long long)w.sign * 10000000LL + w.value;
    
    cpu.IR.opcode      = (int)(total / 1000000);        // 2 dígitos (00-99)
    cpu.IR.addressing  = (int)((total / 100000) % 10);  // 1 dígito  (0-9)
    cpu.IR.operand     = (int)(total % 100000);         // 5 dígitos (00000-99999)

}

//Interpretar el modo de direccionamiento y obtener el operando
int obtenerOperando(int *ok) {
    Word w;

    *ok = 1;

    switch (cpu.IR.addressing) {

        case ADDR_IMMEDIATE:
            return cpu.IR.operand;

        case ADDR_DIRECT:
            if (!leerMemoria(cpu.IR.operand, &w)) {
                *ok = 0;
                return 0;
            }
            return obtenerValorReal(w);

        case ADDR_INDEXED: {
            int direccion = obtenerValorReal(cpu.AC) + cpu.IR.operand;
            if (!leerMemoria(direccion, &w)) {
                *ok = 0;
                return 0;
            }
            return obtenerValorReal(w);
        }

        default:
            printf("Interrupción: Modo de direccionamiento inválido (%d)\n",
                cpu.IR.addressing);
            cpu.halted = 1;
            *ok = 0;
            return 0;
    }
}

// Función auxiliar para actualizar PSW.condition (0=Cero, 1=Neg, 2=Pos, 3=Overflow)
void actualizarCodCond(long long resultado) {
    if (resultado > 9999999 || resultado < -9999999) {
        cpu.PSW.condition = 3; // Overflow
    } else if (resultado == 0) {
        cpu.PSW.condition = 0; // Cero
    } else if (resultado < 0) {
        cpu.PSW.condition = 1; // Negativo (X < Y)
    } else {
        cpu.PSW.condition = 2; // Positivo (X > Y)
    }
}

//Funcion que interpreta el opcode y llama a la función de ejecución correspondiente
void decodeExecute() {
switch(cpu.IR.opcode) {

    case OPC_SUM: { // 0
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int ok;
        int valor = obtenerOperando(&ok);
        if (ok) {
            long long resultado = (long long)obtenerValorReal(cpu.AC) + valor;
            actualizarCodCond(resultado);
            asignarValor(&cpu.AC, (int)resultado);
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_RES: { // 1
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int ok;
        int valor = obtenerOperando(&ok);
        if (ok) {
            long long resultado = (long long)obtenerValorReal(cpu.AC) - valor;
            actualizarCodCond(resultado);
            asignarValor(&cpu.AC, (int)resultado);
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_MULT: { // 2
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int ok;
        int valor = obtenerOperando(&ok);
        if (ok) {
            long long resultado = (long long)obtenerValorReal(cpu.AC) * valor;
            actualizarCodCond(resultado);
            asignarValor(&cpu.AC, (int)resultado);
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_DIVI: { // 3
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int ok;
        int valor = obtenerOperando(&ok);
        if (ok) {
            if(valor == 0) {
                printf("Interrupción: División por cero\n");
                cpu.halted = 1;
            } else {
                long long resultado = (long long)obtenerValorReal(cpu.AC) / valor;
                actualizarCodCond(resultado);
                asignarValor(&cpu.AC, (int)resultado);
            }
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_LOAD: { // 4
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int ok;
        int valor = obtenerOperando(&ok);
        if (ok) {
            asignarValor(&cpu.AC, valor);
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_STR: { // 5
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int dir;
        int ok = 1;

        if (cpu.IR.addressing == ADDR_DIRECT) {
            dir = cpu.IR.operand;
        } else if (cpu.IR.addressing == ADDR_INDEXED) {
            dir = obtenerValorReal(cpu.AC) + cpu.IR.operand;
        } else {
            printf("Interrupción: Modo de direccionamiento inválido para STR (%d)\n", cpu.IR.addressing);
            cpu.halted = 1;
            ok = 0;
        }

        if (ok) {
            escribirMemoria(dir, cpu.AC);
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
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
        int ok;
        int a = obtenerValorReal(cpu.AC);
        int b = obtenerOperando(&ok);
        
        if (ok) {
            if (a == b) {
                cpu.PSW.condition = 0; // X = Y
            } else if (a < b) {
                cpu.PSW.condition = 1; // X < Y
            } else {
                cpu.PSW.condition = 2; // X > Y
            }
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_JMPE: { // 9
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        if (cpu.stack.sp > OS_RESERVED) {
            Word top = RAM[cpu.stack.sp - 1];
            if (obtenerValorReal(cpu.AC) == obtenerValorReal(top)) {
                Word target;
                if (leerMemoria(cpu.IR.operand, &target)) {
                    cpu.PSW.pc = obtenerValorReal(target) - 1;
                    printf("DEBUG: JMPE -> Salto indirecto a PC=%d\n", cpu.PSW.pc + 1);
                }
            }
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_JMPNE: { // 10
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        if (cpu.stack.sp > OS_RESERVED) {
            Word top = RAM[cpu.stack.sp - 1];
            if (obtenerValorReal(cpu.AC) != obtenerValorReal(top)) {
                Word target;
                if (leerMemoria(cpu.IR.operand, &target)) {
                    cpu.PSW.pc = obtenerValorReal(target) - 1;
                    printf("DEBUG: JMPNE -> Salto indirecto a PC=%d\n", cpu.PSW.pc + 1);
                }
            }
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_JMPLT: { // 11
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        if (cpu.stack.sp > OS_RESERVED) {
            Word top = RAM[cpu.stack.sp - 1];
            if (obtenerValorReal(cpu.AC) < obtenerValorReal(top)) {
                Word target;
                if (leerMemoria(cpu.IR.operand, &target)) {
                    cpu.PSW.pc = obtenerValorReal(target) - 1;
                    printf("DEBUG: JMPLT -> Salto indirecto a PC=%d\n", cpu.PSW.pc + 1);
                }
            }
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_JMPLGT: { // 12
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        if (cpu.stack.sp > OS_RESERVED) {
            Word top = RAM[cpu.stack.sp - 1];
            if (obtenerValorReal(cpu.AC) > obtenerValorReal(top)) {
                Word target;
                if (leerMemoria(cpu.IR.operand, &target)) {
                    cpu.PSW.pc = obtenerValorReal(target) - 1;
                    printf("DEBUG: JMPLGT -> Salto indirecto a PC=%d\n", cpu.PSW.pc + 1);
                }
            }
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_SVC: { // 13
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        int param = (cpu.stack.sp > OS_RESERVED) ? obtenerValorReal(RAM[cpu.stack.sp - 1]) : 0;
        printf("Llamada al sistema: código AC=%d, parámetro en stack=%d\n", obtenerValorReal(cpu.AC), param);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_RETRN: { // 14
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        // Retorno: PC = POP()
        if (cpu.stack.sp > OS_RESERVED) {
            cpu.AC = RAM[--cpu.stack.sp];
            cpu.PSW.pc = obtenerValorReal(cpu.AC) - 1; // -1 porque incrementarPC sumará 1
            printf("DEBUG: RETRN -> PC restaurado a %d\n", cpu.PSW.pc + 1);
        } else {
            printf("Error: Stack Underflow en RETRN\n");
            cpu.halted = 1;
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_HAB: { // 15
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.PSW.interrupt = 1; // Interrupciones Habilitadas = 1
        printf("Sistema: Interrupciones habilitadas (HSW.int=1)\n");
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_DHAB: { // 16
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.PSW.interrupt = 0; // Interrupciones Deshabilitadas = 0
        printf("Sistema: Interrupciones deshabilitadas (PSW.int=0)\n");
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_TTI: { // 17
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        printf("Sistema: Temporizador TTI configurado cada %d ciclos\n", cpu.IR.operand);
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
        int target = cpu.IR.operand;
        if (cpu.IR.addressing == ADDR_INDEXED) {
            target = obtenerValorReal(cpu.AC) + cpu.IR.operand;
        }
        printf("DEBUG: Executing JMP to logical address %d\n", target);
        cpu.PSW.pc = target - 1; // -1 porque incrementarPC sumará 1
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    case OPC_SDMAP:   // 28
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.dma.track = cpu.IR.operand;
        printf("DMA: Pista configurada a %d\n", cpu.dma.track);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    case OPC_SDMAC:   // 29
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.dma.cylinder = cpu.IR.operand;
        printf("DMA: Cilindro configurado a %d\n", cpu.dma.cylinder);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    case OPC_SDMAS:   // 30
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.dma.sector = cpu.IR.operand;
        printf("DMA: Sector configurado a %d\n", cpu.dma.sector);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    case OPC_SDMAIO:  // 31
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.dma.io_type = cpu.IR.operand;
        printf("DMA: Modo I/O configurado a %s (%d)\n", 
            (cpu.dma.io_type == 0) ? "Lectura (Disco->RAM)" : "Escritura (RAM->Disco)", 
            cpu.dma.io_type);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    case OPC_SDMAM:   // 32
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        cpu.dma.mem_addr = cpu.IR.operand;
        printf("DMA: Dirección de memoria RAM configurada a %d\n", cpu.dma.mem_addr);
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    case OPC_SDMAON: { // 33
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        
        // Calcular dirección lineal en el disco
        int disk_addr = (cpu.dma.cylinder * DISK_TRACKS * DISK_SECTORS) + 
                        (cpu.dma.track * DISK_SECTORS) + 
                        cpu.dma.sector;
        
        if (disk_addr >= DISK_SIZE || disk_addr < 0) {
            printf("Error DMA: Dirección de disco inválida (%d)\n", disk_addr);
            cpu.halted = 1;
        } else {
            // Realizar transferencia siguiendo el modo usuario/kernel
            if (cpu.dma.io_type == 0) { // Lectura: Disco -> RAM
                Word data = DISK[disk_addr];
                if (escribirMemoria(cpu.dma.mem_addr, data)) {
                    printf("DMA: Transferencia EXITOSA [Disco:%d] -> [RAM:%d] | Valor: %d\n", 
                        disk_addr, cpu.dma.mem_addr, obtenerValorReal(data));
                }
            } else { // Escritura: RAM -> Disco
                Word data;
                if (leerMemoria(cpu.dma.mem_addr, &data)) {
                    DISK[disk_addr] = data;
                    printf("DMA: Transferencia EXITOSA [RAM:%d] -> [Disco:%d] | Valor: %d\n", 
                        cpu.dma.mem_addr, disk_addr, obtenerValorReal(data));
                }
            }
        }
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }

    default: {
        log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
        printf("Interrupción: Instrucción inválida (opcode=%d)\n", cpu.IR.opcode);
        cpu.halted = 1;
        log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
        break;
    }
}
}


void ejecutarInst() {
    if(cpu.halted) return;  // No ejecuta instrucción si el sistema está detenido
    fetch();
    decodeExecute();
    incrementarPC();
}


