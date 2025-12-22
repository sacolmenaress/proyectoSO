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

//Códigs de operación
#define OPC_SUM      0
#define OPC_RES      1
#define OPC_MULT     2
#define OPC_DIVI     3
#define OPC_LOAD     4
#define OPC_STR      5
#define OPC_LOADRX   6
#define OPC_STRRX    7
#define OPC_COMP     8
#define OPC_JMPE     9
#define OPC_JMPNE    10
#define OPC_JMPLT    11
#define OPC_JMPLGT   12
#define OPC_SVC      13
#define OPC_RETRN    14
#define OPC_HAB      15
#define OPC_DHAB     16
#define OPC_TTI      17
#define OPC_CHMOD    18
#define OPC_LOADRB   19
#define OPC_STRRB    20
#define OPC_LOADRL   21
#define OPC_STRRL    22
#define OPC_LOADSP   23
#define OPC_STRSP    24
#define OPC_PSH      25
#define OPC_POP      26
#define OPC_J        27
#define OPC_SDMAP    28
#define OPC_SDMAC    29
#define OPC_SDMAS    30
#define OPC_SDMAIO   31
#define OPC_SDMAM    32
#define OPC_SDMAON   33


//Definimos estructura 

//Palabra de memoria: 8 dígitos decimales + signo 
typedef struct {
    int sign;          // 0 = positivo, 1 = negativo 
    int value;        // magnitud (0 – 99999999) 
} Word;

int obtenerValorReal(Word w);
void asignarValor(Word *w, int resultado);

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
    Word AC;           // Registro Acumulador 
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

// Funciones de ejecución
void fetch();
void decodeExecute();
void inicializarCPU(void);
void ejecutarInst(void);  


#endif
