/*
 * scheduler.c — Planificador Round-Robin
 *
 * Implementa el planificador de procesos con algoritmo Round-Robin
 * de quantum = PROCESS_QUANTUM (2 ticks).
 *
 * Caracteristicas:
 *  - Admite procesos en estado NEW si hay partición libre.
 *  - Despierta procesos SLEEPING cuando su wake_tick <= system_ticks.
 *  - Rota entre procesos READY en orden circular.
 *  - Guarda y restaura contexto en cada cambio de proceso.
 *
 * No modifica architecture.c, dma.c, log.c.
 */

#include "scheduler.h"
#include "architecture.h"
#include "process.h"
#include "log.h"
#include "console_colors.h"
#include <stdio.h>
#include <string.h>

/* 
 * dispatch — Despacha un proceso: carga su contexto en la CPU
 */
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

    /* Imprimir dispatch SÓLO si es un proceso diferente o no había nada corriendo */
    if (old_pid != new_pid && !cpu_running) {
        printf(ANSI_FG_B_YELLOW "[SCHEDULER]" ANSI_RESET " Despachando PID=%.2d (%s)\n",
               new_pid, process_table[new_pid].name);
    }
}

/* 
 * scheduler_admit_new — Pasa procesos NEW a READY si hay RAM
 */
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


/* 
 * scheduler_next_ready — Busca el siguiente proceso READY
 * (búsqueda circular a partir de current_pid+1)
 */
static int scheduler_next_ready(void) {
    /* Aqui simplemente sacamos de la cola de listos */
    return process_dequeue_ready();
}

/* 
 * scheduler_tick — Llamar UNA VEZ por cada ciclo de CPU
 *
 * Retorna:
 *   1  → hay un proceso RUNNING (ejecutar ejecutarInst())
 *   0  → no hay nada que ejecutar (idle)
 */
int scheduler_tick(void) {
    /*
     *  Admite procesos NEW a READY cuando hay RAM libre.
     *  Despacha el siguiente proceso READY si ninguno corre.
     */

    /* Paso 1: Admitir procesos NEW si hay memoria disponible */
    scheduler_admit_new();

    /* Paso 2: Si ya hay un proceso RUNNING, no hacer nada más */
    if (current_pid != -1 &&
        process_table[current_pid].state == STATE_RUNNING) {
        return 1; /* Hay proceso running */
    }

    /* Paso 3: No hay nadie RUNNING → buscar siguiente READY */
    int next = scheduler_next_ready();
    if (next != -1) {
        dispatch(-1, next);
        return 1;
    }

    /* Paso 4: No hay procesos listos */
    return 0;
}

/* 
 * scheduler_handle_terminate — Llamar cuando el proceso actual termina
 */
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
    if (!cpu_running) {
        printf(ANSI_FG_B_YELLOW "[SCHEDULER]" ANSI_RESET " PID=%d (%s) terminó.\n",
                current_pid, process_table[current_pid].name);
    }

    current_pid = -1;
}

/* 
 * scheduler_handle_sleep — Llamar cuando el proceso pide dormir
 */
void scheduler_handle_sleep(int duration_ticks) {
    if (current_pid == -1) return;

    /* Recuperar los registros reales del usuario desde la pila
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
    if (!cpu_running) {
        printf(ANSI_FG_B_YELLOW "[SCHEDULER]" ANSI_RESET " PID=%d duerme %d ticks.\n", current_pid, duration_ticks);
    }

    current_pid = -1;
}

/* 
 * MANEJADOR DE INTERRUPCIONES DEL KERNEL
 * RUTINAS DE INTERRUPCIONES
 * 
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
 */
int kernel_interrupt_handler(int codigo, int operando) {
  char msg[256];

  switch (codigo) {

    /* Código 0: Llamada al sistema inválida */
    case INT_INVALID_SYSCALL:
      snprintf(msg, sizeof(msg),
               "KERNEL: Syscall inválida (operando=%d) en PID=%d. Ignorando.",
               operando, current_pid);
      escribir_log(msg);
      if (!cpu_running) {
          printf(ANSI_FG_B_MAGENTA "[KERNEL]" ANSI_RESET " Syscall inválida ignorada para PID=%d\n", current_pid);
      }
      return 0; /* No manejado: dejar que la ISR en RAM ejecute RETRN */

    /* Código 1: Código de interrupción inválido */
    case INT_INVALID_INT:
      snprintf(msg, sizeof(msg),
               "KERNEL: Código de interrupción inválido recibido. PID=%d.",
               current_pid);
      escribir_log(msg);
      if (!cpu_running) {
          printf(ANSI_FG_B_MAGENTA "[KERNEL]" ANSI_RESET " Interrupción inválida ignorada.\n");
      }
      return 0; /* No manejado: RETRN restaurará el contexto */

    /* Código 2: Llamada al sistema (SVC) */
    /* ABI de Syscalls:
     *   - El CÓDIGO de la syscall viene en el AC del usuario.
     *   - Los PARÁMETROS se leen del stack con kernel_pop_stack().
     *
     * Syscalls soportadas:
     *   1 = termina_prog(estado)   → Termina el proceso actual
     *   2 = imprime_pantalla(val)  → Imprime un valor en consola
     *   3 = leer_pantalla()        → Lee un entero del teclado → AC
     *   4 = dormir(tics)           → Duerme el proceso N tics
     */
    case INT_SYSCALL: {
      if (current_pid == -1) return 0;

      /* El código de la syscall viene en el AC.
       * Dentro de lanzarInterrupcion(), cpu.AC fue pusheado a la
       * pila del sistema pero NO fue borrado, así que aún tiene
       * el valor original del usuario. */
      int syscall_code = obtenerValorReal(cpu.AC);
      int param = 0;

      switch (syscall_code) {

      case 1: /* termina_prog(estado) */
          /*
           * Recupera el contexto del usuario de la pila del
           * sistema antes de llamar kernel_pop_stack. Cuando lanzarInterrupcion()
           * procesó el SVC, empujó PC/AC/RX/RB/RL/CC/Mode al system stack.
           * process_save_context_from_interrupt() los saca y los guarda en el
           * PCB. Esto asegura que kernel_pop_stack use el SP y la base del
           * usuario (ctx.sp, p->base) y no los de la CPU en modo kernel.
           */
          process_save_context_from_interrupt(current_pid);
          kernel_pop_stack(current_pid, &param);
          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall 1 (TERMINAR) PID=%d, estado=%d",
                   current_pid, param);
          escribir_log(msg);
          if (!cpu_running) {
              printf(ANSI_FG_B_MAGENTA "[KERNEL]" ANSI_RESET " PID=%d solicita terminar (estado=%d)\n",
                     current_pid, param);
          }
          scheduler_handle_terminate();
          return 1; /* proceso eliminado */

      case 2: /* imprime_pantalla(valor) */
          /*
           * Igual que syscall 1, recuperamos el contexto del
           * usuario desde la pila del sistema antes de leer el parámetro.
           * Además, retornamos 1 y restauramos el contexto
           * explícitamente, para que el proceso continúe exactamente donde
           * se quedó (con PC apuntando a la instrucción siguiente a SVC).
           */
          process_save_context_from_interrupt(current_pid);
          kernel_pop_stack(current_pid, &param);
          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall 2 (IMPRIMIR) PID=%d, valor=%d",
                   current_pid, param);
          escribir_log(msg);
          printf("\n" ANSI_FG_B_GREEN "[PANTALLA PID=%d]:" ANSI_RESET " %d\n", current_pid, param);
          /* Restaurar contexto del usuario y marcar como RUNNING */
          process_load_context(current_pid);
          process_change_state(current_pid, STATE_RUNNING);
          return 1; /* Manejado: nosotros restauramos, no ejecutar RETRN */

      case 3: { /* leer_pantalla() */
          /* Leemos un entero del teclado y lo dejamos en el AC
           * del usuario para que lo encuentre al volver. */
          printf("\n" ANSI_FG_B_GREEN "[ENTRADA PID=%d]:" ANSI_RESET " Ingrese un valor: ", current_pid);
          int input_val = 0;
          if (scanf("%d", &input_val) != 1) {
              input_val = 0;
              /* Limpiar buffer si el usuario escribió letras */
              while (getchar() != '\n');
          }

          /* Salvamos contexto de la pila del sistema al PCB, modificamos el AC en el PCB, y restauramos
           * el contexto de vuelta a la CPU. */
          process_save_context_from_interrupt(current_pid);
          process_table[current_pid].ctx.ac_sign  = (input_val < 0) ? 1 : 0;
          process_table[current_pid].ctx.ac_value = (input_val < 0) ? -input_val : input_val;
          process_load_context(current_pid);
          process_change_state(current_pid, STATE_RUNNING);

          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall 3 (LEER) PID=%d, valor leído=%d",
                   current_pid, input_val);
          escribir_log(msg);
          return 1; /* Manejado: ya restauramos contexto nosotros */
      }

      case 4: /* dormir(tics) */
          /* El programa puso la duración en el stack. */
          kernel_pop_stack(current_pid, &param);
          if (param <= 0) param = 1; /* Mínimo 1 tic */
          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall 4 (DORMIR %d tics) PID=%d",
                   param, current_pid);
          escribir_log(msg);
          scheduler_handle_sleep(param);
          return 1; /* Manejado: proceso dormido */

      default:
          /* Protección: syscall desconocida TERMINA el proceso.
           * Si no hacemos esto, RETRN restaura el contexto y el
           * proceso vuelve a ejecutar SVC → bucle infinito. */
          process_save_context_from_interrupt(current_pid);
          snprintf(msg, sizeof(msg),
                   "KERNEL: Syscall desconocida (código AC=%d) PID=%d. Terminando proceso.",
                   syscall_code, current_pid);
          escribir_log(msg);
          if (!cpu_running) {
              printf("[KERNEL] Syscall %d no reconocida para PID=%d. Terminando.\n",
                     syscall_code, current_pid);
          }
          scheduler_handle_terminate();
          return 1; /* Manejado: proceso terminado */
      }
    }

    case INT_CLOCK: {
      /* a) Avanzar el reloj global del sistema operativo */
      system_ticks++;
      snprintf(msg, sizeof(msg),
               "KERNEL: INT_CLOCK tick=%d, PID=%d.",
               system_ticks, current_pid);
      escribir_log(msg);

      /* b) Despertar procesos SLEEPING cuyo tiempo cumplio */
      for (int _i = 0; _i < MAX_PROCESSES; _i++) {
          if (process_table[_i].state == STATE_SLEEPING &&
              process_table[_i].wake_tick <= system_ticks) {
              process_change_state(_i, STATE_READY);
              char wlog[128];
              snprintf(wlog, sizeof(wlog),
                       "SCHEDULER: PID=%d despertado por INT_CLOCK en tick=%d.",
                       _i, system_ticks);
              escribir_log(wlog);
              if (!cpu_running) {
                  printf(ANSI_FG_B_YELLOW "[SCHEDULER]" ANSI_RESET
                         " PID=%d desperto (tick=%d).\n", _i, system_ticks);
              }
          }
      }

      /* c-d) Gestion del quantum si hay un proceso corriendo */
      if (current_pid != -1 &&
          process_table[current_pid].state == STATE_RUNNING) {

          process_table[current_pid].quantum_counter++;
          snprintf(msg, sizeof(msg),
                   "KERNEL: PID=%d quantum=%d/%d en tick=%d.",
                   current_pid,
                   process_table[current_pid].quantum_counter,
                   PROCESS_QUANTUM, system_ticks);
          escribir_log(msg);

          if (process_table[current_pid].quantum_counter >= PROCESS_QUANTUM) {
              int next = scheduler_next_ready();

              if (next != -1 && next != current_pid) {
                    /*
                   * lanzarInterrupcion() empujo 7 registros al system stack.
                   * process_save_context_from_interrupt() los extrae (sysPop x7)
                   * y los guarda en el PCB del proceso saliente.
                   * Asi el system stack queda vacio y consistente.
                   */
                  int saliente = current_pid;
                  process_save_context_from_interrupt(saliente);
                  process_change_state(saliente, STATE_READY);
                  process_table[saliente].quantum_counter = 0;

                  current_pid = next;
                  process_load_context(next);
                  process_table[next].quantum_counter = 0;
                  process_change_state(next, STATE_RUNNING);

                  char qlog[256];
                  snprintf(qlog, sizeof(qlog),
                           "[LOG] Quantum agotado. Proceso saliente: %d, Proceso entrante: %d",
                           saliente, next);
                  escribir_log(qlog);
                  if (!cpu_running) {
                      printf(ANSI_FG_B_YELLOW "[SCHEDULER]" ANSI_RESET
                             " Quantum agotado via INT_CLOCK."
                             " Sale PID=%d -> Entra PID=%d (tick=%d).\n",
                             saliente, next, system_ticks);
                  }

                  /* Stack vaciado por save_context_from_interrupt: no ejecutar RETRN */
                  return 1;

              } else {
                  /* Sin preempcion: solo un proceso disponible, sigue el mismo */
                  char qlog[256];
                  snprintf(qlog, sizeof(qlog),
                           "[LOG] Quantum agotado. Proceso saliente: %d, Proceso entrante: %d",
                           current_pid, current_pid);
                  escribir_log(qlog);
                  if (!cpu_running) {
                      printf(ANSI_FG_B_YELLOW "[SCHEDULER]" ANSI_RESET
                             " Quantum agotado. PID=%d sigue (tick=%d).\n",
                             current_pid, system_ticks);
                  }
                  process_table[current_pid].quantum_counter = 0;
              }
          }
      }

      /*
       * Sin preempcion: retornar 0 para que el hardware ejecute RETRN.
       * RETRN hace sysPop x7, vaciando lo que lanzarInterrupcion() empujo,
       * y restaura el proceso actual exactamente donde estaba.
       */
      return 0;
    }

    /* Código 4: Fin de Entrada/Salida (DMA completado) */
    case INT_IO_COMPLETE:
      snprintf(msg, sizeof(msg),
               "KERNEL: DMA completado. Transferencia E/S finalizada (PID=%d).",
               current_pid);
      escribir_log(msg);
      if (!cpu_running) {
          printf("[KERNEL] DMA completado para PID=%d\n", current_pid);
      }
      return 0; /* No manejado: RETRN restaurará contexto */

    /* Códigos 5,6: Errores fatales (instrucción/dirección) */
    case INT_INVALID_INST:
    case INT_INVALID_ADDR:
      if (current_pid == -1) return 0;
      snprintf(msg, sizeof(msg),
               "KERNEL: Error Fatal (Int %d) en PID=%d. Terminando proceso.",
               codigo, current_pid);
      escribir_log(msg);
      if (!cpu_running) {
          printf("\n[ERROR FATAL] PID=%d causa interrupción %d. Abortando.\n",
                 current_pid, codigo);
      }
      scheduler_handle_terminate();
      return 1; /* Manejado: proceso ya terminado */

    /* Códigos 7,8: Errores aritméticos (underflow/overflow) */
    case INT_UNDERFLOW:
    case INT_OVERFLOW:
      if (current_pid == -1) return 0;
      snprintf(msg, sizeof(msg),
               "KERNEL: Error aritmético (Int %d: %s) en PID=%d. Terminando.",
               codigo,
               (codigo == INT_OVERFLOW) ? "Overflow" : "Underflow",
               current_pid);
      escribir_log(msg);
      if (!cpu_running) {
          printf("\n[ERROR ARITMÉTICO] PID=%d: %s. Abortando.\n",
                 current_pid,
                 (codigo == INT_OVERFLOW) ? "Overflow" : "Underflow");
      }
      scheduler_handle_terminate();
      return 1; /* Manejado: proceso ya terminado */

    /* Default: interrupción desconocida */
    default:
      snprintf(msg, sizeof(msg),
               "KERNEL: Interrupción desconocida (código=%d). Ignorando.",
               codigo);
      escribir_log(msg);
      return 0; /* No manejado: flujo normal */
  }
}

