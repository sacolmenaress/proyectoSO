/*
 * Mantiene la tabla de procesos, maneja los cambios de estado
 * y salva/restaura contextos usando la CPU_t global de architecture.h.
 */

#include "process.h"
#include "architecture.h"
#include "log.h"
#include "console_colors.h"
#include <stdio.h>
#include <string.h>

/* ============================================================
 * DEFINICIÓN DE VARIABLES GLOBALES
 * ============================================================ */
PCB_t process_table[MAX_PROCESSES];
int   current_pid   = -1;
int   system_ticks  =  0;
int   partition_bitmap[NUM_PARTITIONS];

/* NUEVO: Estructura original para la Cola de Listos (Ready Queue) */
typedef struct {
    int pids[MAX_PROCESSES];
    int head;
    int tail;
    int count;
} ReadyQueue_t;

static ReadyQueue_t rq;

void process_enqueue_ready(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return;
    if (rq.count >= MAX_PROCESSES) {
        printf("[ERROR KERNEL] Ready Queue llena. No se puede encolar PID=%d\n", pid);
        return;
    }
    rq.pids[rq.tail] = pid;
    rq.tail = (rq.tail + 1) % MAX_PROCESSES;
    rq.count++;
}

int process_dequeue_ready(void) {
    if (rq.count == 0) return -1;
    int pid = rq.pids[rq.head];
    rq.head = (rq.head + 1) % MAX_PROCESSES;
    rq.count--;
    return pid;
}

/* Espacio en disco virtual reservado para procesos.
 * Cada proceso ocupa hasta PARTITION_SIZE palabras en DISK[].
 * Proceso 0 → DISK[0..339], proceso 1 → DISK[340..679], etc.
 * Se usa el pid * PARTITION_SIZE como offset.
 * (Simple y sin riesgo de colisión con la lógica DMA de architecture.c) */

/* ============================================================
 * INICIALIZACIÓN
 * ============================================================ */
void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid              = -1;
        process_table[i].name[0]          = '\0';
        process_table[i].state            = STATE_TERMINATED;
        process_table[i].partition_id     = -1;
        process_table[i].base             = -1;
        process_table[i].limit            = -1;
        process_table[i].disk_offset      = -1;
        process_table[i].prog_size        =  0;
        process_table[i].entry_point      =  0;
        process_table[i].quantum_counter  =  0;
        process_table[i].wake_tick        =  0;
        memset(&process_table[i].ctx, 0, sizeof(ProcessContext_t));
    }
    for (int i = 0; i < NUM_PARTITIONS; i++) {
        partition_bitmap[i] = 0; /* 0 = libre */
    }
    current_pid  = -1;
    system_ticks =  0;

    /* Inicializar la cola de listos */
    rq.head  = 0;
    rq.tail  = 0;
    rq.count = 0;
    memset(rq.pids, -1, sizeof(rq.pids));

    {
        char msg[128];
        snprintf(msg, sizeof(msg),
                "KERNEL: Tabla de procesos inicializada (max=%d procesos).",
                MAX_PROCESSES);
        escribir_log(msg);
    }
}

/* ============================================================
 * UTILIDADES
 * ============================================================ */
const char *state_to_string(ProcessState s) {
    switch (s) {
        case STATE_NEW:        return "NUEVO";
        case STATE_READY:      return "LISTO";
        case STATE_RUNNING:    return "EN EJECUCION";
        case STATE_SLEEPING:   return "DORMIDO";
        case STATE_TERMINATED: return "TERMINADO";
        default:               return "DESCONOCIDO";
    }
}

/* ============================================================
 * CAMBIO DE ESTADO — registra SIEMPRE en log.txt
 * ============================================================ */
void process_change_state(int pid, ProcessState new_state) {
    if (pid < 0 || pid >= MAX_PROCESSES) return;

    ProcessState old_state = process_table[pid].state;
    process_table[pid].state = new_state;

    /* Si pasa a READY, lo encolamos automáticamente (si no estaba ya) */
    if (new_state == STATE_READY && old_state != STATE_READY) {
        process_enqueue_ready(pid);
    }

    char msg[256];
    snprintf(msg, sizeof(msg),
            "PROCESO [PID=%d] (%s): %s --> %s  (tick=%d)",
            pid,
            process_table[pid].name,
            state_to_string(old_state),
            state_to_string(new_state),
            system_ticks);
    escribir_log(msg);
    printf(ANSI_FG_GREEN "[PROCESO]" ANSI_RESET " PID=%d (%s): %s -> %s\n",
            pid,
            process_table[pid].name,
            state_to_string(old_state),
            state_to_string(new_state));
}

/* ============================================================
 * PARTICIONES DE MEMORIA
 * ============================================================ */
int process_find_partition(void) {
    for (int i = 0; i < NUM_PARTITIONS; i++) {
        if (partition_bitmap[i] == 0)
            return i;
    }
    return -1; /* Memoria llena */
}

void process_free_partition(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return;
    int part = process_table[pid].partition_id;
    if (part >= 0 && part < NUM_PARTITIONS) {
        partition_bitmap[part] = 0;
        process_table[pid].partition_id = -1;
        process_table[pid].base  = -1;
        process_table[pid].limit = -1;
    }
}

/* ============================================================
 * CARGA DEL PROCESO DESDE DISCO A RAM
 * ============================================================ */
void process_load_to_ram(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return;
    PCB_t *p = &process_table[pid];

    /* Copiar desde DISK al área de la partición en RAM */
    for (int i = 0; i < p->prog_size && i < PARTITION_SIZE; i++) {
        RAM[p->base + i] = DISK[p->disk_offset + i];
    }

    char msg[256];
    snprintf(msg, sizeof(msg),
            "KERNEL: PID=%d (%s) cargado en RAM[%d..%d] desde DISK[%d].",
            pid, p->name, p->base, p->base + p->prog_size - 1,
            p->disk_offset);
    escribir_log(msg);
}

/* ============================================================
 * CREACIÓN DE PROCESO
 *  - Lee archivo de texto
 *  - Almacena instrucciones en DISK[]
 *  - Crea PCB en estado NEW
 *  Retorna PID o -1 si error
 * ============================================================ */
int process_create(const char *filename, const char *name) {
    /* 1. Buscar slot libre en la tabla (máx. 20) */
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == -1 || process_table[i].state == STATE_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                "ERROR: Tabla de procesos llena. No se puede crear '%s'.", name);
        escribir_log(msg);
        printf("ERROR: Tabla de procesos llena (max=%d).\n", MAX_PROCESSES);
        return -1;
    }

    /* 2. Offset en disco = slot * PARTITION_SIZE */
    int disk_off = slot * PARTITION_SIZE;

    /* 3. Leer el archivo y guardar en DISK[] */
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf(ANSI_FG_B_RED "ERROR:" ANSI_RESET " No se pudo abrir el archivo '%s'.\n", filename);
        return -1;
    }

    char line[256];
    int  words_read  = 0;
    int  entry_point = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0) continue;

        /* Metadato: punto de entrada */
        if (strncmp(line, "_start", 6) == 0) {
            int sp = 0;
            sscanf(line, "_start %d", &sp);
            entry_point = (sp > 0) ? sp - 1 : 0; /* base 0 */
            continue;
        }
        /* Ignorar directivas y comentarios */
        if (line[0] == '.' || line[0] == '/' || line[0] == '#')
            continue;

        /* Verificar que no sobrepase la partición en disco */
        if (words_read >= PARTITION_SIZE) {
            printf("ADVERTENCIA: Programa '%s' supera %d palabras. Truncado.\n",
                name, PARTITION_SIZE);
            break;
        }

        /* Verificar que no sobrepase el disco virtual */
        if (disk_off + words_read >= DISK_SIZE) {
            printf("ADVERTENCIA: Disco virtual lleno. Proceso '%s' truncado.\n", name);
            break;
        }

        long long raw = atoll(line);
        long long abs_val = (raw < 0) ? -raw : raw;

        DISK[disk_off + words_read].sign  = (raw < 0) ? 1 : (int)(abs_val / 10000000);
        DISK[disk_off + words_read].value = (int)(abs_val % 10000000);
        if (raw < 0) DISK[disk_off + words_read].sign = 1;
        words_read++;
    }
    fclose(f);

    if (words_read == 0) {
        printf("ERROR: El archivo '%s' no contiene instrucciones válidas.\n", filename);
        return -1;
    }

    /* 4. Verificar que cabe en una partición de RAM */
    if (words_read > PARTITION_SIZE) {
        printf("ERROR: El programa '%s' (%d palabras) no cabe en una partición (%d palabras).\n",
            name, words_read, PARTITION_SIZE);
        return -1;
    }

    /* 5. Inicializar PCB en estado NEW */
    PCB_t *p = &process_table[slot];
    p->pid             = slot;
    strncpy(p->name, name, 63);
    p->name[63]        = '\0';
    p->disk_offset     = disk_off;
    p->prog_size       = words_read;
    p->entry_point     = entry_point;
    p->partition_id    = -1;   /* Se asigna al pasar a READY */
    p->base            = -1;
    p->limit           = -1;
    p->quantum_counter = 0;
    p->wake_tick       = 0;
    memset(&p->ctx, 0, sizeof(ProcessContext_t));

    /* Registrar en NEW primero (sin estado previo, usamos TERMINATED como ficticio) */
    process_table[slot].state = STATE_TERMINATED; /* valor base para el log */
    process_change_state(slot, STATE_NEW);

    char msg[256];
    snprintf(msg, sizeof(msg),
            "KERNEL: Proceso creado. PID=%d, Nombre='%s', "
            "Disco offset=%d, Palabras=%d, EntryPoint=%d.",
            slot, name, disk_off, words_read, entry_point);
    escribir_log(msg);
    printf("Proceso '%s' creado (PID=%d, %d palabras, entry=%d).\n",
            name, slot, words_read, entry_point);

    /* 6. Intentar pasar a READY si hay memoria disponible */
    int part = process_find_partition();
    if (part == -1) {
        /* Sin memoria ahora: queda en NEW, el scheduler lo admitirá cuando haya hueco */
        char m2[128];
        snprintf(m2, sizeof(m2),
            "KERNEL: PID=%d queda en NEW (sin partición de RAM libre ahora).", slot);
        escribir_log(m2);
        printf("PID=%d queda en NEW (sin RAM libre). Se admitirá cuando se libere memoria.\n", slot);
    } else {
        /* Hay partición libre → asignar y pasar a READY */
        partition_bitmap[part] = 1;
        p->partition_id = part;
        p->base  = OS_RESERVED + (part * PARTITION_SIZE);
        p->limit = p->base + PARTITION_SIZE - 1;

        /* Cargar de disco a RAM */
        process_load_to_ram(slot);

        /* Configurar contexto inicial */
        p->ctx.pc        = entry_point;
        p->ctx.rb        = p->base;
        p->ctx.rl        = p->limit;
        p->ctx.sp        = PARTITION_SIZE - 1; /* último slot válido (base 0); primer PSH escribe aquí */
        p->ctx.ac_sign   = 0;
        p->ctx.ac_value  = 0;
        p->ctx.rx        = 0;
        p->ctx.condition = 0;
        p->ctx.mode      = MODE_USER;
        p->ctx.interrupt = 1;

        /* Inicializar pila del sistema para este proceso */
        p->saved_system_sp = OS_STACK_TOP;

        process_change_state(slot, STATE_READY);
    }

    return slot;
}

/* ============================================================
 * SALVAR CONTEXTO: CPU → PCB
 * ============================================================ */
void process_save_context(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return;
    PCB_t *p = &process_table[pid];

    p->ctx.ac_sign   = cpu.AC.sign;
    p->ctx.ac_value  = cpu.AC.value;
    p->ctx.pc        = cpu.PSW.pc;
    p->ctx.rx        = cpu.stack.rx;
    p->ctx.sp        = cpu.stack.sp;
    p->ctx.rb        = cpu.mp.base;
    p->ctx.rl        = cpu.mp.limit;
    p->ctx.condition = cpu.PSW.condition;
    p->ctx.mode      = cpu.PSW.mode;
    p->ctx.interrupt = cpu.PSW.interrupt;

    /* Persistencia de la Pila del Sistema */
    p->saved_system_sp = cpu.system_sp;
    int words_in_stack = OS_STACK_TOP - cpu.system_sp;
    if (words_in_stack > 0 && words_in_stack <= 20) {
        for (int i = 0; i < words_in_stack; i++) {
            p->saved_system_stack[i] = RAM[cpu.system_sp + 1 + i];
        }
    }
}

/* ============================================================
 * SALVAR CONTEXTO DESDE INTERRUPCIÓN: Pila del Sistema → PCB
 *
 * Cuando lanzarInterrupcion() guarda el contexto del usuario en la
 * pila del sistema (RAM[30-299]) y cambia a Modo Kernel, los 
 * registros de la CPU ya NO contienen los valores del usuario.
 * Esta función los recupera desde la pila (sysPop) y los guarda
 * en el PCB para que el proceso despierte correctamente.
 *
 * Orden de Push (en lanzarInterrupcion): PC, AC, RX, RB, RL, CC, Mode
 * Orden de Pop (inverso):                Mode, CC, RL, RB, RX, AC, PC
 * ============================================================ */
void process_save_context_from_interrupt(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return;
    PCB_t *p = &process_table[pid];

    int temp;
    sysPop(&temp); p->ctx.mode      = temp;
    sysPop(&temp); p->ctx.condition = temp;
    sysPop(&temp); p->ctx.rl        = temp;
    sysPop(&temp); p->ctx.rb        = temp;
    sysPop(&temp); p->ctx.rx        = temp;
    sysPop(&temp);
    p->ctx.ac_sign  = (temp < 0) ? 1 : 0;
    p->ctx.ac_value = (temp < 0) ? -temp : temp;
    sysPop(&temp); p->ctx.pc        = temp;

    /* SP no se guarda en la pila del sistema; lo tomamos tal cual */
    p->ctx.sp        = cpu.stack.sp;
    /* Al restaurar, las interrupciones deben volver a estar ON */
    p->ctx.interrupt = 1;
}

/* ============================================================
 * RESTAURAR CONTEXTO: PCB → CPU
 * ============================================================ */
void process_load_context(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return;
    PCB_t *p = &process_table[pid];

    cpu.AC.sign      = p->ctx.ac_sign;
    cpu.AC.value     = p->ctx.ac_value;
    cpu.PSW.pc       = p->ctx.pc;
    cpu.stack.rx     = p->ctx.rx;
    cpu.stack.sp     = p->ctx.sp;
    cpu.mp.base      = p->ctx.rb;
    cpu.mp.limit     = p->ctx.rl;
    cpu.PSW.condition= p->ctx.condition;
    cpu.PSW.mode     = p->ctx.mode;
    cpu.PSW.interrupt= p->ctx.interrupt;

    /* Restaurar Pila del Sistema */
    cpu.system_sp = p->saved_system_sp;
    int words_in_stack = OS_STACK_TOP - cpu.system_sp;
    if (words_in_stack > 0 && words_in_stack <= 20) {
        for (int i = 0; i < words_in_stack; i++) {
            RAM[cpu.system_sp + 1 + i] = p->saved_system_stack[i];
        }
    }

    cpu.halted        = 0;
}

/* ============================================================
 * IMPRIMIR TABLA DE PROCESOS (comando 'ps')
 * ============================================================ */
void process_print_table(void) {
    printf(ANSI_FG_B_BLUE "\n=== TABLA DE PROCESOS (tick=%d) ===\n" ANSI_RESET, system_ticks);
    printf(ANSI_FG_CYAN "%-4s %-20s %-14s %-6s %-6s %-6s %-5s\n" ANSI_RESET,
        "PID", "Nombre", "Estado", "Base", "Lím", "PC", "Q");
    printf("------------------------------------------------------------\n");

    int encontrado = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == -1) continue;
        if (process_table[i].state == STATE_TERMINATED &&
            process_table[i].name[0] == '\0') continue;

        printf("%-4d %-20s %-14s %-6d %-6d %-6d %-5d\n",
            i,
            process_table[i].name,
            state_to_string(process_table[i].state),
            process_table[i].base,
            process_table[i].limit,
            process_table[i].ctx.pc,
            process_table[i].quantum_counter);
        encontrado = 1;
    }
    if (!encontrado)
        printf("(No hay procesos registrados)\n");
    printf("====================================\n\n");
}

/* ============================================================
 * KERNEL POP STACK — Lee un parámetro de la pila del usuario
 *
 * Durante una interrupción (estamos en Modo Kernel), no podemos
 * usar leerMemoria() porque la MMU traduce diferente en Kernel.
 * Accedemos directamente a la dirección física: base + sp.
 *
 * Inspirado en el diseño de Paccaneglla: encapsula el acceso
 * al stack del proceso sin tocar los registros reales de la CPU
 * más allá de SP.
 *
 * Retorna 0 si éxito, -1 si error.
 * ============================================================ */
int kernel_pop_stack(int pid, int *value) {
    if (pid < 0 || pid >= MAX_PROCESSES) return -1;

    int sp   = cpu.stack.sp;   /* SP lógico del usuario          */
    int base = cpu.mp.base;    /* Base física de su partición     */
    int phys = base + sp;      /* Dirección física real en RAM    */

    /* Validar que la dirección física está dentro de la RAM */
    if (phys < 0 || phys >= RAM_SIZE) {
        printf("[KERNEL] ERROR: kernel_pop_stack dirección inválida %d\n", phys);
        return -1;
    }

    /* Leer el valor en signo-magnitud y convertir a entero C */
    *value = obtenerValorReal(RAM[phys]);

    /* Actualizar SP del proceso (pop = mover SP hacia arriba) */
    cpu.stack.sp++;

    return 0; /* Éxito */
}
