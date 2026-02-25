#include "architecture.h"
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>

pthread_cond_t  dma_cond = PTHREAD_COND_INITIALIZER;
pthread_t       dma_thread;

void* dma_thread_func(void* arg) {
    while (1) {
        pthread_mutex_lock(&bus_mutex);
        
        // Esperar a que haya una transferencia pendiente
        while (!cpu.dma.pending) {
            pthread_cond_wait(&dma_cond, &bus_mutex);
        }
        
        // Iniciar transferencia
        cpu.dma.status = 0; // Éxito por defecto
        
        int disk_addr = (cpu.dma.cylinder * DISK_TRACKS * DISK_SECTORS) +
                        (cpu.dma.track * DISK_SECTORS) + cpu.dma.sector;
        
        // Simular tiempo de búsqueda del disco (50-150ms)
        pthread_mutex_unlock(&bus_mutex);
        usleep(50000); 
        pthread_mutex_lock(&bus_mutex);

        if (disk_addr >= DISK_SIZE || disk_addr < 0) {
            cpu.dma.status = 1;
        } else {
            if (cpu.dma.io_type == 0) { // LEER de disco a RAM
                Word data = DISK[disk_addr];
                // Intentar escribir en RAM (esto puede fallar por protección MMU, 
                // pero el DMA suele tener acceso privilegiado o se valida antes)
                int dirReal = cpu.dma.mem_addr;
                if (cpu.PSW.mode == MODE_USER) dirReal += cpu.mp.base;

                if (dirReal >= 0 && dirReal < RAM_SIZE) {
                    RAM[dirReal] = data;
                    cpu.dma.status = 0;
                } else {
                    cpu.dma.status = 1;
                }
            } else { // ESCRIBIR de RAM a disco
                int dirReal = cpu.dma.mem_addr;
                if (cpu.PSW.mode == MODE_USER) dirReal += cpu.mp.base;

                if (dirReal >= 0 && dirReal < RAM_SIZE) {
                    Word data = RAM[dirReal];
                    DISK[disk_addr] = data;
                    cpu.dma.status = 0;
                } else {
                    cpu.dma.status = 1;
                }
            }
        }

        cpu.dma.pending = 0;
        
        // Disparar interrupción de fin de E/S
        // IMPORTANTE: lanzarInterrupcion requiere el mutex? 
        // Sí, porque modifica registros del CPU.
        lanzarInterrupcion(INT_IO_COMPLETE);
        
        pthread_mutex_unlock(&bus_mutex);
    }
    return NULL;
}

void inicializarDMAThread(void) {
    pthread_create(&dma_thread, NULL, dma_thread_func, NULL);
}
