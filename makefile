CC = gcc
CFLAGS = -Wall -Iinclude -pthread

SRC = src/main.c src/architecture.c
OBJ = $(SRC:.c=.o)

TARGET = arquitectura_virtual

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(CFLAGS)

src/%.o: src/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)
