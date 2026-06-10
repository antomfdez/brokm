# brokm - build, test, and clean targets.
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
CFLAGS  += -Iinclude -Isrc
LDFLAGS ?=
LDFLAGS += -lm   # libm for the math stdlib (no-op on macOS, required on Linux)

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := brokm

.PHONY: all debug test bench embed test-embed test-selfcompile test-bootstrap \
	test-fixpoint clean

# Embedding demo: the library sources (minus the CLI main) + the host program.
EMBED_SRC := $(filter-out src/main.c,$(SRC))

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

# Run the whole suite with the collector firing on every allocation. If marking
# is wrong, a live object gets swept and a test crashes or misbehaves.
test-gc: clean
	$(CC) $(CFLAGS) -DBK_DEBUG_STRESS_GC -o $(BIN) $(SRC) $(LDFLAGS)
	@bash tests/run_tests.sh
	@$(MAKE) --no-print-directory clean

# Build the C embedding example (examples/embed.c) into ./embed-demo.
embed: examples/embed.c $(EMBED_SRC)
	$(CC) $(CFLAGS) -o embed-demo examples/embed.c $(EMBED_SRC) $(LDFLAGS)

# Run the embedding example and compare to its golden output.
test-embed: embed
	@./embed-demo > /tmp/brokm-embed.out 2>&1; \
	if diff examples/embed.expected /tmp/brokm-embed.out >/dev/null; then \
	  echo "embed: OK"; \
	else \
	  echo "embed: FAIL"; diff examples/embed.expected /tmp/brokm-embed.out; exit 1; \
	fi

# Self-hosting check: the in-brokm compiler (examples/realcc.bk) must agree with
# the C compiler. Run a real brokm program (examples/sample.bk) directly, then
# read + compile + run the same file via the in-brokm compiler, and diff.
test-selfcompile: $(BIN)
	@./brokm examples/sample.bk > /tmp/brokm-direct.out 2>&1; \
	./brokm examples/selfcompile.bk > /tmp/brokm-selfcc.out 2>&1; \
	if diff /tmp/brokm-direct.out /tmp/brokm-selfcc.out >/dev/null; then \
	  echo "selfcompile: OK (C compiler == in-brokm compiler)"; \
	else \
	  echo "selfcompile: FAIL"; diff /tmp/brokm-direct.out /tmp/brokm-selfcc.out; exit 1; \
	fi

# Full bootstrap: realcc compiles its OWN source, and the self-compiled compiler
# must produce the same output for sample.bk as the C compiler. (examples/sample.bk
# run directly == the result of examples/bootstrap.bk, which two-stage self-hosts.)
test-bootstrap: $(BIN)
	@./brokm examples/sample.bk > /tmp/brokm-direct.out 2>&1; \
	./brokm examples/bootstrap.bk > /tmp/brokm-boot.out 2>&1; \
	if diff /tmp/brokm-direct.out /tmp/brokm-boot.out >/dev/null; then \
	  echo "bootstrap: OK (C compiler == realcc-compiled realcc)"; \
	else \
	  echo "bootstrap: FAIL"; diff /tmp/brokm-direct.out /tmp/brokm-boot.out; exit 1; \
	fi

# Three-stage bootstrap fixpoint: realcc compiles realcc (stage 1), the result
# compiles realcc again (stage 2), and the two compiled compilers must be
# byte-for-byte identical bytecode (examples/fixpoint.bk diffs them in-language).
test-fixpoint: $(BIN)
	@out=$$(./brokm examples/fixpoint.bk 2>&1); \
	if [ "$$out" = "fixpoint: 1" ]; then \
	  echo "fixpoint: OK (stage-1 == stage-2 bytecode)"; \
	else \
	  echo "fixpoint: FAIL"; echo "$$out"; exit 1; \
	fi

# Benchmark the JIT against the interpreter on a recursion-heavy program.
bench:
	$(CC) $(CFLAGS) -o /tmp/brokm-jit $(SRC) $(LDFLAGS)
	$(CC) $(CFLAGS) -DBK_NO_JIT -o /tmp/brokm-nojit $(SRC) $(LDFLAGS)
	@echo "interpreter:"; time /tmp/brokm-nojit bench/fib.bk
	@echo "JIT:";         time /tmp/brokm-jit   bench/fib.bk

clean:
	rm -f $(OBJ) $(BIN) embed-demo
