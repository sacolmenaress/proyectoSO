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

// Vector de Interrupciones
int vectorInterrupciones[INTERRUPT_VECTOR_SIZE];
pthread_mutex_t bus_mutex;  // Mutex POSIX para arbitraje de bus CPU/DMA

// Obtener valor real de la palabra (signo + magnitud)
int obtenerValorReal(Word w) { return (w.sign == 1) ? -(w.value) : w.value; }

// Asignar valor entero a palabra separar signo y magnitud
void asignarValor(Word *w, int resultado) {
  if (resultado < 0) {
    w->sign = 1;
    w->value = -resultado;
  } else {
    w->sign = 0;
    w->value = resultado;
  }
}

// Tabla de descripciones de interrupciones
static const char *INT_DESCRIPTIONS[] = {
    "Código de llamada al sistema inválido", // 0
    "Código de interrupción inválido",       // 1
    "Llamada al sistema",                     // 2
    "Reloj",                                  // 3
    "Finalización de E/S",                    // 4
    "Instrucción inválida",                   // 5
    "Direccionamiento inválido",              // 6
    "Underflow",                              // 7
    "Overflow"                                // 8
};

/**
 * Lanza una interrupción y maneja su procesamiento
 *  codigo Código de interrupción (0-8)
 *
 * Imprime mensaje en salida estándar Y en log
 * Salvaguarda registros y salta al manejador
 */
// ──────────────────────────────────────────────────────────────
// Pila del Sistema (RAM[30-299], crece hacia abajo)
// Usada SOLO por el kernel para guardar/restaurar contexto
// en interrupciones.  No pasa por MMU (acceso directo a RAM).
// ──────────────────────────────────────────────────────────────
int sysPush(int valor) {
  cpu.system_sp--;
  if (cpu.system_sp < OS_STACK_BOTTOM) {
    printf("FATAL: Stack Overflow del sistema (SP=%d < %d)\n",
           cpu.system_sp, OS_STACK_BOTTOM);
    cpu.system_sp++;  // Revertir
    return 0; // fallo
  }
  RAM[cpu.system_sp].sign = (valor < 0) ? 1 : 0;
  RAM[cpu.system_sp].value = (valor < 0) ? -valor : valor;
  return 1; // éxito
}

int sysPop(int *valor) {
  if (cpu.system_sp > OS_STACK_TOP) {
    printf("FATAL: Stack Underflow del sistema (SP=%d > %d)\n",
           cpu.system_sp, OS_STACK_TOP);
    return 0; // fallo
  }
  int v = RAM[cpu.system_sp].value;
  if (RAM[cpu.system_sp].sign) v = -v;
  *valor = v;
  cpu.system_sp++;
  return 1; // éxito
}

void lanzarInterrupcion(int codigo) {
  if (codigo < 0 || codigo >= INTERRUPT_VECTOR_SIZE) {
    lanzarInterrupcion(INT_INVALID_INT);
    return;
  }

  // Mensaje con código y descripción
  char mensaje[256];
  snprintf(mensaje, sizeof(mensaje), "INTERRUPCIÓN: Código %d - %s", codigo,
          INT_DESCRIPTIONS[codigo]);
  printf("\n*** %s ***\n", mensaje);
  escribir_log(mensaje);

  // 1. GUARDAR CONTEXTO en la pila del sistema (7 registros)
  //    Orden: PC, AC, RX, RB, RL, CC, Mode
  //    (RETRN los restaura en orden inverso)
  sysPush(cpu.PSW.pc);
  sysPush(obtenerValorReal(cpu.AC));
  sysPush(cpu.stack.rx);
  sysPush(cpu.mp.base);
  sysPush(cpu.mp.limit);
  sysPush(cpu.PSW.condition);
  sysPush(cpu.PSW.mode);

  // 2. Cambiar a modo kernel y deshabilitar interrupciones
  cpu.PSW.mode = MODE_KERNEL;
  cpu.PSW.interrupt = 0;

  // 3. Leer dirección del manejador desde RAM[codigo] (vector en RAM)
  int handler = RAM[codigo].value;  // RAM[0..8] contiene direcciones

  if (handler != 0) {
    printf("Saltando al manejador en dirección %d\n", handler);
    cpu.PSW.pc = handler - 1;  // -1 porque PC se incrementa tras fetch
  } else {
    printf("No hay manejador configurado para esta interrupción.\n");
    if (codigo == INT_INVALID_ADDR || codigo == INT_INVALID_INST ||
        codigo == INT_INVALID_INT) {
      printf("Error crítico. Deteniendo CPU.\n");
      cpu.halted = 1;
    }
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

/**
 * Lee una palabra de memoria con protección y arbitraje de bus
 * @param direccion Dirección LÓGICA (el programa usa 0, 1, 50, etc.)
 * @param w Puntero donde se almacenará la palabra leída
 * return 1 si éxito, 0 si error (genera interrupción)
 *
 * MMU: En modo USUARIO, la dirección lógica se traduce a física sumando RB.
 *      En modo KERNEL, la dirección se usa directamente (acceso a toda la RAM).
 *
 * Implementa arbitraje de bus (pthread_mutex_t bus_mutex)
 * Previene condiciones de competencia CPU/DMA
 * Protección de memoria con RB/RL en modo USER
 */
int leerMemoria(int direccion, Word *w) {
  // ARBITRAJE DE BUS: Adquirir mutex
  pthread_mutex_lock(&bus_mutex);

  // MMU: Traducir dirección lógica a física
  int dirReal;
  if (cpu.PSW.mode == MODE_USER) {
    dirReal = direccion + cpu.mp.base;  // Modo usuario: lógica + RB
  } else {
    dirReal = direccion;  // Modo kernel: acceso directo
  }

  // En modo privilegiado se omite esta comprobación
  if (cpu.PSW.mode == MODE_USER) {
    // Validar que la dirección esté dentro del rango del proceso [RB, RL]
    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
      pthread_mutex_unlock(&bus_mutex); // Liberar bus antes de salir
      lanzarInterrupcion(INT_INVALID_ADDR);
      return 0;
    }
  } else {
    // En modo Kernel, validamos solo límites físicos de la RAM
    if (dirReal < 0 || dirReal >= RAM_SIZE) {
      pthread_mutex_unlock(&bus_mutex); // Liberar bus antes de salir
      lanzarInterrupcion(INT_INVALID_ADDR);
      return 0;
    }
  }

  *w = RAM[dirReal];
  pthread_mutex_unlock(&bus_mutex); // Liberar bus después del acceso
  return 1;
}

/**
 * Escribe una palabra en memoria con protección y arbitraje de bus
 * param direccion Dirección LÓGICA (el programa usa 0, 1, 50, etc.)
 * param w Palabra a escribir
 * return 1 si éxito, 0 si error (genera interrupción)
 *
 * MMU: En modo USUARIO, la dirección lógica se traduce a física sumando RB.
 *      En modo KERNEL, la dirección se usa directamente (acceso a toda la RAM).
 *
 * Implementa arbitraje de bus (pthread_mutex_t bus_mutex)
 * Previene condiciones de competencia CPU/DMA
 * Protección de memoria con RB/RL en modo USER
 */
int escribirMemoria(int direccion, Word w) {
  // ARBITRAJE DE BUS: Adquirir mutex
  pthread_mutex_lock(&bus_mutex);

  // MMU: Traducir dirección lógica a física
  int dirReal;
  if (cpu.PSW.mode == MODE_USER) {
    dirReal = direccion + cpu.mp.base;  // Modo usuario: lógica + RB
  } else {
    dirReal = direccion;  // Modo kernel: acceso directo
  }

  // En modo privilegiado se omite esta comprobación
  if (cpu.PSW.mode == MODE_USER) {
    // Validar que la dirección esté dentro del rango del proceso [RB, RL]
    if (dirReal < cpu.mp.base || dirReal > cpu.mp.limit) {
      pthread_mutex_unlock(&bus_mutex); // Liberar bus antes de salir
      lanzarInterrupcion(INT_INVALID_ADDR);
      return 0;
    }
  } else {
    // En modo Kernel, validamos solo límites físicos de la RAM
    if (dirReal < 0 || dirReal >= RAM_SIZE) {
      pthread_mutex_unlock(&bus_mutex); // Liberar bus antes de salir
      lanzarInterrupcion(INT_INVALID_ADDR);
      return 0;
    }
  }

  RAM[dirReal] = w;
  pthread_mutex_unlock(&bus_mutex); // Liberar bus después del acceso
  return 1;
}

// Inicializar Memoria RAM y Disco Virtual
void inicializarMemoria() {
  // Inicializar mutex del bus
  pthread_mutex_init(&bus_mutex, NULL);

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

/**
 * Inicializar el Área Reservada del SO en RAM (posiciones 0-299)
 *
 * Estructura:
 *   [0..8]    - Vector de Interrupciones: cada posición contiene la dirección
 *               del manejador correspondiente al código de interrupción.
 *   [20]      - Manejador genérico: instrucción RETRN (opcode 14) que
 *               restaura el contexto y retorna de la interrupción.
 *   [21..29]  - Reservado para manejadores específicos futuros.
 *   [30..299] - Área de pila del sistema (crece hacia abajo desde 299).
 *
 * Esta función debe llamarse DESPUÉS de inicializarMemoria() para que
 * los valores no sean sobreescritos por la limpieza de RAM.
 */
void inicializarAreaSO() {
  // 1. Escribir el Vector de Interrupciones en RAM[0..8]
  //    Cada posición guarda la dirección del manejador para ese código.
  //    Por ahora, todos apuntan al manejador genérico en OS_HANDLER_ADDR (20).
  for (int i = 0; i <= INT_OVERFLOW; i++) {
    RAM[OS_IVT_START + i].sign = 0;
    RAM[OS_IVT_START + i].value = OS_HANDLER_ADDR;

    // Sincronizar con el array C de vectorInterrupciones
    vectorInterrupciones[i] = OS_HANDLER_ADDR;
  }

  // 2. Colocar el manejador genérico en RAM[20]
  //    Instrucción RETRN (opcode 14):
  //    Formato de palabra: sign=1, value=4000000
  //    Decodificación: opcode = (1*10)+4 = 14, addressing=0, operand=00000
  RAM[OS_HANDLER_ADDR].sign = 1;
  RAM[OS_HANDLER_ADDR].value = 4000000;

  // 3. Las posiciones 21-29 quedan en 0 (reservadas para futuros manejadores)
  // 4. Las posiciones 30-299 quedan en 0 (área de pila del sistema, disponible)

  printf("Área del SO inicializada: Vector[0-%d]->%d, Handler@%d=RETRN\n",
         INT_OVERFLOW, OS_HANDLER_ADDR, OS_HANDLER_ADDR);
}

// Inicializar CPU y RAM
void inicializarCPU() {

  cpu.AC.sign = 0;
  cpu.AC.value = 0;
  cpu.stack.sp = OS_RESERVED;
  cpu.system_sp = OS_STACK_TOP + 1;  // Pila sistema vacía (crece hacia abajo)
  cpu.PSW.condition = 0;
  cpu.PSW.mode = MODE_USER; // Siempre inicia en modo usuario
  cpu.PSW.interrupt = 0;
  cpu.PSW.pc = 0;
  cpu.halted = 0; // Inicializar flag de parada

  // Inicializar protección de memoria
  cpu.mp.base = 0;
  cpu.mp.limit = 0;

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
  inicializarAreaSO(); // Poblar el área reservada del SO
}

/**
 * Fase FETCH del ciclo de instrucción
 *
 * Implementa fase de búsqueda usando PC, MAR, MDR, IR
 * 1. Lee instrucción de RAM[PC]
 * 2. Decodifica en IR (opcode, addressing, operand)
 * 3. Incrementa PC
 */
void fetch() {
  if (!validarPC(cpu.PSW.pc))
    return;

  // MMU: Traducir PC lógico a dirección física para leer la instrucción
  int dirFisica;
  if (cpu.PSW.mode == MODE_USER) {
    dirFisica = cpu.PSW.pc + cpu.mp.base;  // PC lógico + RB
  } else {
    dirFisica = cpu.PSW.pc;  // Kernel: acceso directo
  }

  // Validar dirección física resultante
  if (dirFisica < 0 || dirFisica >= RAM_SIZE) {
    lanzarInterrupcion(INT_INVALID_ADDR);
    return;
  }

  Word instruccionCodificada = RAM[dirFisica];
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

/**
 * Fase EXECUTE del ciclo de instrucción
 *
 * Implementa fase de ejecución
 * Decodifica opcode del IR y ejecuta la instrucción correspondiente
 * Respeta códigos de operación y formato de arquitectura
 */
void decodeExecute() {
  switch (cpu.IR.opcode) {

  // ============================================================
  // ARITMÉTICAS (00-03)
  // ============================================================

  case OPC_ADD: { // 00 - SUM: AC = AC + operando
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      long long resultado = (long long)obtenerValorReal(cpu.AC) + valor;
      actualizarCodCond(resultado);
      if (resultado > 9999999)
        resultado %= 10000000;
      asignarValor(&cpu.AC, (int)resultado);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_SUB: { // 01 - RES: AC = AC - operando
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      long long resultado = (long long)obtenerValorReal(cpu.AC) - valor;
      actualizarCodCond(resultado);
      if (resultado < -9999999) {
        lanzarInterrupcion(INT_UNDERFLOW);
        resultado %= 10000000;
      }
      asignarValor(&cpu.AC, (int)resultado);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_MUL: { // 02 - MULT: AC = AC * operando
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

  case OPC_DIVI: { // 03 - DIVI: AC = AC / operando
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      if (valor == 0) {
        lanzarInterrupcion(INT_OVERFLOW);
      } else {
        long long resultado = (long long)obtenerValorReal(cpu.AC) / valor;
        actualizarCodCond(resultado);
        asignarValor(&cpu.AC, (int)resultado);
      }
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // TRANSFERENCIA DE DATOS (04-07)
  // ============================================================

  case OPC_LOAD: { // 04 - LOAD: AC = operando
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      asignarValor(&cpu.AC, valor);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_STORE: { // 05 - STR: M[operando] = AC
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int dirDestino = 0;
    if (cpu.IR.addressing == ADDR_DIRECT) {
      dirDestino = cpu.IR.operand;
    } else if (cpu.IR.addressing == ADDR_INDEXED) {
      dirDestino = cpu.IR.operand + cpu.stack.rx;
    } else {
      lanzarInterrupcion(INT_INVALID_ADDR);
      break;
    }
    escribirMemoria(dirDestino, cpu.AC);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_LOADRX: { // 06 - LOADRX: RX = operando [NUEVO]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      cpu.stack.rx = valor;
      printf("RX cargado: %d\n", cpu.stack.rx);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_STRRX: { // 07 - STRRX: M[operando] = RX [NUEVO]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int dirDestino = 0;
    if (cpu.IR.addressing == ADDR_DIRECT) {
      dirDestino = cpu.IR.operand;
    } else if (cpu.IR.addressing == ADDR_INDEXED) {
      dirDestino = cpu.IR.operand + cpu.stack.rx;
    } else {
      lanzarInterrupcion(INT_INVALID_ADDR);
      break;
    }
    Word valor;
    asignarValor(&valor, cpu.stack.rx);
    escribirMemoria(dirDestino, valor);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // COMPARACIÓN (08)
  // ============================================================

  case OPC_COMP: { // 08 - COMP: Compara AC con operando
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      int acVal = obtenerValorReal(cpu.AC);
      // CC=0 si iguales, CC=1 si AC < operando, CC=2 si AC > operando
      actualizarCodCond((long long)acVal - valor);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // SALTOS CONDICIONALES (09-12)
  // ============================================================

  case OPC_JEQ: { // 09 - JMPE: salta si AC == M[SP]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    Word stackWord;
    if (leerMemoria(cpu.stack.sp, &stackWord)) {
      int acVal = obtenerValorReal(cpu.AC);
      int spVal = obtenerValorReal(stackWord);
      if (acVal == spVal) {
        int target = cpu.IR.operand;
        if (cpu.IR.addressing == ADDR_INDEXED)
          target += cpu.stack.rx;
        cpu.PSW.pc = target - 1;
      }
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_JNE: { // 10 - JMPNE: salta si AC != M[SP]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    Word stackWord;
    if (leerMemoria(cpu.stack.sp, &stackWord)) {
      int acVal = obtenerValorReal(cpu.AC);
      int spVal = obtenerValorReal(stackWord);
      if (acVal != spVal) {
        int target = cpu.IR.operand;
        if (cpu.IR.addressing == ADDR_INDEXED)
          target += cpu.stack.rx;
        cpu.PSW.pc = target - 1;
      }
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_JL: { // 11 - JMPLT: salta si AC < M[SP]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    Word stackWord;
    if (leerMemoria(cpu.stack.sp, &stackWord)) {
      int acVal = obtenerValorReal(cpu.AC);
      int spVal = obtenerValorReal(stackWord);
      if (acVal < spVal) {
        int target = cpu.IR.operand;
        if (cpu.IR.addressing == ADDR_INDEXED)
          target += cpu.stack.rx;
        cpu.PSW.pc = target - 1;
      }
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_JG: { // 12 - JMPLGT: salta si AC > M[SP]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    Word stackWord;
    if (leerMemoria(cpu.stack.sp, &stackWord)) {
      int acVal = obtenerValorReal(cpu.AC);
      int spVal = obtenerValorReal(stackWord);
      if (acVal > spVal) {
        int target = cpu.IR.operand;
        if (cpu.IR.addressing == ADDR_INDEXED)
          target += cpu.stack.rx;
        cpu.PSW.pc = target - 1;
      }
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // SISTEMA (13-18)
  // ============================================================

  case OPC_SVC: { // 13 - Llamada al sistema
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    lanzarInterrupcion(INT_SYSCALL);
    int param = (cpu.stack.sp > OS_RESERVED)
                    ? obtenerValorReal(RAM[cpu.stack.sp - 1])
                    : 0;
    printf("DEBUG SVC: Param en stack=%d\n", param);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_RETRN: { // 14 - Retorno de interrupción
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    if (cpu.PSW.mode == MODE_USER) {
      // Un programa de usuario NO puede ejecutar RETRN
      lanzarInterrupcion(INT_INVALID_INST);
    } else {
      // Restaurar contexto en ORDEN INVERSO al guardado:
      // Guardado: PC, AC, RX, RB, RL, CC, Mode
      // Restaurar: Mode, CC, RL, RB, RX, AC, PC
      int temp;
      printf("Restaurando contexto desde pila del sistema...\n");
      sysPop(&temp); cpu.PSW.mode = temp;
      sysPop(&temp); cpu.PSW.condition = temp;
      sysPop(&temp); cpu.mp.limit = temp;
      sysPop(&temp); cpu.mp.base = temp;
      sysPop(&temp); cpu.stack.rx = temp;
      sysPop(&temp);
      cpu.AC.sign = (temp < 0) ? 1 : 0;
      cpu.AC.value = (temp < 0) ? -temp : temp;
      sysPop(&temp); cpu.PSW.pc = temp;
      cpu.PSW.interrupt = 1; // Rehabilitar interrupciones
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_HAB: { // 15 - Habilitar interrupciones [NUEVO]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.PSW.interrupt = 1;
    printf("Interrupciones HABILITADAS\n");
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_DHAB: { // 16 - Deshabilitar interrupciones [NUEVO]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.PSW.interrupt = 0;
    printf("Interrupciones DESHABILITADAS\n");
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_TTI: { // 17 - Timer Tick Interrupt [NUEVO]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    printf("TTI: Timer Tick ejecutado.\n");
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_CHMOD: { // 18 - Cambiar modo [NUEVO]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    if (cpu.PSW.mode == MODE_USER) {
      printf("ERROR: Intento de CHMOD en Modo Usuario.\n");
      lanzarInterrupcion(INT_INVALID_INST);
    } else {
      int ok;
      int valor = obtenerOperando(&ok);
      if (ok) {
        if (valor == MODE_USER || valor == MODE_KERNEL) {
          cpu.PSW.mode = valor;
          printf("Modo cambiado a: %s\n",
                 valor == MODE_KERNEL ? "KERNEL" : "USER");
        } else {
          lanzarInterrupcion(INT_INVALID_INST);
        }
      }
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // REGISTROS BASE/LÍMITE/PILA (19-24)
  // ============================================================

  case OPC_LOADRB: { // 19 - LOADRB: RB = operando
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      cpu.mp.base = valor;
      printf("RB cargado: %d\n", cpu.mp.base);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_STRRB: { // 20 - STRRB: M[operando] = RB
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int dirDestino = 0;
    if (cpu.IR.addressing == ADDR_DIRECT) {
      dirDestino = cpu.IR.operand;
    } else if (cpu.IR.addressing == ADDR_INDEXED) {
      dirDestino = cpu.IR.operand + cpu.stack.rx;
    }
    Word valor;
    asignarValor(&valor, cpu.mp.base);
    escribirMemoria(dirDestino, valor);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_LOADRL: { // 21 - LOADRL: RL = operando
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      cpu.mp.limit = valor;
      printf("RL cargado: %d\n", cpu.mp.limit);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_STRRL: { // 22 - STRRL: M[operando] = RL
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int dirDestino = 0;
    if (cpu.IR.addressing == ADDR_DIRECT) {
      dirDestino = cpu.IR.operand;
    } else if (cpu.IR.addressing == ADDR_INDEXED) {
      dirDestino = cpu.IR.operand + cpu.stack.rx;
    }
    Word valor;
    asignarValor(&valor, cpu.mp.limit);
    escribirMemoria(dirDestino, valor);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_LOADSP: { // 23 - LOADSP: SP = operando [NUEVO]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int ok;
    int valor = obtenerOperando(&ok);
    if (ok) {
      cpu.stack.sp = valor;
      printf("SP cargado: %d\n", cpu.stack.sp);
    }
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_STRSP: { // 24 - STRSP: M[operando] = SP [NUEVO]
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int dirDestino = 0;
    if (cpu.IR.addressing == ADDR_DIRECT) {
      dirDestino = cpu.IR.operand;
    } else if (cpu.IR.addressing == ADDR_INDEXED) {
      dirDestino = cpu.IR.operand + cpu.stack.rx;
    }
    Word valor;
    asignarValor(&valor, cpu.stack.sp);
    escribirMemoria(dirDestino, valor);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // STACK (25-26)
  // ============================================================

  case OPC_PSH: { // 25 - Push AC a la pila
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    RAM[cpu.stack.sp++] = cpu.AC;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  case OPC_POP: { // 26 - Pop de la pila a AC
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    cpu.AC = RAM[--cpu.stack.sp];
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // SALTO INCONDICIONAL (27)
  // ============================================================

  case OPC_J: { // 27 - Salto incondicional
    log_inicio_instruccion(cpu.PSW.pc, cpu.IR.opcode);
    int target = cpu.IR.operand;
    if (cpu.IR.addressing == ADDR_INDEXED) {
      target = obtenerValorReal(cpu.AC) + cpu.IR.operand;
    }
    cpu.PSW.pc = target - 1;
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // DMA (28-33)
  // ============================================================

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
    int disk_addr = (cpu.dma.cylinder * DISK_TRACKS * DISK_SECTORS) +
                    (cpu.dma.track * DISK_SECTORS) + cpu.dma.sector;
    if (disk_addr >= DISK_SIZE || disk_addr < 0) {
      cpu.dma.status = 1;
    } else {
      if (cpu.dma.io_type == 0) {
        Word data = DISK[disk_addr];
        if (escribirMemoria(cpu.dma.mem_addr, data)) {
          cpu.dma.status = 0;
        } else {
          cpu.dma.status = 1;
        }
      } else {
        Word data;
        if (leerMemoria(cpu.dma.mem_addr, &data)) {
          DISK[disk_addr] = data;
          cpu.dma.status = 0;
        } else {
          cpu.dma.status = 1;
        }
      }
    }
    lanzarInterrupcion(INT_IO_COMPLETE);
    log_resultado_instruccion(cpu.AC, cpu.stack.sp, cpu.PSW.condition);
    break;
  }

  // ============================================================
  // HALT (99)
  // ============================================================

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
