#include "architecture.h"
#include "log.h"
#include "process.h"    // Gestión de procesos
#include "scheduler.h"  // Planificador Round-Robin
#include "console_colors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // usleep()

/* Variables globales para el hilo de la CPU */
int cpu_running = 0;        // Flag: 1 = hilo CPU activo
pthread_t cpu_thread;       // Hilo de fondo de la CPU

// Función para mostrar comandos
void command_help() {
  printf(ANSI_FG_B_MAGENTA "\nCOMANDOS DISPONIBLES:\n\n" ANSI_RESET);
  printf(ANSI_FG_B_WHITE "  ejecutar " ANSI_FG_CYAN "<p1> [p2]... " ANSI_RESET ": Cargar y ejecutar programas en Round-Robin\n");
  printf(ANSI_FG_B_WHITE "  ps                   " ANSI_RESET ": Ver tabla de procesos\n");
  printf(ANSI_FG_B_WHITE "  memestat             " ANSI_RESET ": Ver mapa y porcentaje de uso de memoria\n");
  printf(ANSI_FG_B_WHITE "  reg                  " ANSI_RESET ": Ver registros del CPU\n");
  printf(ANSI_FG_B_WHITE "  reiniciar            " ANSI_RESET ": Reiniciar CPU, Memoria y Procesos\n");
  printf(ANSI_FG_B_WHITE "  apagar               " ANSI_RESET ": Salir del simulador\n\n");
}

// Función para cargar programa desde archivo
// Ahora crea un proceso (PCB) en la tabla de procesos
void command_load(const char *filename) {
  // Crea proceso via subsistema de procesos
  //Extraer nombre base del archivo para usarlo como nombre del proceso
  char procname[64];
  const char *slash = filename;
  for (const char *p = filename; *p; p++)
    if (*p == '/' || *p == '\\') slash = p + 1;
  strncpy(procname, slash, 63);
  procname[63] = '\0';
  /* Quitar extension .txt si existe */
  char *dot = strrchr(procname, '.');
  if (dot) *dot = '\0';

  int pid = process_create(filename, procname);
  if (pid == -1) {
    printf("Error: No se pudo crear el proceso para '%s'.\n", filename);
    return;
  }

  /* Contar procesos activos para dar un mensaje adecuado */
  int active_cnt = 0;
  for (int i = 0; i < MAX_PROCESSES; i++) {
      if (process_table[i].pid != -1 && process_table[i].state != STATE_TERMINATED) {
          active_cnt++;
      }
  }

  PCB_t *p = &process_table[pid];
  printf("Proceso '%s' creado exitosamente:\n", p->name);
  printf("  PID             : %d\n", p->pid);
  printf("  Partición RAM   : %d (RAM[%d..%d])\n", p->partition_id, p->base, p->limit);
  printf("  Tamaño Código   : %d palabras\n", p->prog_size);
  printf("  Punto Entrada   : %d (lógico)\n", p->entry_point);
  printf("  Protección      : RB=%d, RL=%d\n", p->base, p->limit);

  if (active_cnt > 1) {
      printf("\nSistema con %d procesos activos. Multiprogramación lista.\n", active_cnt);
  } else {
      printf("\nProceso listo para ejecución sencilla.\n");
  }
}

void command_reg() {
  printf("=== Registros del CPU ===\n");
  int ac_val = cpu.AC.value;
  if (cpu.AC.sign)
    ac_val = -ac_val;

  printf("  AC : %d\n", ac_val);
  printf("  PC : %d\n", cpu.PSW.pc);
  printf("  MAR: %d\n", cpu.MAR.address);
  printf("  MDR: %d (Signo: %d, Valor: %d)\n", obtenerValorReal(cpu.MDR.data),
        cpu.MDR.data.sign, cpu.MDR.data.value);
  printf("  SP : %d\n", cpu.stack.sp);
  printf("  IR : OPC=%d, Addr=%d, Op=%d\n", cpu.IR.opcode, cpu.IR.addressing,
        cpu.IR.operand);
  printf("  PSW: Cond=%d, Mode=%d, Int=%d\n", cpu.PSW.condition, cpu.PSW.mode,
        cpu.PSW.interrupt);
}



/*
 * Hilo de fondo de la CPU 
 * Ejecuta el loop del scheduler + instrucciones con usleep(250000)
 * entre cada instruccion para que la shell siga disponible.
 */
void *cpu_loop_thread(void *arg) {
    (void)arg;
    int idle_count = 0;

    while (cpu_running) {
        /* El scheduler decide qué proceso corre */
        int hay_running = scheduler_tick();

        if (!hay_running) {
            /* Verificar si quedan procesos activos */
            int quedan = 0;
            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (process_table[i].pid != -1 &&
                    (process_table[i].state == STATE_READY ||
                     process_table[i].state == STATE_RUNNING ||
                     process_table[i].state == STATE_SLEEPING ||
                     process_table[i].state == STATE_NEW)) {
                    quedan = 1;
                    break;
                }
            }
            if (!quedan) {
                printf("\n" ANSI_FG_B_GREEN ">> Todos los procesos terminaron." ANSI_RESET "\n");
                printf(ANSI_FG_B_MAGENTA "OS" ANSI_FG_WHITE "> " ANSI_RESET);
                fflush(stdout);
                cpu_running = 0;
                break;
            }
            /* Hay procesos SLEEPING: esperar */
            idle_count++;
            if (idle_count > 1000) {
                printf("\n>> Sistema idle por mucho tiempo. Deteniendo CPU.\n");
                printf(ANSI_FG_B_MAGENTA "OS" ANSI_FG_WHITE "> " ANSI_RESET);
                fflush(stdout);
                cpu_running = 0;
                break;
            }
            usleep(250000); /* 250ms idle wait */
            continue;
        }
        idle_count = 0;

        /* Hay un proceso RUNNING: ejecutar UNA instrucción */
        if (cpu.halted) {
            char hmsg[128];
            snprintf(hmsg, sizeof(hmsg), "CPU detenida. PID=%d terminó por HALT.", current_pid);
            escribir_log(hmsg);
            scheduler_handle_terminate();
            usleep(250000);
            continue;
        }

        /* Verificar fin de programa */
        if (cpu.PSW.mode == MODE_USER &&
            current_pid != -1 &&
            cpu.PSW.pc >= process_table[current_pid].prog_size) {
            char fmsg[128];
            snprintf(fmsg, sizeof(fmsg), "PID=%d (%s): fin de programa (PC=%d).",
                     current_pid, process_table[current_pid].name, cpu.PSW.pc);
            escribir_log(fmsg);
            scheduler_handle_terminate();
            usleep(250000);
            continue;
        }

        ejecutarInst();

        /* Protección anti-loop: contar instrucciones por proceso */
        if (current_pid != -1) {
            process_table[current_pid].inst_count++;
            if (process_table[current_pid].inst_count >= MAX_INST_PER_PROCESS) {
                char lmsg[128];
                snprintf(lmsg, sizeof(lmsg),
                         "KERNEL: PID=%d excedió límite de %d instrucciones. Terminando.",
                         current_pid, MAX_INST_PER_PROCESS);
                escribir_log(lmsg);
                scheduler_handle_terminate();
            }
        }

        usleep(250000); /* 250ms por instrucción */
    }

    return NULL;
}

void command_run() {
    /* Verificar si el hilo CPU ya está corriendo */
    if (cpu_running) {
        printf("La CPU ya está ejecutando procesos en segundo plano.\n");
        printf("Use '" ANSI_FG_B_YELLOW "ps" ANSI_RESET "' para ver el estado de los procesos.\n");
        return;
    }

    /* Verificar si hay algún proceso activo */
    int hay_procesos = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid != -1 &&
            process_table[i].state != STATE_TERMINATED) {
            hay_procesos = 1;
            break;
        }
    }

    if (!hay_procesos) {
        printf("No hay procesos cargados. Use '" ANSI_FG_B_YELLOW "load" ANSI_RESET "' primero.\n");
        return;
    }

    /* Lanzar hilo CPU en segundo plano */
    printf("Ejecutando con planificador Round-Robin (quantum=%d)...\n", PROCESS_QUANTUM);
    printf("La shell sigue disponible. Use '" ANSI_FG_B_YELLOW "ps" ANSI_RESET "' y '" ANSI_FG_B_YELLOW "memestat" ANSI_RESET "' para monitorear.\n");
    cpu_running = 1;
    pthread_create(&cpu_thread, NULL, cpu_loop_thread, NULL);
}

/* Comando ps */
void command_ps(void) {
  process_print_table();
}

/* Comando memestat */
void command_memestat(void) {
    printf("\n=== Mapa de Memoria (RAM_SIZE=%d) ===\n", RAM_SIZE);
    printf("  [0 - %d] : Sistema Operativo (%d palabras ocupadas)\n", OS_RESERVED - 1, OS_RESERVED);
    int particiones_usadas = 0;
    for (int i = 0; i < NUM_PARTITIONS; i++) {
        int base = OS_RESERVED + (i * PARTITION_SIZE);
        int limit = base + PARTITION_SIZE - 1;
        if (partition_bitmap[i] == 1) {
            printf("  [%d - %d] : Partición %d - " ANSI_FG_B_RED "OCUPADA" ANSI_RESET "\n", base, limit, i);
            particiones_usadas++;
        } else {
            printf("  [%d - %d] : Partición %d - " ANSI_FG_B_GREEN "LIBRE" ANSI_RESET "\n", base, limit, i);
        }
    }
    int total_usado = OS_RESERVED + (particiones_usadas * PARTITION_SIZE);
    float porcentaje = ((float)total_usado / RAM_SIZE) * 100.0f;
    printf("----------------------------------------\n");
    printf("Uso total: %.2f%% (%d/%d palabras)\n\n", porcentaje, total_usado, RAM_SIZE);
}

int main() {
  char input[10000];
  char *cmd;
  char *arg1;

  abrir_log();
  inicializarCPU();
  process_init(); /* Inicializar tabla de procesos */
  inicializarDMAThread();

  CLEAR_SCREEN();
  printf(ANSI_FG_B_MAGENTA);
  printf("  __  __ _       _   _  __                    _ \n");
  printf(" |  \\/  (_)     (_) | |/ /                   | |\n");
  printf(" | \\  / |_ _ __  _  | ' / ___ _ __ _ __   ___| |\n");
  printf(" | |\\/| | | '_ \\| | |  < / _ \\ '__| '_ \\ / _ \\ |\n");
  printf(" | |  | | | | | | | | . \\  __/ |  | | | |  __/ |\n");
  printf(" |_|  |_|_|_| |_|_| |_|\\_\\___|_|  |_| |_|\\___|_|\n");
  printf(ANSI_RESET "\n");

  printf(ANSI_FG_B_WHITE "¡Bienvenido al Mini Kernel de SO! \n" ANSI_RESET);
  printf("Escriba '" ANSI_FG_B_YELLOW "help" ANSI_RESET "' para ver los comandos disponibles.\n\n");

  while (1) {
    printf(ANSI_FG_B_MAGENTA "OS" ANSI_FG_WHITE "> " ANSI_RESET);
    if (fgets(input, sizeof(input), stdin) == NULL)
      break;

    input[strcspn(input, "\n")] = 0;

    cmd = strtok(input, " ");
    if (cmd == NULL)
      continue;

    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "apagar") == 0) {
      /* Detener hilo CPU si está corriendo */
      if (cpu_running) {
        cpu_running = 0;
        pthread_join(cpu_thread, NULL);
        printf("CPU detenida.\n");
      }
      break;
    } else if (strcmp(cmd, "help") == 0) {
      command_help();
    } else if (strcmp(cmd, "ejecutar") == 0) {
      /* cargar + ejecutar en un solo paso*/
      arg1 = strtok(NULL, " ");
      if (!arg1) {
        printf("Uso: ejecutar <prog1.txt> [prog2.txt] ...\n");
      } else {
        int loaded_any = 0;
        while (arg1 != NULL) {
          command_load(arg1);
          loaded_any = 1;
          arg1 = strtok(NULL, " ");
        }
        if (loaded_any) {
          command_run();
        }
      }
    } else if (strcmp(cmd, "memestat") == 0) {
      command_memestat();
    } else if (strcmp(cmd, "reg") == 0) {
      command_reg();
    } else if (strcmp(cmd, "ps") == 0) {
      command_ps();
    } else if (strcmp(cmd, "reset") == 0 || strcmp(cmd, "reiniciar") == 0) {
      /* Detener hilo CPU si está corriendo */
      if (cpu_running) {
        cpu_running = 0;
        pthread_join(cpu_thread, NULL);
      }
      inicializarCPU();
      process_init(); /*limpiar procesos al reiniciar */
      printf("Sistema reiniciado (CPU, Pila SO y Procesos).\n");
    } else {
      printf("Comando desconocido: %s\n", cmd);
    }
  }

  cerrar_log();
  printf("Simulador terminado.\n");
  return 0;
}
