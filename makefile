CC = gcc
CLANG = clang
OPT = opt

CFLAGS = -std=c17 -Wall -Wextra -Wpedantic

LLVM_CFLAGS = $(shell llvm-config --cflags)
LLVM_LDFLAGS = $(shell llvm-config --ldflags)
LLVM_LIBS = $(shell llvm-config --libs core analysis native)

CFLAGS += $(LLVM_CFLAGS)
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS)

PBIT = pbit
DRIVER_SOURCES = \
    compiler/main.c \
    compiler/codegen/codegen.c \
    compiler/semantic/semantic.c \
    compiler/parser/parser.c \
    compiler/ast/ast.c \
    compiler/lexer/lexer.c

CODEGEN_TEST_BIN = pbit-codegen-test
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

.PHONY: all codegen-test verify native-test test clean \
        multidim-test bounds-test primitive-array-tests \
        semantic-bounds-test lexer-test parser-test semantic-test \
        unit-tests driver-test package install uninstall

all: pbit

pbit: $(DRIVER_SOURCES)
	$(CC) $(CFLAGS) $(DRIVER_SOURCES) $(LDFLAGS) -o $(PBIT)

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

LEXER_TEST_BIN = pebblebit-lexer-test
PARSER_TEST_BIN = pebblebit-parser-test
SEMANTIC_TEST_BIN = pebblebit-semantic-test

lexer-test:
	$(CC) $(CFLAGS) compiler/lexer/lexer.c compiler/lexer/lexer_test.c -o $(LEXER_TEST_BIN)
	./$(LEXER_TEST_BIN)
	@rm -f $(LEXER_TEST_BIN)

parser-test:
	$(CC) $(CFLAGS) compiler/lexer/lexer.c compiler/ast/ast.c compiler/parser/parser.c compiler/parser/parser-test.c -o $(PARSER_TEST_BIN)
	./$(PARSER_TEST_BIN)
	@rm -f $(PARSER_TEST_BIN)

semantic-test:
	$(CC) $(CFLAGS) compiler/lexer/lexer.c compiler/ast/ast.c compiler/parser/parser.c compiler/semantic/semantic.c compiler/semantic/semantic-test.c -o $(SEMANTIC_TEST_BIN)
	./$(SEMANTIC_TEST_BIN)
	@rm -f $(SEMANTIC_TEST_BIN)

unit-tests: lexer-test parser-test semantic-test

test: unit-tests native-test primitive-array-tests multidim-test bounds-test semantic-bounds-test driver-test
driver-test: pbit
	bash ./tests/driver_test.sh ./pbit

PLATFORM ?= $(shell uname -s | tr '[:upper:]' '[:lower:]')-$(shell uname -m)
DIST_DIR = dist/pebblebit-$(PLATFORM)
PREFIX ?= /usr/local

package: pbit
	rm -rf $(DIST_DIR) dist/pebblebit-$(PLATFORM).tar.gz
	mkdir -p $(DIST_DIR)/bin $(DIST_DIR)/runtime $(DIST_DIR)/lib/pebblebit
	cp $(PBIT) $(DIST_DIR)/bin/
	cp runtime/pebblebit_runtime.c runtime/pebblebit_runtime.h $(DIST_DIR)/runtime/
	cp -R stdlib $(DIST_DIR)/lib/pebblebit/std
	cp README.md pebblebit-language-spec.md $(DIST_DIR)/
	tar -czf dist/pebblebit-$(PLATFORM).tar.gz -C dist pebblebit-$(PLATFORM)
	@echo "Package created: dist/pebblebit-$(PLATFORM).tar.gz"

install: pbit
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(PBIT) $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)$(PREFIX)/lib/pebblebit
	install -m 644 runtime/pebblebit_runtime.c runtime/pebblebit_runtime.h $(DESTDIR)$(PREFIX)/lib/pebblebit/
	install -d $(DESTDIR)$(PREFIX)/lib/pebblebit/std
	install -m 644 stdlib/*.pb $(DESTDIR)$(PREFIX)/lib/pebblebit/std/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PBIT)
	rm -rf $(DESTDIR)$(PREFIX)/lib/pebblebit

clean:
	rm -rf $(PBIT) \
		$(CODEGEN_TEST_BIN) $(CODEGEN_IR) $(CODEGEN_EXE) \
		$(LEXER_TEST_BIN) $(PARSER_TEST_BIN) $(SEMANTIC_TEST_BIN) \
		dist