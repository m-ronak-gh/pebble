CC = gcc
CLANG = clang
OPT = opt

CFLAGS = -std=c17 -Wall -Wextra -Wpedantic

LLVM_CFLAGS = $(shell llvm-config --cflags)
LLVM_LDFLAGS = $(shell llvm-config --ldflags)
LLVM_LIBS = $(shell llvm-config --libs core native)

CFLAGS += $(LLVM_CFLAGS)
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS)

SMOKE = pebble-smoke
CODEGEN_TEST_BIN = pebble-codegen-test
CODEGEN_IR = integration-test.ll
CODEGEN_EXE = integration-test
EXPECTED_EXIT = 33

CODEGEN_SOURCES = \
    compiler/codegen/codegen-test.c \
    compiler/codegen/codegen.c \
    compiler/semantic/semantic.c \
    compiler/parser/parser.c \
    compiler/ast/ast.c \
    compiler/lexer/lexer.c

.PHONY: all smoke codegen-test verify native-test test clean multidim-test bounds-test primitive-array-tests
all: smoke

smoke:
	$(CC) $(CFLAGS) compiler/codegen/smoke.c $(LDFLAGS) -o $(SMOKE)

$(CODEGEN_TEST_BIN): $(CODEGEN_SOURCES)
	$(CC) $(CFLAGS) $(CODEGEN_SOURCES) $(LDFLAGS) -o $(CODEGEN_TEST_BIN)

codegen-test: $(CODEGEN_TEST_BIN)
	./$(CODEGEN_TEST_BIN)

verify: codegen-test
	$(OPT) -passes=verify -disable-output $(CODEGEN_IR)

native-test: verify
	$(CLANG) $(CODEGEN_IR) -o $(CODEGEN_EXE)
	@./$(CODEGEN_EXE); status=$$?; \
	if [ $$status -ne $(EXPECTED_EXIT) ]; then \
		echo "FAIL: expected exit $(EXPECTED_EXIT), got $$status"; \
		exit 1; \
	fi; \
	echo "PASS: valid array access returned $(EXPECTED_EXIT)"

primitive-array-tests: $(CODEGEN_TEST_BIN)
	@for mode in decimal-array bool-array char-array zero-array; do \
		expected=$(EXPECTED_EXIT); \
		if [ "$$mode" = "zero-array" ]; then expected=0; fi; \
		echo "Testing primitive array: $$mode (expected exit $$expected)"; \
		./$(CODEGEN_TEST_BIN) $$mode || exit 1; \
		$(OPT) -passes=verify -disable-output $(CODEGEN_IR) || exit 1; \
		$(CLANG) $(CODEGEN_IR) -o $(CODEGEN_EXE) || exit 1; \
		./$(CODEGEN_EXE); status=$$?; \
		if [ $$status -ne $$expected ]; then \
			echo "FAIL: $$mode expected $$expected, got $$status"; \
			exit 1; \
		fi; \
		echo "PASS: $$mode returned $$status"; \
	done

multidim-test: $(CODEGEN_TEST_BIN)
	@./$(CODEGEN_TEST_BIN) multi
	$(OPT) -passes=verify -disable-output $(CODEGEN_IR)
	$(CLANG) $(CODEGEN_IR) -o $(CODEGEN_EXE)
	@./$(CODEGEN_EXE); status=$$?; \
	if [ $$status -ne $(EXPECTED_EXIT) ]; then \
		echo "FAIL: multidimensional test expected $(EXPECTED_EXIT), got $$status"; \
		exit 1; \
	fi; \
	echo "PASS: multidimensional array access returned $(EXPECTED_EXIT)"

bounds-test: $(CODEGEN_TEST_BIN)
	@mode=upper; \
	echo "Testing runtime bounds: $$mode"; \
	./$(CODEGEN_TEST_BIN) $$mode || exit 1; \
	$(OPT) -passes=verify -disable-output $(CODEGEN_IR) || exit 1; \
	$(CLANG) $(CODEGEN_IR) -o $(CODEGEN_EXE) || exit 1; \
	(ulimit -c 0; ./$(CODEGEN_EXE) >/dev/null 2>&1); \
	status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo "FAIL: $$mode unexpectedly succeeded"; \
		exit 1; \
	fi; \
	echo "PASS: dynamic out-of-bounds access terminated (status $$status)"

semantic-bounds-test: $(CODEGEN_TEST_BIN)
	@for mode in negative upper-literal; do \
		echo "Testing compile-time bounds rejection: $$mode"; \
		if ./$(CODEGEN_TEST_BIN) $$mode >semantic-bounds-test.log 2>&1; then \
			echo "FAIL: $$mode unexpectedly passed semantic analysis"; \
			cat semantic-bounds-test.log; \
			rm -f semantic-bounds-test.log; \
			exit 1; \
		fi; \
		if ! grep -q "array index is outside the fixed array bounds" semantic-bounds-test.log; then \
			echo "FAIL: $$mode failed for an unexpected reason"; \
			cat semantic-bounds-test.log; \
			rm -f semantic-bounds-test.log; \
			exit 1; \
		fi; \
		echo "PASS: $$mode rejected during semantic analysis"; \
	done; \
	rm -f semantic-bounds-test.log

test: smoke native-test

clean:
	rm -f $(SMOKE) pebble-smoke.o pebble-smoke-native \
		$(CODEGEN_TEST_BIN) $(CODEGEN_IR) $(CODEGEN_EXE)