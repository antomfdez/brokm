# brokm - build, test, and clean targets.
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
CFLAGS  += -Iinclude -Isrc
LDFLAGS ?=

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := brokm

.PHONY: all debug test clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Debug build: assertions, sanitizers, and bytecode/exec tracing.
debug: CFLAGS += -O0 -g -fsanitize=address,undefined \
	-DBK_DEBUG_PRINT_CODE -DBK_DEBUG_TRACE_EXECUTION
debug: clean $(BIN)

test: $(BIN)
	@bash tests/run_tests.sh

clean:
	rm -f $(OBJ) $(BIN)
