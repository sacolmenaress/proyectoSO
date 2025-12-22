# Makefile para Arquitectura Virtual
# Estructura: src/*.c, include/*.h

CC = gcc
CFLAGS = -Wall -Wextra -g -I./include
TARGET = arquitectura_virtual.exe

# Archivos fuente en src/
SRCS = src/architecture.c src/main.c src/log.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compilar cada .c en su .o
src/architecture.o: src/architecture.c include/architecture.h include/log.h
	$(CC) $(CFLAGS) -c src/architecture.c -o src/architecture.o

src/main.o: src/main.c include/architecture.h include/log.h
	$(CC) $(CFLAGS) -c src/main.c -o src/main.o

src/log.o: src/log.c include/log.h
	$(CC) $(CFLAGS) -c src/log.c -o src/log.o

clean:
	rm -f $(TARGET) src/*.o

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run