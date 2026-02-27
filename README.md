# Proyecto Sistemas Operativos – Arquitectura Virtual

Proyecto de la materia **Sistemas Operativos**, desarrollado en **lenguaje C**, que utiliza un **Makefile** para la compilación y ejecución del programa.

---

## Descripción

Este proyecto implementa una arquitectura virtual básica (Mini Kernel), aplicando conceptos
fundamentales vistos en la materia Sistemas Operativos, como organización del
código, compilación y ejecución desde consola.

---

## Para ejecutar la máquina virtual

Al clonar el repositorio, si deseas usar el **Makefile** para ejecutar la máquina virtual,
primero debes instalar las dependencias necesarias.

### Dependencias

```bash
sudo apt update && sudo apt install make gcc
```

> Proyecto desarrollado y probado en sistemas Linux.

## Compilación y ejecución 

Desde la raíz del proyecto, ejecutar:  
``` bash
make
```

Esto compilará el proyecto y generará el ejecutable correspondiente.

Una vez compilado, ejecutar: 
``` bash
./arquitectura_virtual
```

Durante la ejecución, el programa puede generar información que se guarda en el archivo log.txt.


## Limpiar archivos compilados
``` bash
make clean
```

## Estructura del proyecto
``` bash
ProyectoSO/
├── include/
│   ├── architecture.h    ← Tipos (Word, CPU_t), constantes, prototipos
│   ├── process.h         ← ProcessState, ProcessContext_t, PCB_t, prototipos
│   ├── scheduler.h       ← scheduler_tick, handle_terminate, handle_sleep
│   ├── log.h             ← Funciones de logging
│   └── cpu.h             ← Prototipo de inicializar_cpu (legacy)
├── src/
│   ├── architecture.c    ← Ciclo instrucción, MMU, interrupciones, pila sistema
│   ├── process.c         ← Gestión de procesos, cola de listos, contextos
│   ├── scheduler.c       ← Planificador RR, dispatch, kernel_interrupt_handler
│   ├── main.c            ← CLI, comandos load/run/step/ps
│   ├── log.c             ← Escritura en log.txt
│   ├── dma.c             ← Hilo DMA asíncrono
│   └── cpu.c             ← Wrapper de inicialización (legacy)
├── CasosPrueba/          ← Archivos de test (.txt con instrucciones)
├── makefile
└── log.txt               ← Registro de ejecución
```

## Tecnologías utilizadas

- Lenguaje C
- GCC
- Makefile


