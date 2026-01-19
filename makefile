
CC = gcc
CFLAGS = -Wall -Wextra -g -I./include

# Detectar sistema operativo
ifeq ($(OS),Windows_NT)
    RM = del /f /q
    TARGET = arquitectura_virtual.exe
    PATH_SEP = \\
    CLEAN_CMD = if exist $(TARGET) del /f /q $(TARGET) & if exist src\\*.o del /f /q src\\*.o
else
    RM = rm -f
    TARGET = arquitectura_virtual
    PATH_SEP = /
    CLEAN_CMD = rm -f $(TARGET) src/*.o
endif

# Archivos fuente en src/
SRCS = src/architecture.c src/main.c src/log.c src/cpu.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compilar cada .c en su .o
src/architecture.o: src/architecture.c include/architecture.h include/log.h
	$(CC) $(CFLAGS) -c src/architecture.c -o src/architecture.o

src/main.o: src/main.c include/architecture.h include/log.h
	$(CC) $(CFLAGS) -c src/main.c -o src/main.o

src/log.o: src/log.c include/log.h include/architecture.h
	$(CC) $(CFLAGS) -c src/log.c -o src/log.o

src/cpu.o: src/cpu.c include/cpu.h include/log.h
	$(CC) $(CFLAGS) -c src/cpu.c -o src/cpu.o

clean:
ifeq ($(OS),Windows_NT)
	@if exist $(TARGET) del /f /q $(TARGET)
	@if exist src\\*.o del /f /q src\\*.o
else
	@rm -f $(TARGET)
	@rm -f src/*.o
endif

run: $(TARGET)
	./$(TARGET)