# Compilador y opciones
CC = gcc
CFLAGS = -Wall -Wextra -g -I./include -pthread

# Nombre del ejecutable
TARGET = arquitectura_virtual


SRCS = src/architecture.c src/main.c src/log.c src/cpu.c src/dma.c \
       src/process.c src/scheduler.c

# Archivos objeto 
OBJS = src/architecture.o src/main.o src/log.o src/cpu.o src/dma.o \
       src/process.o src/scheduler.o

#compilar todo el proyecto
all: $(TARGET)

# Enlazar los archivos objeto para crear el ejecutable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compilar architecture.c
src/architecture.o: src/architecture.c
	$(CC) $(CFLAGS) -c src/architecture.c -o src/architecture.o

# Compilar main.c
src/main.o: src/main.c
	$(CC) $(CFLAGS) -c src/main.c -o src/main.o

# Compilar log.c
src/log.o: src/log.c
	$(CC) $(CFLAGS) -c src/log.c -o src/log.o

# Compilar cpu.c
src/cpu.o: src/cpu.c
	$(CC) $(CFLAGS) -c src/cpu.c -o src/cpu.o

# Compilar dma.c
src/dma.o: src/dma.c
	$(CC) $(CFLAGS) -c src/dma.c -o src/dma.o

# Compilar process.c (Fase 2)
src/process.o: src/process.c
	$(CC) $(CFLAGS) -c src/process.c -o src/process.o

# Compilar scheduler.c (Fase 2)
src/scheduler.o: src/scheduler.c
	$(CC) $(CFLAGS) -c src/scheduler.c -o src/scheduler.o

# Limpiar archivos objeto y ejecutable
clean:
	rm -f $(TARGET) src/*.o

# Ejecutar el programa
run: $(TARGET)
	./$(TARGET)