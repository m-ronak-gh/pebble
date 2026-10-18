#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ast/ast.h"
#include "codegen/codegen.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semantic/semantic.h"

static void usage(const char *program)
{
    (void)program;
    fprintf(stderr,
        "Usage:\n"
        "  peb <source.pb|source.peb> [--keep-ir]\n"
        "  peb build <source.pb|source.peb> [-o output]\n"
        "  peb run <source.pb|source.peb> [-o output]\n");
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "peb: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "peb: cannot seek '%s'\n", path);
        fclose(file);
        return NULL;
    }

    long end = ftell(file);
    if (end < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "peb: cannot determine size of '%s'\n", path);
        fclose(file);
        return NULL;
    }

    size_t size = (size_t)end;
    char *buffer = malloc(size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "peb: out of memory reading '%s'\n", path);
        fclose(file);
        return NULL;
    }

    size_t got = fread(buffer, 1, size, file);
    if (got != size || ferror(file)) {
        fprintf(stderr, "peb: failed reading '%s'\n", path);
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

static char *default_output_name(const char *source)
{
    size_t length = strlen(source);
    size_t stem_length = length;
    const char *slash = strrchr(source, '/');
    const char *dot = strrchr(source, '.');
    if (dot != NULL && (slash == NULL || dot > slash)) {
        stem_length = (size_t)(dot - source);
    }

    char *output = malloc(stem_length + 1);
    if (output == NULL) return NULL;
    memcpy(output, source, stem_length);
    output[stem_length] = '\0';
    if (output[0] == '\0') {
        free(output);
        return NULL;
    }
    return output;
}

/* Quote a path for a POSIX shell command. Embedded single quotes become '\'' . */
static char *shell_quote(const char *text)
{
    size_t needed = 3;
    for (const char *p = text; *p; ++p) {
        needed += (*p == '\'') ? 4 : 1;
    }

    char *quoted = malloc(needed);
    if (quoted == NULL) return NULL;
    char *out = quoted;
    *out++ = '\'';
    for (const char *p = text; *p; ++p) {
        if (*p == '\'') {
            memcpy(out, "'\\''", 4);
            out += 4;
        } else {
            *out++ = *p;
        }
    }
    *out++ = '\'';
    *out = '\0';
    return quoted;
}

static int run_program(const char *path)
{
    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "peb: cannot start executable: %s\n", strerror(errno));
        return 1;
    }
    if (child == 0) {
        execl(path, path, (char *)NULL);
        fprintf(stderr, "peb: cannot execute '%s': %s\n", path, strerror(errno));
        _exit(127);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        fprintf(stderr, "peb: waitpid failed: %s\n", strerror(errno));
        return 1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static int emit_object(LLVMModuleRef module, const char *object_path)
{
    if (LLVMInitializeNativeTarget() != 0 ||
        LLVMInitializeNativeAsmPrinter() != 0) {
        fprintf(stderr, "peb: failed to initialize LLVM native target\n");
        return 0;
    }

    char *triple = LLVMGetDefaultTargetTriple();
    if (triple == NULL) {
        fprintf(stderr, "peb: LLVM could not determine the target triple\n");
        return 0;
    }

    LLVMTargetRef target = NULL;
    char *error = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
        fprintf(stderr, "peb: cannot select target: %s\n",
                error != NULL ? error : "unknown LLVM error");
        if (error != NULL) LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return 0;
    }

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple, "generic", "", LLVMCodeGenLevelDefault,
        LLVMRelocDefault, LLVMCodeModelDefault);
    if (machine == NULL) {
        fprintf(stderr, "peb: failed to create LLVM target machine\n");
        LLVMDisposeMessage(triple);
        return 0;
    }

    LLVMSetTarget(module, triple);
    LLVMTargetDataRef data = LLVMCreateTargetDataLayout(machine);
    char *layout = LLVMCopyStringRepOfTargetData(data);
    LLVMSetDataLayout(module, layout);

    int ok = 1;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error) != 0) {
        fprintf(stderr, "peb: generated invalid LLVM IR:\n%s\n",
                error != NULL ? error : "unknown verifier error");
        if (error != NULL) LLVMDisposeMessage(error);
        error = NULL;
        ok = 0;
    }

    if (ok && LLVMTargetMachineEmitToFile(
            machine, module, (char *)object_path, LLVMObjectFile, &error) != 0) {
        fprintf(stderr, "peb: object generation failed: %s\n",
                error != NULL ? error : "unknown LLVM error");
        if (error != NULL) LLVMDisposeMessage(error);
        ok = 0;
    }

    if (layout != NULL) LLVMDisposeMessage(layout);
    LLVMDisposeTargetData(data);
    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(triple);
    return ok;
}

static int link_object(const char *object_path, const char *output_path)
{
    char *object_q = shell_quote(object_path);
    char *output_q = shell_quote(output_path);
    if (object_q == NULL || output_q == NULL) {
        fprintf(stderr, "peb: out of memory preparing linker command\n");
        free(object_q);
        free(output_q);
        return 0;
    }

    size_t length = strlen(object_q) + strlen(output_q) + 32;
    char *command = malloc(length);
    if (command == NULL) {
        free(object_q);
        free(output_q);
        return 0;
    }
    snprintf(command, length, "cc -no-pie %s -o %s", object_q, output_q);
    int status = system(command);
    free(command);
    free(object_q);
    free(output_q);

    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "peb: system linker failed\n");
        return 0;
    }
    return 1;
}

static int compile_source(const char *source_path, const char *output_path,
                          int keep_ir)
{
    char *source = read_file(source_path);
    if (source == NULL) return 1;

    Parser parser;
    parser_init(&parser, source);
    ASTNode *program = parser_parse_program(&parser);
    if (program == NULL) {
        fprintf(stderr, "peb: parsing failed; executable not produced\n");
        free(source);
        return 1;
    }

    SemanticContext *semantic = semantic_analyze(program);
    if (semantic == NULL) {
        fprintf(stderr, "peb: semantic analysis failed; executable not produced\n");
        ast_free(program);
        free(source);
        return 1;
    }

    ASTNode *main_function = NULL;
    size_t main_count = 0;
    for (size_t i = 0; i < program->data.program.statement_count; ++i) {
        ASTNode *node = program->data.program.statements[i];
        if (node == NULL || node->kind != AST_FUNCTION) continue;
        if (node->data.function_decl.name_length == 4 &&
            memcmp(node->data.function_decl.name, "main", 4) == 0) {
            main_function = node;
            ++main_count;
        }
        if (node->data.function_decl.parameter_count != 0) {
            fprintf(stderr,
                "peb: function '%.*s' has parameters, but parameter code generation is not implemented yet\n",
                (int)node->data.function_decl.name_length,
                node->data.function_decl.name);
            semantic_free(semantic);
            ast_free(program);
            free(source);
            return 1;
        }
    }

    if (main_count != 1 || main_function == NULL) {
        fprintf(stderr, "peb: program must define exactly one function number main()\n");
        semantic_free(semantic);
        ast_free(program);
        free(source);
        return 1;
    }
    if (main_function->data.function_decl.return_type.kind != AST_TYPE_NUMBER ||
        main_function->data.function_decl.return_type.dimension_count != 0) {
        fprintf(stderr, "peb: entry point must return number\n");
        semantic_free(semantic);
        ast_free(program);
        free(source);
        return 1;
    }

    Codegen *codegen = codegen_create("pebble_module", semantic);
    if (codegen == NULL) {
        fprintf(stderr, "peb: could not initialize code generator\n");
        semantic_free(semantic);
        ast_free(program);
        free(source);
        return 1;
    }

    for (size_t i = 0; i < program->data.program.statement_count; ++i) {
        ASTNode *node = program->data.program.statements[i];
        if (node != NULL && node->kind == AST_FUNCTION) {
            codegen_function(codegen, node);
        }
    }

    /* Do not emit or link partially generated LLVM IR. */
    if (codegen_has_error(codegen)) {
        fprintf(stderr,
                "peb: code generation failed; executable not produced\n");
        (void)remove(output_path);
        codegen_free(codegen);
        semantic_free(semantic);
        ast_free(program);
        free(source);
        return 1;
    }

    size_t output_len = strlen(output_path);
    char *object_path = malloc(output_len + 3);
    char *ir_path = malloc(output_len + 4);
    if (object_path == NULL || ir_path == NULL) {
        fprintf(stderr, "peb: out of memory creating output paths\n");
        free(object_path);
        free(ir_path);
        codegen_free(codegen);
        semantic_free(semantic);
        ast_free(program);
        free(source);
        return 1;
    }
    snprintf(object_path, output_len + 3, "%s.o", output_path);
    snprintf(ir_path, output_len + 4, "%s.ll", output_path);

    /* Never leave a stale executable that could be mistaken for this build. */
    (void)remove(output_path);
    int ok = 1;
    if (keep_ir) {
        if (!codegen_write_ir(codegen, ir_path)) ok = 0;
    }
    if (ok) ok = emit_object(codegen_module(codegen), object_path);
    if (ok) ok = link_object(object_path, output_path);
    (void)remove(object_path);
    if (!keep_ir) (void)remove(ir_path);

    if (ok) fprintf(stderr, "peb: built %s\n", output_path);
    else (void)remove(output_path);

    free(object_path);
    free(ir_path);
    codegen_free(codegen);
    semantic_free(semantic);
    ast_free(program);
    free(source);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *mode = "run";
    const char *source = NULL;
    const char *output = NULL;
    int keep_ir = 0;
    int index = 1;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (strcmp(argv[index], "build") == 0 || strcmp(argv[index], "run") == 0) {
        mode = argv[index++];
        if (index >= argc) {
            usage(argv[0]);
            return 2;
        }
    }
    source = argv[index++];

    while (index < argc) {
        if (strcmp(argv[index], "-o") == 0 && index + 1 < argc) {
            output = argv[index + 1];
            index += 2;
        } else if (strcmp(argv[index], "--keep-ir") == 0) {
            keep_ir = 1;
            ++index;
        } else {
            fprintf(stderr, "peb: unknown argument '%s'\n", argv[index]);
            usage(argv[0]);
            return 2;
        }
    }

    char *generated_output = NULL;
    if (output == NULL) {
        generated_output = default_output_name(source);
        output = generated_output;
    }
    if (output == NULL || output[0] == '\0') {
        fprintf(stderr, "peb: invalid output name\n");
        free(generated_output);
        return 2;
    }

    int status = compile_source(source, output, keep_ir);
    if (status == 0 && strcmp(mode, "run") == 0) {
        /* exec requires a path containing '/', so prefix bare output names. */
        if (strchr(output, '/') == NULL) {
            size_t n = strlen(output);
            char *relative = malloc(n + 3);
            if (relative == NULL) {
                free(generated_output);
                return 1;
            }
            snprintf(relative, n + 3, "./%s", output);
            status = run_program(relative);
            free(relative);
        } else {
            status = run_program(output);
        }
    }

    free(generated_output);
    return status;
}
