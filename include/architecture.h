#ifndef ARCHITECTURE_H
#define ARCHITECTURE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

// Constantes del sistema
#define RAM_SIZE 2000
#define OS_RESERVED 300 // 300 direcciones reservadas para el SO

// Layout del Área Reservada del SO (0 a 299)
// [0..8]    Vector de Interrupciones (dirección del manejador para cada código)
// [20]      Manejador genérico de interrupciones (instrucción RETRN)
// [21..29]  Reservado para futuros manejadores
// [30..299] Pila del Sistema (crece hacia abajo desde 299)
#define OS_IVT_START 0        // Inicio del Vector de Interrupciones
#define OS_HANDLER_ADDR 20    // Dirección del manejador genérico
#define OS_STACK_TOP 299      // Tope de la pila del sistema
#define OS_STACK_BOTTOM 30    // Fondo mínimo de la pila del sistema

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

// ============================================================
// Códigos de operación (ISA — Conjunto de Instrucciones)
// ============================================================
// Formato de instrucción: SMMMMMMM (8 dígitos)
//   S = primer dígito del opcode (almacenado en Word.sign)
//   M1 = segundo dígito del opcode (almacenado en primer dígito de Word.value)
//   M2 = modo de direccionamiento (0=directo, 1=inmediato, 2=indexado)
//   M3-M7 = operando (5 dígitos)
// ============================================================

// Aritméticas (00-03)
#define OPC_ADD  0   // SUM:  AC = AC + operando
#define OPC_SUB  1   // RES:  AC = AC - operando
#define OPC_MUL  2   // MULT: AC = AC * operando
#define OPC_DIVI 3   // DIVI: AC = AC / operando

// Transferencia de datos (04-07)
#define OPC_LOAD   4   // LOAD:   AC = operando
#define OPC_STORE  5   // STR:    M[operando] = AC
#define OPC_LOADRX 6   // LOADRX: RX = operando          [NUEVO]
#define OPC_STRRX  7   // STRRX:  M[operando] = RX       [NUEVO]

// Comparación (08)
#define OPC_COMP 8   // COMP: Compara AC con operando, actualiza CC  [NUEVO]

// Saltos condicionales (09-12) — comparan AC con operando
#define OPC_JEQ  9   // JMPE:  salta si AC == operando
#define OPC_JNE  10  // JMPNE: salta si AC != operando
#define OPC_JL   11  // JMPLT: salta si AC <  operando
#define OPC_JG   12  // JMPLGT: salta si AC > operando

// Sistema (13-18)
#define OPC_SVC   13  // SVC:   Llamada al sistema
#define OPC_RETRN 14  // RETRN: Retorno de interrupción
#define OPC_HAB   15  // HAB:   Habilitar interrupciones  [NUEVO]
#define OPC_DHAB  16  // DHAB:  Deshabilitar interrupciones [NUEVO]
#define OPC_TTI   17  // TTI:   Timer Tick Interrupt      [NUEVO]
#define OPC_CHMOD 18  // CHMOD: Cambiar modo (User/Kernel) [NUEVO]

// Registros Base/Límite (19-24)
#define OPC_LOADRB 19  // LOADRB: RB = operando
#define OPC_STRRB  20  // STRRB:  M[operando] = RB
#define OPC_LOADRL 21  // LOADRL: RL = operando
#define OPC_STRRL  22  // STRRL:  M[operando] = RL
#define OPC_LOADSP 23  // LOADSP: SP = operando           [NUEVO]
#define OPC_STRSP  24  // STRSP:  M[operando] = SP        [NUEVO]

// Stack (25-26)
#define OPC_PSH 25  // PSH: Push AC a la pila
#define OPC_POP 26  // POP: Pop de la pila a AC

// Salto incondicional (27)
#define OPC_J 27  // J: Salto incondicional a operando

// DMA (28-33)
#define OPC_SDMAP  28  // SDMAP:  Establecer pista
#define OPC_SDMAC  29  // SDMAC:  Establecer cilindro
#define OPC_SDMAS  30  // SDMAS:  Establecer sector
#define OPC_SDMAIO 31  // SDMAIO: Establecer tipo E/S (0=leer, 1=escribir)
#define OPC_SDMAM  32  // SDMAM:  Establecer dirección de memoria
#define OPC_SDMAON 33  // SDMAON: Iniciar transferencia DMA

#define OPC_HALT 99  // HALT: Detener CPU

// Códigos de Interrupción
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
  int status;    // ESTADOdma: 0 = éxito, 1 = error
  int pending;   // 1 = hay transferencia pendiente (señal para el hilo DMA)
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
  int system_sp;   // Stack pointer del sistema (pila en RAM[30-299], crece hacia abajo)
} CPU_t;

// Variables Globales
extern CPU_t cpu;
extern Word RAM[RAM_SIZE];
extern Word DISK[DISK_SIZE];
extern int vectorInterrupciones[INTERRUPT_VECTOR_SIZE];
extern pthread_mutex_t bus_mutex;  // Mutex POSIX para arbitraje de bus
extern pthread_cond_t  dma_cond;   // Condición para despertar hilo DMA
extern pthread_t       dma_thread; // Hilo del DMA

// Prototipos
void inicializarCPU(void);
void inicializarMemoria(void);
void inicializarDMAThread(void); // Crear e iniciar el hilo DMA
void ejecutarInst(void);
int obtenerOperando(int *ok);
int obtenerValorReal(Word w);

/* Funciones de la Pila del Sistema (RAM[30-299]) */
int sysPush(int valor);
int sysPop(int *valor);

/* Hook para que el hardware notifique al Kernel de C sobre una interrupción.
 * Retorna 1 si el kernel manejó la interrupción (no ejecutar RETRN).
 * Retorna 0 si no la manejó (seguir flujo normal con handler de RAM). */
int kernel_interrupt_handler(int codigo, int operando);

#endif
