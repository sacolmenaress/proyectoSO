#include "architecture.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Función para mostrar comandos
void command_help() {
  printf("Comandos disponibles:\n");
  printf("  load <archivo>       : Cargar un programa en memoria\n");
  printf("  mem <inicio> <cant>  : Ver contenido de memoria\n");
  printf("  reg                  : Ver registros del CPU\n");
  printf("  run                  : Ejecutar el programa hasta finalizar\n");
  printf("  step                 : Ejecutar una instruccion (paso a paso)\n");
  printf("  reset                : Reiniciar CPU y Memoria\n");
  printf("  setvec <cod> <dir>   : Configurar vector de interrupciones\n");
  printf("  exit                 : Salir del simulador\n");
}

// Función para cargar programa desde archivo
void command_load(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    printf("Error: No se pudo abrir el archivo '%s'.\n", filename);
    return;
  }

  int address = OS_RESERVED;
  int start_pc = OS_RESERVED; // Default start
  char line[256];
  char progName[100] = "Unknown";
  int numPalabras = 0;

  // Leer linea por linea
  while (fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\r\n")] = 0;
    if (strlen(line) == 0)
      continue;

    // Revisar metadatos 
    if (strncmp(line, "_start", 6) == 0) {
      sscanf(line, "_start %d", &start_pc);
      // Ajustar el PC de inicio sumando OS_RESERVED ya que las direcciones
      // suelen ser absolutas desde 0 lógica
      start_pc += OS_RESERVED;
      continue;
    }
    if (strncmp(line, ".NumeroPalabras", 15) == 0) {
      sscanf(line, ".NumeroPalabras %d", &numPalabras);
      continue;
    }
    if (strncmp(line, ".NombreProg", 11) == 0) {
      sscanf(line, ".NombreProg %s", progName);
      continue;
    }

    // Ignorar comentarios
    if (line[0] == '/' || line[0] == '.')
      continue;

    long long raw_val;
    if (sscanf(line, "%lld", &raw_val) == 1) {
      if (address >= RAM_SIZE) {
        printf("Advertencia: Programa excede tamano de memoria RAM.\n");
        break;
      }
      long long abs_val = llabs(raw_val);
      RAM[address].sign = (int)(abs_val / 10000000);
      RAM[address].value = (int)(abs_val % 10000000);
      if (raw_val < 0) {
        RAM[address].sign = 1;
      }
      address++;
    }
  }

  printf("Programa '%s' cargado. Palabras: %d. Inicio: %d.\n", progName,
        numPalabras, start_pc);
  fclose(file);
  
  // Configurar protección de memoria (Item 11)
  // RB = inicio del área del programa
  // RL = fin del área del programa
  cpu.mp.base = OS_RESERVED;
  cpu.mp.limit = address - 1;  // Última dirección escrita
  
  printf("Protección de memoria: RB=%d, RL=%d\n", cpu.mp.base, cpu.mp.limit);
  
  cpu.PSW.pc = start_pc;
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
  if (cpu.PSW.pc >= RAM_SIZE) {
    printf("PC fuera de rango.\n");
    return;
  }
  if (cpu.halted) {
    printf("CPU detenida.\n");
    return;
  }

  // ITEM 14: Modo debugger con información detallada
  printf("\n==== Modo Debugger ====\n");
  printf("Dirección: %d\n", cpu.PSW.pc);

  // Decodificar y mostrar instrucción ANTES de ejecutar
  Word inst = RAM[cpu.PSW.pc];
  int opcode = (inst.sign * 10) + (inst.value / 1000000);
  int addressing = (inst.value / 100000) % 10;
  int operand = inst.value % 100000;

  printf("Instrucción: Opcode=%02d, Modo=%d, Operando=%05d\n", opcode,
        addressing, operand);

  // Guardar estado anterior
  Word AC_antes = cpu.AC;
  int PC_antes = cpu.PSW.pc;

  // EJECUTAR
  printf("Ejecutando...\n");
  ejecutarInst();

  // DESPUÉS: mostrar resultado
  printf("Resultado: AC=%d (antes=%d), PC=%d (antes=%d)\n",
        obtenerValorReal(cpu.AC), obtenerValorReal(AC_antes), cpu.PSW.pc,
        PC_antes);

  // Preguntar al usuario
  printf("\n[s] Siguiente  [r] Ver registros  [m <dir>] Ver memoria  [Enter] "
        "Continuar: ");
  char input[50];
  if (fgets(input, sizeof(input), stdin)) {
    input[strcspn(input, "\n")] = 0;
    if (input[0] == 'r') {
      command_reg();
    } else if (input[0] == 'm') {
      int addr = 0;
      if (sscanf(input, "m %d", &addr) == 1) {
        command_mem(addr, 5);
      } else {
        printf("Uso: m <direccion>\n");
      }
    }
    // Si es 's', vacío, o cualquier otra cosa, solo continúa
  }
}

void command_run() {
  printf("Ejecutando programa...\n");
  // Ejecutar hasta que se encuentre una condición de parada o error?
  // Por ahora, pondremos un límite de seguridad o hasta interrupción.
  int max_cycles = 1000;
  int cycles = 0;
  while (cycles < max_cycles) {
    if (cpu.halted) {
      printf("Ejecucion detenida por instruccion Halt o error.\n");
      break;
    }
    ejecutarInst();
    cycles++;
  }
  if (cycles >= max_cycles) {
    printf("Ejecucion detenida: Limite de ciclos alcanzado (%d).\n",
          max_cycles);
  }
  printf("Ejecucion finalizada.\n");
}

int main() {
  char input[100];
  char *cmd;
  char *arg1;
  char *arg2;

  abrir_log();
  inicializarCPU();

  printf("=== Simulador de SO Fase I ===\n");
  printf("Escriba 'help' para ver los comandos.\n");

  while (1) {
    printf("OS> ");
    if (fgets(input, sizeof(input), stdin) == NULL)
      break;

    // Remove newline
    input[strcspn(input, "\n")] = 0;

    // Parse command
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
    } else if (strcmp(cmd, "run") == 0) {
      command_run();
    } else if (strcmp(cmd, "reset") == 0) {
      inicializarCPU();
      printf("Sistema reiniciado.\n");
    } else if (strcmp(cmd, "setvec") == 0) {
      // ITEM 13: Configurar vector de interrupciones
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
