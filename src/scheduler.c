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
#include "process.h"
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
    snprintf(msg, sizeof(msg),
             "[LOG] Quantum agotado. Proceso saliente: %d, Proceso entrante: %d",
             old_pid, new_pid);
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
    if (current_pid == -1) {
        /* Primer proceso: buscar desde el inicio */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].state == STATE_READY)
                return i;
        }
        return -1;
    }

    /* Búsqueda circular: desde current_pid+1 dando la vuelta */
    for (int offset = 1; offset <= MAX_PROCESSES; offset++) {
        int i = (current_pid + offset) % MAX_PROCESSES;
        if (process_table[i].state == STATE_READY)
            return i;
    }
    return -1; /* No hay ninguno READY */
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
                /* Hay otro proceso listo: cambio de contexto. logging está en dispatch */
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

    process_save_context(current_pid);
    process_table[current_pid].wake_tick = system_ticks + duration_ticks;
    process_change_state(current_pid, STATE_SLEEPING);
    process_free_partition(current_pid); /* La partición se podría liberar o mantener */
    /* Opción conservadora: mantener la partición (no liberar hasta TERMINATED) */
    /* Si se quiere liberar: process_free_partition(current_pid) */
    /* Por simplicidad, la mantenemos para no tener que recargar desde disco */
    /* Re-marcar como ocupada (process_free_partition la limpió) */
    if (process_table[current_pid].partition_id == -1) {
        /* Ya fue liberada, re-asignar */
        int part = process_find_partition();
        if (part != -1) {
            partition_bitmap[part] = 1;
            process_table[current_pid].partition_id = part;
        }
    } else {
        /* No fue liberada (no llamamos process_free_partition en este camino) */
    }

    char msg[128];
    snprintf(msg, sizeof(msg),
             "SCHEDULER: PID=%d dormido por %d ticks (despierta en tick=%d).",
             current_pid, duration_ticks,
             process_table[current_pid].wake_tick);
    escribir_log(msg);
    printf("[SCHEDULER] PID=%d duerme %d ticks.\n", current_pid, duration_ticks);

    current_pid = -1;
}
