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
.
├── include/                 # Archivos de cabecera
├── src/                     # Código fuente
├── makefile                 # Archivo de compilación
├── log.txt                  # Archivo de registro
└── arquitectura_virtual.exe # Ejecutable
```

## Tecnologías utilizadas

- Lenguaje C
- GCC
- Makefile


