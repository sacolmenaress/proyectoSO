#include "architecture.h"
#include "log.h"
#include "process.h"    /* Fase 2: gestión de procesos */
#include "scheduler.h"  /* Fase 2: planificador Round-Robin */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Función para mostrar comandos
void command_help() {
  printf("Comandos disponibles:\n");
  printf("  load <archivo>       : Cargar un programa (crea proceso)\n");
  printf("  ps                   : Ver tabla de procesos\n");
  printf("  mem <inicio> <cant>  : Ver contenido de memoria\n");
  printf("  reg                  : Ver registros del CPU\n");
  printf("  run                  : Ejecutar con planificador Round-Robin\n");
  printf("  step                 : Ejecutar una instruccion (paso a paso)\n");
  printf("  reset                : Reiniciar CPU y Memoria\n");
  printf("  setvec <cod> <dir>   : Configurar vector de interrupciones\n");
  printf("  exit                 : Salir del simulador\n");
}

// Función para cargar programa desde archivo
// Fase 2: ahora crea un proceso (PCB) en la tabla de procesos
void command_load(const char *filename) {
  /* --- Fase 2: Crear proceso via subsistema de procesos --- */
  /* Extraer nombre base del archivo para usarlo como nombre del proceso */
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

void command_mem(int start, int count) {
  if (start < 0 || start >= RAM_SIZE) {
    printf("Error: Direccion de inicio invalida.\n");
    return;
  }

  printf("Memoria [%d - %d]:\n", start, start + count - 1);
  for (int i = 0; i < count && (start + i) < RAM_SIZE; i++) {
    int idx = start + i;
    int val = RAM[idx].value;
    if (RAM[idx].sign == 1)
      val = -val;
    printf("  [%04d]: %d (Signo: %d, Valor: %d)\n", idx, val, RAM[idx].sign,
          RAM[idx].value);
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

void command_step() {
  /* --- Fase 2: si hay procesos activos, usar el scheduler --- */
  int hay_procesos = 0;
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (process_table[i].pid != -1 &&
        process_table[i].state != STATE_TERMINATED) {
      hay_procesos = 1;
      break;
    }
  }

  if (hay_procesos) {
    /* Modo scheduler: el tick decide quién corre */
    int hay_running = scheduler_tick();
    if (!hay_running) {
      printf("[STEP] Sistema idle: no hay proceso listo para ejecutar.\n");
      return;
    }
    /* Verificar si el proceso actual terminó */
    if (cpu.halted) {
      printf("[STEP] PID=%d terminó por HALT.\n", current_pid);
      scheduler_handle_terminate();
      return;
    }
    if (cpu.PSW.mode == MODE_USER && current_pid != -1 &&
        cpu.PSW.pc >= process_table[current_pid].prog_size) {
      printf("[STEP] PID=%d fin de programa (PC=%d).\n",
             current_pid, cpu.PSW.pc);
      scheduler_handle_terminate();
      return;
    }
  } else {
    /* Modo Fase 1 (sin procesos): chequeos directos */
    if (cpu.PSW.pc >= RAM_SIZE) {
      printf("PC fuera de rango.\n");
      return;
    }
    if (cpu.halted) {
      printf("CPU detenida.\n");
      return;
    }
    if (cpu.PSW.mode == MODE_USER && cpu.PSW.pc >= cpu.stack.rx) {
      printf("Fin del programa alcanzado (PC=%d).\n", cpu.PSW.pc);
      return;
    }
  }

  // Decodificar y mostrar instrucción ANTES de ejecutar
  printf("\n==== Modo Debugger (tick=%d, PID=%d) ====\n",
         system_ticks, current_pid);
  printf("Dirección física: %d\n", cpu.mp.base + cpu.PSW.pc);

  int dir_fisica = (cpu.PSW.mode == MODE_USER)
                   ? cpu.mp.base + cpu.PSW.pc
                   : cpu.PSW.pc;
  Word inst = RAM[dir_fisica];
  int opcode     = (inst.sign * 10) + (inst.value / 1000000);
  int addressing = (inst.value / 100000) % 10;
  int operand    = inst.value % 100000;

  printf("Instrucción: Opcode=%02d, Modo=%d, Operando=%05d\n",
         opcode, addressing, operand);

  // Guardar estado anterior
  Word AC_antes = cpu.AC;
  int  PC_antes = cpu.PSW.pc;

  // EJECUTAR
  printf("Ejecutando...\n");
  ejecutarInst();

  // DESPUÉS: mostrar resultado
  printf("Resultado: AC=%d (antes=%d), PC=%d (antes=%d)\n",
        obtenerValorReal(cpu.AC), obtenerValorReal(AC_antes),
        cpu.PSW.pc, PC_antes);

  // Sub-menú interactivo
  printf("\n[r] Registros  [m <dir>] Memoria  [Enter] Continuar: ");
  char input[50];
  if (fgets(input, sizeof(input), stdin)) {
    input[strcspn(input, "\n")] = 0;
    if (input[0] == 'r') {
      command_reg();
    } else if (input[0] == 'm') {
      int addr = 0;
      if (sscanf(input, "m %d", &addr) == 1)
        command_mem(addr, 5);
      else
        printf("Uso: m <direccion>\n");
    }
  }
}


void command_run() {
  /* === Fase 2: Ejecutar con planificador Round-Robin ===
   * Si hay procesos en la tabla, usamos el scheduler.
   * Si no hay ninguno, caemos al modo Fase 1 (compatibilidad). */

  /* Verificar si hay algún proceso activo (NEW, READY, RUNNING, SLEEPING) */
  int hay_procesos = 0;
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (process_table[i].pid != -1 &&
        process_table[i].state != STATE_TERMINATED) {
      hay_procesos = 1;
      break;
    }
  }

  if (hay_procesos) {
    /* --- Modo Fase 2: planificador Round-Robin --- */
    printf("Ejecutando con planificador Round-Robin (quantum=%d)...\n",
           PROCESS_QUANTUM);
    int max_cycles = 100000; /* límite de seguridad */
    int cycles = 0;
    int idle_count = 0;

    while (cycles < max_cycles) {
      /* El scheduler decide qué proceso corre (o si hay idle) */
      int hay_running = scheduler_tick();

      if (!hay_running) {
        /* No hay proceso listo: verificar si quedan procesos activos */
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
          printf(">> Todos los procesos terminaron.\n");
          break;
        }
        /* Hay procesos SLEEPING: esperar (contar idle para evitar busy-wait infinito) */
        idle_count++;
        if (idle_count > 1000) {
          printf(">> Sistema idle por mucho tiempo. Abortando.\n");
          break;
        }
        cycles++;
        continue;
      }
      idle_count = 0;

      /* Hay un proceso RUNNING: ejecutar UNA instrucción */
      if (cpu.halted) {
        printf(">> CPU detenida. PID=%d terminó por HALT.\n", current_pid);
        scheduler_handle_terminate();
        cycles++;
        continue;
      }

      /* Verificar fin de programa: PC fuera del rango cargado */
      if (cpu.PSW.mode == MODE_USER &&
          current_pid != -1 &&
          cpu.PSW.pc >= process_table[current_pid].prog_size) {
        printf(">> PID=%d (%s): fin de programa (PC=%d).\n",
               current_pid,
               process_table[current_pid].name,
               cpu.PSW.pc);
        scheduler_handle_terminate();
        cycles++;
        continue;
      }

      ejecutarInst();
      cycles++;
    }

    if (cycles >= max_cycles)
      printf("Ejecucion detenida: limite de ciclos (%d) alcanzado.\n", max_cycles);

  } else {
    /* --- Modo Fase 1 (compatibilidad): sin procesos en tabla --- */
    printf("Ejecutando programa (modo directo)...\n");
    int max_cycles = 1000;
    int cycles = 0;
    while (cycles < max_cycles) {
      if (cpu.halted) {
        printf("Ejecucion detenida por instruccion Halt o error.\n");
        break;
      }
      if (cpu.PSW.mode == MODE_USER && cpu.PSW.pc >= cpu.stack.rx) {
        printf("Fin del programa alcanzado (PC=%d).\n", cpu.PSW.pc);
        break;
      }
      ejecutarInst();
      cycles++;
    }
    if (cycles >= max_cycles) {
      printf("Ejecucion detenida: Limite de ciclos alcanzado (%d).\n", max_cycles);
    }
  }

  printf("Ejecucion finalizada.\n");
}

/* === Fase 2: Comando ps === */
void command_ps(void) {
  process_print_table();
}

int main() {
  char input[100];
  char *cmd;
  char *arg1;
  char *arg2;

  abrir_log();
  inicializarCPU();
  process_init(); /* Fase 2: inicializar tabla de procesos */
  inicializarDMAThread();

  printf("=== Mini Kernel ===\n");
  printf("Escriba 'help' para ver los comandos.\n");

  while (1) {
    printf("SO> ");
    if (fgets(input, sizeof(input), stdin) == NULL)
      break;

    input[strcspn(input, "\n")] = 0;

    cmd = strtok(input, " ");
    if (cmd == NULL)
      continue;

    if (strcmp(cmd, "exit") == 0) {
      break;
    } else if (strcmp(cmd, "help") == 0) {
      command_help();
    } else if (strcmp(cmd, "load") == 0) {
      arg1 = strtok(NULL, " ");
      if (arg1)
        command_load(arg1);
      else
        printf("Uso: load <archivo>\n");
    } else if (strcmp(cmd, "mem") == 0) {
      arg1 = strtok(NULL, " ");
      arg2 = strtok(NULL, " ");
      if (arg1 && arg2)
        command_mem(atoi(arg1), atoi(arg2));
      else
        printf("Uso: mem <inicio> <cantidad>\n");
    } else if (strcmp(cmd, "reg") == 0) {
      command_reg();
    } else if (strcmp(cmd, "step") == 0) {
      command_step();
    } else if (strcmp(cmd, "ps") == 0) {
      command_ps();
    } else if (strcmp(cmd, "run") == 0) {
      command_run();
    } else if (strcmp(cmd, "reset") == 0) {
      inicializarCPU();
      printf("Sistema reiniciado.\n");
    } else if (strcmp(cmd, "setvec") == 0) {
      // Configurar vector de interrupciones
      arg1 = strtok(NULL, " ");
      arg2 = strtok(NULL, " ");
      if (arg1 && arg2) {
        int codigo = atoi(arg1);
        int direccion = atoi(arg2);
        if (codigo >= 0 && codigo < INTERRUPT_VECTOR_SIZE) {
          vectorInterrupciones[codigo] = direccion;
          printf("Vector[%d] = %d configurado.\n", codigo, direccion);
        } else {
          printf("Error: Código de interrupción inválido (0-%d).\n",
                INTERRUPT_VECTOR_SIZE - 1);
        }
      } else {
        printf("Uso: setvec <codigo> <direccion>\n");
      }
    } else {
      printf("Comando desconocido: %s\n", cmd);
    }
  }

  cerrar_log();
  printf("Simulador terminado.\n");
  return 0;
}
