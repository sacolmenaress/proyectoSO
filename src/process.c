/*
 * Mantiene la tabla de procesos, maneja los cambios de estado
 * y salva/restaura contextos usando la CPU_t global de architecture.h.
 */

#include "process.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * DEFINICIÓN DE VARIABLES GLOBALES
 * ============================================================ */
PCB_t process_table[MAX_PROCESSES];
int   current_pid   = -1;
int   system_ticks  =  0;
int   partition_bitmap[NUM_PARTITIONS];

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

    char msg[256];
    snprintf(msg, sizeof(msg),
            "PROCESO [PID=%d] (%s): %s --> %s  (tick=%d)",
            pid,
            process_table[pid].name,
            state_to_string(old_state),
            state_to_string(new_state),
            system_ticks);
    escribir_log(msg);
    printf("[PROCESO] PID=%d (%s): %s -> %s\n",
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
        printf("ERROR: No se pudo abrir el archivo '%s'.\n", filename);
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
        p->ctx.sp        = PARTITION_SIZE; /* relativo al inicio de partición, primer psh irá a size-1 */
        p->ctx.ac_sign   = 0;
        p->ctx.ac_value  = 0;
        p->ctx.rx        = 0;
        p->ctx.condition = 0;

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

    /* Siempre arranca en modo usuario con interrupciones habilitadas */
    cpu.PSW.mode      = MODE_USER;
    cpu.PSW.interrupt = 1;
    cpu.halted        = 0;
}

/* ============================================================
 * IMPRIMIR TABLA DE PROCESOS (comando 'ps')
 * ============================================================ */
void process_print_table(void) {
    printf("\n=== TABLA DE PROCESOS (tick=%d) ===\n", system_ticks);
    printf("%-4s %-20s %-14s %-6s %-6s %-6s %-5s\n",
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
