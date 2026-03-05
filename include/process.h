/*
 * Define el Bloque de Control de Proceso (BCP/PCB),
 * los 5 estados, la tabla de procesos (máx. 20) y los
 * prototipos de las funciones del subsistema de procesos.
 */

#ifndef PROCESS_H
#define PROCESS_H

#include "architecture.h"   /* CPU_t, Word, RAM_SIZE, OS_RESERVED, etc. */
#include <stdlib.h>

/* 
 * CONSTANTES
 */
#define MAX_PROCESSES   20      /* Máximo de procesos simultáneos */
#define PROCESS_QUANTUM  2      /* Quantum del planificador Round-Robin */

/* Tamaño de cada partición de memoria de usuario.
 * Espacio usuario: RAM[300..1999] = 1700 palabras.
 * 5 particiones × 340 = 1700.  Ajustable si se necesita más. */
#define NUM_PARTITIONS   5
#define PARTITION_SIZE   340    /* palabras por partición */

/* 
 * ESTADOS DEL PROCESO
 */
typedef enum {
    STATE_NEW        = 0,   /* Recién creado, en disco, no en RAM */
    STATE_READY      = 1,   /* En RAM, esperando CPU              */
    STATE_RUNNING    = 2,   /* Usando la CPU actualmente          */
    STATE_SLEEPING   = 3,   /* Dormido, esperando tiempo/E/S      */
    STATE_TERMINATED = 4    /* Finalizó o fue cancelado           */
} ProcessState;

/* 
 * CONTEXTO SALVADO DEL PROCESO
 *
 * Snapshot de los campos de CPU_t que pertenecen al usuario.
 * Se copia hacia/desde la CPU global en cada cambio de contexto.
 */
typedef struct {
    /* Registro acumulador (signo y magnitud separados) */
    int    ac_sign;
    int    ac_value;
    /* Program Counter lógico (relativo a la base) */
    int    pc;
    /* Registro índice RX */
    int    rx;
    /* Stack pointer */
    int    sp;
    /* Registro base y límite (valores absolutos en RAM física) */
    int    rb;
    int    rl;
    /* Código de condición */
    int    condition;
    /* Modo de la CPU (Kernel/User) y estado de interrupciones */
    int    mode;
    int    interrupt;
} ProcessContext_t;

/*  — BLOQUE DE CONTROL DE PROCESO */
typedef struct {
    int              pid;           /* ID del proceso (0-19)           */
    char             name[64];      /* Nombre del programa             */
    ProcessState     state;         /* Estado actual                   */

    ProcessContext_t ctx;           /* Contexto salvado de la CPU      */

    /* Gestión de memoria (partición estática) */
    int              partition_id;  /* Índice de partición (0-4, -1=none) */
    int              base;          /* Dirección física de inicio en RAM  */
    int              limit;         /* Dirección física de fin en RAM     */

    /* Información del programa en disco */
    int              disk_offset;   /* Offset en DISK[] donde está guardado */
    int              prog_size;     /* Tamaño en palabras                   */
    int              entry_point;   /* PC de entrada (relativo, base 0)     */

    /* Planificación */
    int              quantum_counter; /* Ticks consumidos en el turno actual */
    int              wake_tick;       /* Tick en que debe despertar (SLEEPING)*/

    /* Persistencia de la Pila del Sistema —
     * Cada proceso guarda su propia copia de la pila del sistema
     * para que al cambiar de contexto no se pierdan los datos. */
    int              saved_system_sp;
    Word             saved_system_stack[20]; /* Suficiente para 2 contextos anidados */
} PCB_t;

/* VARIABLES GLOBALES (definidas en process.c) */
extern PCB_t process_table[MAX_PROCESSES];
extern int   current_pid;           /* PID en CPU; -1 si ninguno   */
extern int   system_ticks;          /* Reloj global del sistema     */
extern int   partition_bitmap[NUM_PARTITIONS]; /* 0=libre, 1=ocupada */


/* PROTOTIPOS */
/* Inicializa la tabla de procesos y bitmaps */
void process_init(void);

/* Crea un proceso: lee archivo, lo guarda en DISK[], crea PCB en NEW.
 * Retorna PID asignado o -1 si error (tabla llena, sin disco, etc.). */
int  process_create(const char *filename, const char *name);

/* Cambia el estado de un proceso y registra el cambio en log.txt */
void process_change_state(int pid, ProcessState new_state);

/* Guarda el estado actual de la CPU en el PCB del proceso pid */
void process_save_context(int pid);

/* Recupera el contexto original del usuario desde la pila del sistema
 * (usado cuando una interrupción como SVC causa un cambio de contexto) */
void process_save_context_from_interrupt(int pid);

/* Restaura el contexto del PCB del proceso pid hacia la CPU global */
void process_load_context(int pid);

/* Busca una partición libre. Retorna índice (0-4) o -1 si no hay */
int  process_find_partition(void);

/* Libera la partición de un proceso */
void process_free_partition(int pid);

/* Carga las palabras del proceso desde DISK[] a su partición en RAM */
void process_load_to_ram(int pid);

/* Convierte estado a string (para log) */
const char *state_to_string(ProcessState s);

/* Gestión de la Cola de Listos (Ready Queue) de forma original */
void process_enqueue_ready(int pid);
int  process_dequeue_ready(void);

/* Imprime tabla de procesos en consola (comando 'ps') */
void process_print_table(void);

/* Lee un parámetro de la pila del usuario durante una interrupción.
 * Accede a RAM[base+sp] directamente y hace pop (incrementa SP).
 * Retorna 0 si éxito, -1 si error. */
int kernel_pop_stack(int pid, int *value);

#endif /* PROCESS_H */
