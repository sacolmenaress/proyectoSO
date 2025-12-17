#ifndef ARCHITECTURE_H
#define ARCHITECTURE_H

#include <pthread.h>
#include <signal.h>

// Constantes del sistema 
#define RAM_SIZE 2000
#define OS_RESERVED 300

#define MODE_KERNEL 1
#define MODE_USER   0

#define ADDR_DIRECT    0
#define ADDR_IMMEDIATE 1
#define ADDR_INDEXED   2

//Palabra de memoria: 8 dígitos decimales + signo 
typedef struct {
    int sign;          //0 = positivo, 1 = negativo 
    int value;        //* magnitud (0 – 99999999) 
} Word;

//Registros de memoria
typedef struct {
    int address;       //MAR 
} MAR_t;

typedef struct {
    Word data;         //MDR 
} MDR_t;

// Registro de instrucción 
typedef struct {
    int opcode;        // 2 dígitos 
    int addressing;   // 1 dígito 
    int operand;      // 5 dígitos 
} IR_t;

// Registros de protección 
typedef struct {
    int base;          // RB 
    int limit;         // RL 
} MemoryProtection_t;

// Registros de pila 
typedef struct {
    int rx;            // Índice 
    int sp;            // Stack Pointer 
} StackRegisters_t;

// PSW: Program Status Word 
typedef struct {
    int condition;     // 1 dígito (0-3)
    int mode;          // 1 dígito (Kernel = 1 / Usuario = 0)
    int interrupt;     // 1 dígito (Flag de interrupción 0 = no habilitada, 1 = sí habilitada)
    int pc;            // 5 dígitos Program Counter 
} PSW_t;

// CPU virtual 
typedef struct {
    Word AC;           // Acumulador
    MAR_t MAR;
    MDR_t MDR;
    IR_t IR;
    MemoryProtection_t mp;
    StackRegisters_t stack;
    PSW_t PSW;
} CPU_t;

// Memoria principal 
extern Word RAM[RAM_SIZE];

// CPU global 
extern CPU_t cpu;

#endif
