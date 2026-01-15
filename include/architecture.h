#ifndef ARCHITECTURE_H
#define ARCHITECTURE_H

#include <signal.h>

// Constantes del sistema
#define RAM_SIZE 2000
#define OS_RESERVED 300 // 300 direcciones reservadas para el SO

// Geometría del Disco Virtual
#define DISK_CYLINDERS 10
#define DISK_TRACKS 10
#define DISK_SECTORS 100
#define DISK_SIZE (DISK_CYLINDERS * DISK_TRACKS * DISK_SECTORS)

#define MODE_KERNEL 1
#define MODE_USER 0

#define ADDR_DIRECT 0
#define ADDR_IMMEDIATE 1
#define ADDR_INDEXED 2

// Códigos de operación (Compatibilidad con nombres usados en architecture.c)
#define OPC_ADD 00 // Antes OPC_SUM
#define OPC_SUB 01 // Antes OPC_RES
#define OPC_MUL 02 // Antes OPC_MULT
#define OPC_DIVI 03

#define OPC_LOAD 04
#define OPC_STORE 05  // Antes OPC_STR
#define OPC_MOV_RX 40 // Opcode auxiliar

#define OPC_SVC 13
#define OPC_RETRN 14

// Comparaciones
#define OPC_CMPE 15 // Antes OPC_HAB (reutilizado o cambiado)
#define OPC_CMPL 16 // Antes OPC_DHAB
#define OPC_CMPG 17 // Antes OPC_TTI

// Saltos
#define OPC_JEQ 20 // Antes OPC_STRRB
#define OPC_JNE 21 // Antes OPC_LOADRL
#define OPC_JL 22  // Antes OPC_STRRL
#define OPC_JG 23  // Antes OPC_LOADSP
#define OPC_J 27

// Stack
#define OPC_POP_AM 24 // POP a memoria
#define OPC_PSH 25
#define OPC_POP 26

// DMA
#define OPC_SDMAP 28
#define OPC_SDMAC 29
#define OPC_SDMAS 30
#define OPC_SDMAIO 31
#define OPC_SDMAM 32
#define OPC_SDMAON 33

#define OPC_HALT 99

// Códigos de Interrupción (Punto 13 PDF)
#define INT_INVALID_SYSCALL 0
#define INT_INVALID_INT 1
#define INT_SYSCALL 2
#define INT_CLOCK 3
#define INT_IO_COMPLETE 4
#define INT_INVALID_INST 5
#define INT_INVALID_ADDR 6
#define INT_UNDERFLOW 7
#define INT_OVERFLOW 8

// Tamaño del Vector de Interrupciones
#define INTERRUPT_VECTOR_SIZE 16

// Estructuras

typedef struct {
  int sign;
  int value;
} Word;

typedef struct {
  int address;
} MAR_t;

typedef struct {
  Word data;
} MDR_t;

typedef struct {
  int opcode;
  int addressing;
  int operand;
} IR_t;

typedef struct {
  int base;
  int limit;
} MemoryProtection_t;

typedef struct {
  int rx;
  int sp;
} StackRegisters_t;

typedef struct {
  int condition;
  int mode;
  int interrupt;
  int pc;
} PSW_t;

typedef struct {
  int track;
  int cylinder;
  int sector;
  int io_type;
  int mem_addr;
  int status; // ESTADOdma: 0 = éxito, 1 = error
} DMA_t;

typedef struct {
  Word AC;
  MAR_t MAR;
  MDR_t MDR;
  IR_t IR;
  MemoryProtection_t mp;
  StackRegisters_t stack;
  PSW_t PSW;
  DMA_t dma;
  int halted;
} CPU_t;

// Variables Globales
extern CPU_t cpu;
extern Word RAM[RAM_SIZE];
extern Word DISK[DISK_SIZE];
extern int vectorInterrupciones[INTERRUPT_VECTOR_SIZE];

// Prototipos
void inicializarCPU(void);
void inicializarMemoria(void);
void ejecutarInst(void);
int obtenerOperando(int *ok);
int obtenerValorReal(Word w);

#endif
