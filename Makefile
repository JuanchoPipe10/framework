# Build target: 'pc' or 'ultra96'
TARGET_ARCH ?= pc

ifeq ($(TARGET_ARCH), ultra96)
    CC = aarch64-linux-gnu-gcc
else
    CC = gcc
endif

CFLAGS  = -pthread -Wall -Wextra -O2 -Iinclude
ifeq ($(TARGET_ARCH), ultra96)
    LDFLAGS = -pthread
else
    LDFLAGS = -pthread
endif

SRC_DIR = src
BIN_DIR = bin
CFG_DIR = config

SRC = $(SRC_DIR)/main.c \
      $(SRC_DIR)/tcp_comm.c \
      $(SRC_DIR)/device_registry.c \
      $(SRC_DIR)/framework_core.c \
      $(SRC_DIR)/udp_discovery.c

OBJ    = $(SRC:.c=.o)
TARGET = $(BIN_DIR)/framework

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(CFG_DIR)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Build complete: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
	rm -f $(SRC_DIR)/*.o
	@echo "Clean complete"