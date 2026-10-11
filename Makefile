# Box de Mr. Meeseeks — C / POSIX / Linux (también compila en macOS)
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS = -pthread

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -lrt
endif
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
endif

SRC = src/main.c src/sync.c src/ipc.c src/meeseeks.c src/calc.c src/util.c
OBJ = $(SRC:.c=.o)
BIN = meeseeks_box

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

src/%.o: src/%.c include/meeseeks_box.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)
