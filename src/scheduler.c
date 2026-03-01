/*
 * scheduler.c — Planificador Round-Robin (Fase 2)
 *
 * Implementa el planificador de procesos con algoritmo Round-Robin
 * de quantum = PROCESS_QUANTUM (2 ticks).
 *
 * Reglas:
 *  - Admite procesos en estado NEW si hay partición libre.
 *  - Despierta procesos SLEEPING cuando su wake_tick <= system_ticks.
 *  - Rota entre procesos READY en orden circular.
 *  - Guarda y restaura contexto en cada cambio de proceso.
 *
 * NO modifica architecture.c, dma.c, log.c, cpu.c.
 */

#include "scheduler.h"
#include "proceso.h"
#include "log.h"
#include <stdio.h>

/* ============================================================
 * dispatch — Despacha un proceso: carga su contexto en la CPU
 * ============================================================ */
static void dispatch(int old_pid, int new_pid) {
    /* 1. Si había un proceso corriendo, guardar su contexto */
    if (old_pid != -1 &&
        process_table[old_pid].state == STATE_RUNNING) {
        process_save_context(old_pid);
        process_change_state(old_pid, STATE_READY);
        process_table[old_pid].quantum_counter = 0;
    }

    /* 2. Cargar contexto del nuevo proceso */
    current_pid = new_pid;
    process_load_context(new_pid);
    process_table[new_pid].quantum_counter = 0;
    process_change_state(new_pid, STATE_RUNNING);

    char msg[256];
    if (old_pid == -1) {
        snprintf(msg, sizeof(msg),
                 "[LOG] Cambio de contexto. Proceso saliente: NINGUNO, Proceso entrante: %d",
                 new_pid);
    } else {
        snprintf(msg, sizeof(msg),
                 "[LOG] Quantum agotado. Proceso saliente: %d, Proceso entrante: %d",
                 old_pid, new_pid);
    }
    escribir_log(msg);

    printf("[SCHEDULER] Despachando PID=%d (%s)\n",
           new_pid, process_table[new_pid].name);
}

/* ============================================================
 * scheduler_admit_new — Pasa procesos NEW a READY si hay RAM
 * ============================================================ */
static void scheduler_admit_new(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != STATE_NEW) continue;

        int part = process_find_partition();
        if (part == -1) break; /* Sin RAM disponible */

        /* Asignar partición */
        partition_bitmap[part] = 1;
        process_table[i].partition_id = part;
        process_table[i].base  = OS_RESERVED + (part * PARTITION_SIZE);
        process_table[i].limit = process_table[i].base + PARTITION_SIZE - 1;

        /* Cargar de disco a RAM */
        process_load_to_ram(i);

        /* Contexto inicial (entry point ya fue guardado en process_create) */
        process_table[i].ctx.rb = process_table[i].base;
        process_table[i].ctx.rl = process_table[i].limit;
        process_table[i].ctx.sp = PARTITION_SIZE - 1;

        process_change_state(i, STATE_READY);
    }
}

/* ============================================================
 * scheduler_wake_sleeping — Despierta procesos cuyo tiempo expiró
 * ============================================================ */
static void scheduler_wake_sleeping(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != STATE_SLEEPING) continue;
        if (process_table[i].wake_tick <= system_ticks) {
            process_change_state(i, STATE_READY);
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "SCHEDULER: PID=%d despertado en tick=%d.",
                     i, system_ticks);
            escribir_log(msg);
        }
    }
}

/* ============================================================
 * scheduler_next_ready — Busca el siguiente proceso READY
 * (búsqueda circular a partir de current_pid+1)
 * ============================================================ */
static int scheduler_next_ready(void) {
    /* Nueva lógica: simplemente sacamos de la cola de listos */
    return process_dequeue_ready();
}

/* ============================================================
 * scheduler_tick — Llamar UNA VEZ por cada ciclo de CPU
 *
 * Retorna:
 *   1  → hay un proceso RUNNING (ejecutar ejecutarInst())
 *   0  → no hay nada que ejecutar (idle)
 * ============================================================ */
int scheduler_tick(void) {
    system_ticks++;

    /* Paso 1: Admitir procesos NEW si hay memoria */
    scheduler_admit_new();

    /* Paso 2: Despertar procesos SLEEPING que ya cumplieron su tiempo */
    scheduler_wake_sleeping();

    /* Paso 3: Verificar el proceso actual */
    if (current_pid != -1 &&
        process_table[current_pid].state == STATE_RUNNING) {

        process_table[current_pid].quantum_counter++;

        /* ¿Se agotó el quantum? → Preempción */
        if (process_table[current_pid].quantum_counter >= PROCESS_QUANTUM) {
            int next = scheduler_next_ready();
            if (next != -1) {
                /* Hay otro proceso listo: cambio de contexto. 
                 * NOTA: dispatch se encarga de cambiar el actual a READY 
                 * y process_change_state lo meterá al final de la cola. */
                dispatch(current_pid, next);
            } else {
                /* No hay otro: el mismo sigue (reset del contador) */
                char log_msg[256];
                snprintf(log_msg, sizeof(log_msg),
                        "[LOG] Quantum agotado. Proceso saliente: %d, Proceso entrante: %d",
                        current_pid, current_pid);
                escribir_log(log_msg);
                process_table[current_pid].quantum_counter = 0;
            }
        }

        return 1; /* Hay proceso running */
    }

    /* Paso 4: No hay nadie RUNNING → buscar siguiente READY */
    int next = scheduler_next_ready();
    if (next != -1) {
        dispatch(-1, next);
        return 1;
    }

    /* Paso 5: No hay procesos listos */
    return 0;
}

/* ============================================================
 * scheduler_handle_terminate — Llamar cuando el proceso actual termina
 * ============================================================ */
void scheduler_handle_terminate(void) {
    if (current_pid == -1) return;

    process_save_context(current_pid); /* Guardar por última vez */
    process_change_state(current_pid, STATE_TERMINATED);
    process_free_partition(current_pid);

    char msg[128];
    snprintf(msg, sizeof(msg),
            "SCHEDULER: PID=%d (%s) TERMINADO. Partición liberada.",
            current_pid, process_table[current_pid].name);
    escribir_log(msg);
    printf("[SCHEDULER] PID=%d (%s) terminó.\n",
            current_pid, process_table[current_pid].name);

    current_pid = -1;
}

/* ============================================================
 * scheduler_handle_sleep — Llamar cuando el proceso pide dormir
 * ============================================================ */
void scheduler_handle_sleep(int duration_ticks) {
    if (current_pid == -1) return;

    /* CLAVE: Recuperar los registros REALES del usuario desde la pila
     * del sistema, NO los registros actuales (que ya son de Kernel). */
    process_save_context_from_interrupt(current_pid);
    process_table[current_pid].wake_tick = system_ticks + duration_ticks;
    process_change_state(current_pid, STATE_SLEEPING);
    /* Un proceso SLEEPING retiene su partición RAM.
     * Solo libera en TERMINATED. */

    char msg[128];
    snprintf(msg, sizeof(msg),
            "SCHEDULER: PID=%d dormido por %d ticks (despierta en tick=%d).",
            current_pid, duration_ticks,
            process_table[current_pid].wake_tick);
    escribir_log(msg);
    printf("[SCHEDULER] PID=%d duerme %d ticks.\n", current_pid, duration_ticks);

    current_pid = -1;
}

/* ============================================================
 * MANEJADOR DE INTERRUPCIONES DEL KERNEL (Hook desde C)
 * RUTINAS DE INTERRUPCIONES
 * ============================================================
 * Esta función es llamada por el hardware (architecture.c) cada
 * vez que ocurre una interrupción, DESPUÉS de salvar el contexto
 * en la pila del sistema.
 *
 * Implementa la RUTINA MANEJADORA de cada tipo de interrupción.
 * Retorna 1 si el kernel manejó la interrupción (no ejecutar RETRN).
 * Retorna 0 si no la manejó (seguir flujo normal con ISR de RAM).
 *
 * Política de anidamiento: NO se permiten interrupciones anidadas.
 * Las interrupciones se deshabilitan al entrar (lanzarInterrupcion)
 * y se rehabilitan al restaurar contexto (RETRN).
 *
 * Prioridad (por código, menor = mayor prioridad):
 *   Errores fatales (5,6) > Aritméticos (7,8) > Syscall (2) >
 *   Reloj (3) > E/S (4) > Códigos inválidos (0,1)
 * ============================================================ */
int kernel_interrupt_handler(int codigo, int operando) {
  char msg[256];

  switch (codigo) {

    /* ── Código 0: Llamada al sistema inválida ──────────────── */
    case INT_INVALID_SYSCALL:
      snprintf(msg, sizeof(msg),
               "KERNEL: Syscall inválida (operando=%d) en PID=%d. Ignorando.",
               operando, current_pid);
      escribir_log(msg);
      printf("[KERNEL] Syscall inválida ignorada para PID=%d\n", current_pid);
      return 0; /* No manejado: dejar que la ISR en RAM ejecute RETRN */

    /* ── Código 1: Código de interrupción inválido ─────────── */
    case INT_INVALID_INT:
      snprintf(msg, sizeof(msg),
               "KERNEL: Código de interrupción inválido recibido. PID=%d.",
               current_pid);
      escribir_log(msg);
      printf("[KERNEL] Interrupción inválida ignorada.\n");
      return 0; /* No manejado: RETRN restaurará el contexto */

    /* ── Código 2: Llamada al sistema (SVC/INT) ────────────────── */
    case INT_SYSCALL:
      if (current_pid == -1) return 0;
      
      int syscall_code = obtenerValorReal(cpu.AC);
      int phys_sp = cpu.mp.base + cpu.stack.sp;
      
      if (syscall_code == 1) { /* 1: termina_prog(estado) */
          int estado = obtenerValorReal(RAM[phys_sp]);
          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall 1 (TERMINAR) de PID=%d con estado=%d",
                   current_pid, estado);
          escribir_log(msg);
          scheduler_handle_terminate();
          return 1; /* Manejado: proceso terminado */
      }
      else if (syscall_code == 2) { /* 2: imprime_pantalla(valor) */
          int valor = obtenerValorReal(RAM[phys_sp]);
          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall 2 (IMPRIMIR) de PID=%d. Valor=%d",
                   current_pid, valor);
          escribir_log(msg);
          printf("\n[PANTALLA PID=%d]: %d\n", current_pid, valor);
          return 0; /* No manejado: dejar flujo normal para ejecutar RETRN */
      }
      else if (syscall_code == 3) { /* 3: leer_pantalla() */
          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall 3 (LEER) de PID=%d.",
                   current_pid);
          escribir_log(msg);
          int valor;
          printf("\n[ENTRADA PID=%d]: Ingrese valor numérico: ", current_pid);
          if (scanf("%d", &valor) != 1) valor = 0;
          
          /* Escribir el valor en el AC guardado en la pila del sistema.
           * Orden system stack (crece hacia abajo):
           * +0: Mode, +1: CC, +2: RL, +3: RB, +4: RX, +5: AC, +6: PC */
          RAM[cpu.system_sp + 5].sign = (valor < 0) ? 1 : 0;
          RAM[cpu.system_sp + 5].value = (valor < 0) ? -valor : valor;
          
          return 0; /* Ejecutar RETRN restaurará este nuevo AC */
      }
      else if (syscall_code == 4) { /* 4: dormir(tics) */
          int duracion = obtenerValorReal(RAM[phys_sp]);
          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall 4 (SLEEP: %d ticks) de PID=%d",
                   duracion, current_pid);
          escribir_log(msg);
          scheduler_handle_sleep(duracion);
          return 1; /* Manejado: no ejecutar RETRN */
      }

      snprintf(msg, sizeof(msg),
               "KERNEL: Syscall inválida (código AC=%d) para PID=%d",
               syscall_code, current_pid);
      escribir_log(msg);
      return 0; /* No manejado: dejar flujo normal */

    /* ── Código 3: Interrupción de reloj ───────────────────── */
    case INT_CLOCK:
      snprintf(msg, sizeof(msg),
               "KERNEL: Tick de reloj (system_ticks=%d).", system_ticks);
      escribir_log(msg);
      return 0; /* No manejado: RETRN restaurará contexto */

    /* ── Código 4: Fin de Entrada/Salida (DMA completado) ─── */
    case INT_IO_COMPLETE:
      snprintf(msg, sizeof(msg),
               "KERNEL: DMA completado. Transferencia E/S finalizada (PID=%d).",
               current_pid);
      escribir_log(msg);
      printf("[KERNEL] DMA completado para PID=%d\n", current_pid);
      return 0; /* No manejado: RETRN restaurará contexto */

    /* ── Códigos 5,6: Errores fatales (instrucción/dirección) ── */
    case INT_INVALID_INST:
    case INT_INVALID_ADDR:
      if (current_pid == -1) return 0;
      snprintf(msg, sizeof(msg),
               "KERNEL: Error Fatal (Int %d) en PID=%d. Terminando proceso.",
               codigo, current_pid);
      escribir_log(msg);
      printf("\n[ERROR FATAL] PID=%d causa interrupción %d. Abortando.\n",
             current_pid, codigo);
      scheduler_handle_terminate();
      return 1; /* Manejado: proceso ya terminado */

    /* ── Códigos 7,8: Errores aritméticos (underflow/overflow) ── */
    case INT_UNDERFLOW:
    case INT_OVERFLOW:
      if (current_pid == -1) return 0;
      snprintf(msg, sizeof(msg),
               "KERNEL: Error aritmético (Int %d: %s) en PID=%d. Terminando.",
               codigo,
               (codigo == INT_OVERFLOW) ? "Overflow" : "Underflow",
               current_pid);
      escribir_log(msg);
      printf("\n[ERROR ARITMÉTICO] PID=%d: %s. Abortando.\n",
             current_pid,
             (codigo == INT_OVERFLOW) ? "Overflow" : "Underflow");
      scheduler_handle_terminate();
      return 1; /* Manejado: proceso ya terminado */

    /* ── Default: interrupción desconocida ──────────────────── */
    default:
      snprintf(msg, sizeof(msg),
               "KERNEL: Interrupción desconocida (código=%d). Ignorando.",
               codigo);
      escribir_log(msg);
      return 0; /* No manejado: flujo normal */
  }
}

