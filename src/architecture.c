#include "architecture.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Definir RAM globalmente
Word RAM[RAM_SIZE];
// Definir DISCO globalmente
Word DISK[DISK_SIZE];

// CPU global
CPU_t cpu;

// Vector de Interrupciones (Punto 13 PDF)
int vectorInterrupciones[INTERRUPT_VECTOR_SIZE];

// Obtener valor real de la palabra (signo + magnitud)
int obtenerValorReal(Word w) { return (w.sign == 1) ? -(w.value) : w.value; }

// Asignar valor entero a palabra (separar signo y magnitud)
void asignarValor(Word *w, int resultado) {
  if (resultado < 0) {
    w->sign = 1;
    w->value = -resultado;
  } else {
    w->sign = 0;
    w->value = resultado;
  }
}

// Función auxiliar para lanzar interrupciones
void lanzarInterrupcion(int codigo) {
  if (codigo < 0 || codigo >= INTERRUPT_VECTOR_SIZE) {
    lanzarInterrupcion(INT_INVALID_INT);
    return;
  }

  printf("\n*** INTERRUPCIÓN: Código %d ***\n", codigo);
  // Simulación simplificada: solo reportar y detener si es grave
  // En Fase II, aquí se saltaría al manejador (PC = vector[codigo])

  // Lista de códigos fatales que detienen la máquina en Fase I
  if (codigo == INT_INVALID_ADDR || codigo == INT_INVALID_INST ||
      codigo == INT_INVALID_INT) {
    printf("Error crítico. Deteniendo CPU.\n");
    cpu.halted = 1;
  }
}

// Validar PC
int validarPC(int pc) {
  if (pc < 0 || pc >= RAM_SIZE) {
    lanzarInterrupcion(INT_INVALID_ADDR);
    return 0;
  }
  return 1;
}

// Incrementar PC y validar límites
void incrementarPC() {
  cpu.PSW.pc++;
  if (!validarPC(cpu.PSW.pc)) {
    // Ya se lanzó la interrupción dentro de validarPC
  }
}

// Lectura de memoria con protección
int leerMemoria(int direccion, Word *w) {
  int dirReal =
      (cpu.PSW.mode == MODE_KERNEL) ? direccion : direccion + cpu.mp.base;

  // "En modo privilegiado se omite esta comprobación" (Punto 11 PDF)
  if (cpu.PSW.mode == MODE_USER) {
    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
      lanzarInterrupcion(INT_INVALID_ADDR);
      return 0;
    }
  } else {
    // En modo Kernel, validamos solo límites físicos de la RAM
    if (dirReal < 0 || dirReal >= RAM_SIZE) {
      lanzarInterrupcion(INT_INVALID_ADDR);
      return 0;
    }
  }

  *w = RAM[dirReal];
  return 1;
}

// Escritura en memoria con protección
int escribirMemoria(int direccion, Word w) {
  int dirReal =
      (cpu.PSW.mode == MODE_KERNEL) ? direccion : direccion + cpu.mp.base;

  // "En modo privilegiado se omite esta comprobación" (Punto 11 PDF)
  if (cpu.PSW.mode == MODE_USER) {
    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
      lanzarInterrupcion(INT_INVALID_ADDR);
      return 0;
    }
  } else {
    // En modo Kernel, validamos solo límites físicos de la RAM
    if (dirReal < 0 || dirReal >= RAM_SIZE) {
      lanzarInterrupcion(INT_INVALID_ADDR);
      return 0;
    }
  }

  RAM[dirReal] = w;
  return 1;
}

// Inicializar Memoria RAM y Disco Virtual
void inicializarMemoria() {
  // Reiniciar RAM
  for (int i = 0; i < RAM_SIZE; i++) {
    RAM[i].sign = 0;
    RAM[i].value = 0;
  }

  // Reiniciar DISCO
  for (int i = 0; i < DISK_SIZE; i++) {
    DISK[i].sign = 0;
    DISK[i].value = 0;
  }
}

// Inicializar vector de interrupciones
void inicializarVectorInterrupciones() {
  for (int i = 0; i < INTERRUPT_VECTOR_SIZE; i++) {
    vectorInterrupciones[i] = 0;
  }
}

// Inicializar CPU y RAM
void inicializarCPU() {

  cpu.AC.sign = 0;
  cpu.AC.value = 0;
  cpu.stack.sp = OS_RESERVED;
  cpu.PSW.condition = 0;
  cpu.PSW.mode = MODE_USER; // Siempre inicia en modo usuario
  cpu.PSW.interrupt = 0;
  cpu.PSW.pc = 0;
  cpu.halted = 0; // Inicializar flag de parada

  // Inicializar DMA
  cpu.dma.track = 0;
  cpu.dma.cylinder = 0;
  cpu.dma.sector = 0;
  cpu.dma.io_type = 0;
  cpu.dma.mem_addr = 0;
  cpu.dma.status = 0;

  // Inicializar Componentes
  inicializarMemoria();
  inicializarVectorInterrupciones();
}

void fetch() {
  if (!validarPC(cpu.PSW.pc))
    return;

  Word instruccionCodificada = RAM[cpu.PSW.pc];
  int valorInstruccion = instruccionCodificada.value;

  int op1 = instruccionCodificada.sign;
  int op2 = valorInstruccion / 1000000; // Primer digito de magnitud

  // Opcode real de 2 dígitos
  cpu.IR.opcode = (op1 * 10) + op2;

  int resto = valorInstruccion % 1000000;
  cpu.IR.addressing = resto / 100000; // Siguiente dígito
  cpu.IR.operand = resto % 100000;    // Los 5 últimos

  incrementarPC();
}

int obtenerOperando(int *ok) {
  *ok = 1;
  if (cpu.IR.addressing == ADDR_IMMEDIATE) { // Inmediato
    return cpu.IR.operand;
  } else if (cpu.IR.addressing == ADDR_DIRECT) { // Directo
    Word datos;
    if (leerMemoria(cpu.IR.operand, &datos)) {
      return obtenerValorReal(datos);
    } else {
      *ok = 0;
      return 0;
    }
  } else if (cpu.IR.addressing == ADDR_INDEXED) { // Indexado
    int dirEfectiva = cpu.IR.operand + cpu.stack.rx;
    Word datos;
    if (leerMemoria(dirEfectiva, &datos)) {
      return obtenerValorReal(datos);
    } else {
      *ok = 0;
      return 0;
    }
  }
  return 0;
}

// Función auxiliar para actualizar PSW.condition (0=Cero, 1=Neg, 2=Pos,
// 3=Overflow)
void actualizarCodCond(long long resultado) {
  if (resultado > 9999999 || resultado < -9999999) {
    cpu.PSW.condition = 3; // Overflow
    lanzarInterrupcion(INT_OVERFLOW);
  } else if (resultado == 0) {
    cpu.PSW.condition = 0; // Cero
  } else if (resultado < 0) {
    cpu.PSW.condition = 1; // Negativo (X < Y)
  } else {
    cpu.PSW.condition = 2; // Positivo (X > Y)
  }
}

void decodeExecute() {
  switch (cpu.IR.opcode) {
  case OPC_LOAD: { // 04
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      asignarValor(&cpu.AC, valor);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_STORE: { // 05
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    // Store siempre es a memoria, direccionamiento directo o indexado
    int dirDestino = 0;
    if (cpu.IR.addressing == ADDR_DIRECT) {
      dirDestino = cpu.IR.operand;
    } else if (cpu.IR.addressing == ADDR_INDEXED) {
      dirDestino = cpu.IR.operand + cpu.stack.rx;
    } else {
      lanzarInterrupcion(INT_INVALID_ADDR);
      break;
    }

    // Store guarda el contenido de AC en memoria
    escribirMemoria(dirDestino, cpu.AC);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_ADD: { // 00
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      long long resultado = (long long)obtenerValorReal(cpu.AC) + valor;
      actualizarCodCond(resultado);
      // Truncamiento visual
      if (resultado > 9999999)
        resultado %= 10000000;
      asignarValor(&cpu.AC, (int)resultado);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_SUB: { // 01
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      long long resultado = (long long)obtenerValorReal(cpu.AC) - valor;
      actualizarCodCond(resultado);
      if (resultado < -9999999) {
        lanzarInterrupcion(INT_UNDERFLOW);
        // Ajuste básico
        resultado %= 10000000;
      }
      asignarValor(&cpu.AC, (int)resultado);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_MUL: { // 02
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      long long resultado = (long long)obtenerValorReal(cpu.AC) * valor;
      actualizarCodCond(resultado);
      if (resultado > 9999999)
        resultado %= 10000000;
      asignarValor(&cpu.AC, (int)resultado);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_DIVI: { // 3
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      if (valor == 0) {
        lanzarInterrupcion(INT_OVERFLOW); // División por cero ~ Overflow
      } else {
        long long resultado = (long long)obtenerValorReal(cpu.AC) / valor;
        actualizarCodCond(resultado);
        asignarValor(&cpu.AC, (int)resultado);
      }
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // Operaciones lógicas y de comparación
  case OPC_CMPE: { // 15
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      int acVal = obtenerValorReal(cpu.AC);
      actualizarCodCond((long long)acVal - valor);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_CMPL: { // 16
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      int acVal = obtenerValorReal(cpu.AC);
      actualizarCodCond((long long)acVal - valor);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_CMPG: { // 17
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      int acVal = obtenerValorReal(cpu.AC);
      actualizarCodCond((long long)acVal - valor);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

    // Saltos
  case OPC_JEQ: { // 20
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    if (cpu.PSW.condition == 0) {
      int target = cpu.IR.operand;
      if (cpu.IR.addressing == ADDR_INDEXED)
        target += cpu.stack.rx;
      cpu.PSW.pc = target - 1;
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_JNE: { // 21
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    if (cpu.PSW.condition != 0) {
      int target = cpu.IR.operand;
      if (cpu.IR.addressing == ADDR_INDEXED)
        target += cpu.stack.rx;
      cpu.PSW.pc = target - 1;
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_JL: { // 22
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    if (cpu.PSW.condition == 1) {
      int target = cpu.IR.operand;
      if (cpu.IR.addressing == ADDR_INDEXED)
        target += cpu.stack.rx;
      cpu.PSW.pc = target - 1;
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_JG: { // 23
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    if (cpu.PSW.condition == 2) {
      int target = cpu.IR.operand;
      if (cpu.IR.addressing == ADDR_INDEXED)
        target += cpu.stack.rx;
      cpu.PSW.pc = target - 1;
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_SVC: { // 13
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    lanzarInterrupcion(INT_SYSCALL);
    int param = (cpu.stack.sp > OS_RESERVED)
                    ? obtenerValorReal(RAM[cpu.stack.sp - 1])
                    : 0;
    printf("DEBUG SVC: Param en stack=%d\n", param);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_RETRN: { // 14
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    if (cpu.stack.sp > OS_RESERVED) {
      cpu.AC = RAM[--cpu.stack.sp];
      cpu.PSW.pc =
          obtenerValorReal(cpu.AC) - 1; // -1 porque incrementarPC sumará 1
    } else {
      lanzarInterrupcion(INT_UNDERFLOW);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_POP_AM: { // 24
    break;
  }

  case OPC_MOV_RX: { // 40
    break;
  }

  case OPC_PSH: { // 25
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    RAM[cpu.stack.sp++] = cpu.AC;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_POP: { // 26
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.AC = RAM[--cpu.stack.sp];
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_J: { // 27
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int target = cpu.IR.operand;
    if (cpu.IR.addressing == ADDR_INDEXED) {
      target = obtenerValorReal(cpu.AC) + cpu.IR.operand;
    }
    cpu.PSW.pc = target - 1;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_SDMAP: // 28
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.dma.track = cpu.IR.operand;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  case OPC_SDMAC: // 29
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.dma.cylinder = cpu.IR.operand;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  case OPC_SDMAS: // 30
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.dma.sector = cpu.IR.operand;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  case OPC_SDMAIO: // 31
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.dma.io_type = cpu.IR.operand;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  case OPC_SDMAM: // 32
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.dma.mem_addr = cpu.IR.operand;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  case OPC_SDMAON: { // 33
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);

    // Calcular dirección lineal en el disco
    int disk_addr = (cpu.dma.cylinder * DISK_TRACKS * DISK_SECTORS) +
                    (cpu.dma.track * DISK_SECTORS) + cpu.dma.sector;

    if (disk_addr >= DISK_SIZE || disk_addr < 0) {
      cpu.dma.status = 1; // Error
    } else {
      if (cpu.dma.io_type == 0) { // Lectura: Disco -> RAM
        Word data = DISK[disk_addr];
        if (escribirMemoria(cpu.dma.mem_addr, data)) {
          cpu.dma.status = 0;
        } else {
          cpu.dma.status = 1;
        }
      } else { // Escritura: RAM -> Disco
        Word data;
        if (leerMemoria(cpu.dma.mem_addr, &data)) {
          DISK[disk_addr] = data;
          cpu.dma.status = 0;
        } else {
          cpu.dma.status = 1;
        }
      }

      if (cpu.dma.status == 0) {
        lanzarInterrupcion(INT_IO_COMPLETE);
      }
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_HALT: { // 99 - Parada
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    printf("--- HALT instruction executed. CPU stopped. ---\n");
    cpu.halted = 1;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  default: {
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    lanzarInterrupcion(INT_INVALID_INST);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }
  }
}

void ejecutarInst() {
  if (cpu.halted)
    return;
  fetch();
  decodeExecute();
}
