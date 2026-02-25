/*
 * scheduler.h — Interfaz del Planificador Round-Robin (Fase 2)
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

/*
 * scheduler_tick() — Llamar UNA VEZ por cada ciclo de ejecución.
 * Gestiona: admisión NEW→READY, despertar SLEEPING, quantum y dispatch.
 * Retorna:
 *   1  → hay un proceso RUNNING (se debe llamar a ejecutarInst())
 *   0  → no hay proceso para ejecutar (sistema idle)
 */
int  scheduler_tick(void);

/*
 * scheduler_handle_terminate() — Llamar cuando el proceso actual finaliza.
 * Cambia estado a TERMINATED, libera partición, pone current_pid = -1.
 */
void scheduler_handle_terminate(void);

/*
 * scheduler_handle_sleep(duration_ticks) — Llamar cuando el proceso duerme.
 * Cambia estado a SLEEPING y guarda contexto.
 */
void scheduler_handle_sleep(int duration_ticks);

#endif /* SCHEDULER_H */
