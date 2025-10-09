CC = gcc
CFLAGS = -pthread -Wall -Iinclude
SRC = src/main.c src/udp_discover.c src/tcp_comm.c
OBJ = $(SRC:.c=.o)
BIN = bin/framework

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^
	@echo "✓ Compilación exitosa: $(BIN)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)
	rm -f src/*.o
	@echo "✓ Limpieza completa"

.PHONY: all clean