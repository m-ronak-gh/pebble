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

.PHONY: all smoke codegen-test verify native-test test clean

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
	echo "PASS: native executable returned $(EXPECTED_EXIT)"

test: smoke native-test

clean:
	rm -f $(SMOKE) pebble-smoke.o pebble-smoke-native \
		$(CODEGEN_TEST_BIN) $(CODEGEN_IR) $(CODEGEN_EXE)