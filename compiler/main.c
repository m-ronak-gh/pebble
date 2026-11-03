#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#else
#include <windows.h>
#include <process.h>
#include <io.h>
#endif

#include <errno.h>
#include <limits.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef PATH_MAX
#if defined(MAX_PATH)
#define PATH_MAX MAX_PATH
#else
#define PATH_MAX 4096
#endif
#endif

#include "ast/ast.h"
#include "codegen/codegen.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semantic/semantic.h"

static void usage(const char *program, FILE *stream)
{
    fprintf(stream,
        "Usage:\n"
        "  %s <source.pb|source.peb> [-o output] [--keep-ir]\n"
        "  %s build <source.pb|source.peb> [-o output] [--keep-ir]\n"
        "  %s run <source.pb|source.peb> [-o output] [--keep-ir]\n",
        program, program, program);
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

typedef struct {
    char *path;
    char *content;
} LoadedSource;

typedef struct {
    LoadedSource *items;
    size_t count;
    size_t capacity;
} SourceManager;

static int source_manager_has(const SourceManager *sm, const char *canonical_path)
{
    for (size_t i = 0; i < sm->count; ++i) {
        if (strcmp(sm->items[i].path, canonical_path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int source_manager_add(SourceManager *sm, char *canonical_path, char *content)
{
    if (sm->count >= sm->capacity) {
        size_t new_cap = sm->capacity == 0 ? 8 : sm->capacity * 2;
        LoadedSource *new_items = realloc(sm->items, new_cap * sizeof(LoadedSource));
        if (new_items == NULL) return 0;
        sm->items = new_items;
        sm->capacity = new_cap;
    }
    sm->items[sm->count].path = canonical_path;
    sm->items[sm->count].content = content;
    sm->count++;
    return 1;
}

static void source_manager_free(SourceManager *sm)
{
    for (size_t i = 0; i < sm->count; ++i) {
        free(sm->items[i].path);
        free(sm->items[i].content);
    }
    free(sm->items);
    sm->items = NULL;
    sm->count = 0;
    sm->capacity = 0;
}

static char *get_directory(const char *filepath)
{
    const char *last_slash = strrchr(filepath, '/');
#ifdef _WIN32
    const char *last_bslash = strrchr(filepath, '\\');
    if (last_bslash != NULL && (last_slash == NULL || last_bslash > last_slash)) {
        last_slash = last_bslash;
    }
#endif
    if (last_slash == NULL) {
        char *dot = malloc(2);
        if (dot != NULL) {
            dot[0] = '.';
            dot[1] = '\0';
        }
        return dot;
    }
    size_t len = (size_t)(last_slash - filepath);
    char *dir = malloc(len + 1);
    if (dir != NULL) {
        memcpy(dir, filepath, len);
        dir[len] = '\0';
    }
    return dir;
}

static int file_exists(const char *path);

static char *join_paths(const char *dir, const char *rel, size_t rel_len)
{
    size_t dir_len = strlen(dir);
    char *res = malloc(dir_len + rel_len + 2);
    if (res == NULL) return NULL;
    snprintf(res, dir_len + rel_len + 2, "%s/%.*s", dir, (int)rel_len, rel);
    return res;
}

static char *try_search_dir_for_name(const char *dir, const char *name, size_t name_len)
{
    /* 1. Try exact name */
    char *p = join_paths(dir, name, name_len);
    if (p != NULL) {
        if (file_exists(p)) return p;
        free(p);
    }

    /* 2. If no extension, try with .pb and .peb (both as-is and lowercase) */
    int has_dot = 0;
    for (size_t i = 0; i < name_len; ++i) {
        if (name[i] == '.') { has_dot = 1; break; }
    }

    if (!has_dot) {
        char buf[PATH_MAX];
        snprintf(buf, sizeof(buf), "%.*s.pb", (int)name_len, name);
        p = join_paths(dir, buf, strlen(buf));
        if (p != NULL) {
            if (file_exists(p)) return p;
            free(p);
        }

        char lower[128];
        if (name_len < sizeof(lower) - 5) {
            for (size_t i = 0; i < name_len; ++i) {
                lower[i] = (char)tolower((unsigned char)name[i]);
            }
            lower[name_len] = '\0';
            strcat(lower, ".pb");
            p = join_paths(dir, lower, strlen(lower));
            if (p != NULL) {
                if (file_exists(p)) return p;
                free(p);
            }
        }

        snprintf(buf, sizeof(buf), "%.*s.peb", (int)name_len, name);
        p = join_paths(dir, buf, strlen(buf));
        if (p != NULL) {
            if (file_exists(p)) return p;
            free(p);
        }

        if (name_len < sizeof(lower) - 6) {
            for (size_t i = 0; i < name_len; ++i) {
                lower[i] = (char)tolower((unsigned char)name[i]);
            }
            lower[name_len] = '\0';
            strcat(lower, ".peb");
            p = join_paths(dir, lower, strlen(lower));
            if (p != NULL) {
                if (file_exists(p)) return p;
                free(p);
            }
        }
    }

    return NULL;
}

static char *find_import_path(const char *dir, const char *rel_path, size_t rel_len)
{
    int has_dot = 0;
    for (size_t i = 0; i < rel_len; ++i) {
        if (rel_path[i] == '.') { has_dot = 1; break; }
    }

    const char *sub = rel_path;
    size_t sub_len = rel_len;
    if (sub_len > 4 && strncmp(sub, "std/", 4) == 0) {
        sub += 4;
        sub_len -= 4;
    } else if (sub_len > 7 && strncmp(sub, "stdlib/", 7) == 0) {
        sub += 7;
        sub_len -= 7;
    }

    const char *search_dirs[] = {
        getenv("PEBBLE_STD_DIR"),
        getenv("PEBBLE_STDLIB_DIR"),
        "lib/pebble/std",
        "stdlib",
        "../lib/pebble/std",
        "../stdlib",
        "../../lib/pebble/std",
        "../../stdlib",
        "/usr/local/lib/pebble/std",
        "/usr/lib/pebble/std",
        NULL
    };

    /* 1. Stdlib identifier imports (e.g. import Math or import String without extension):
     * search standard library directories first so they do not collide with local test files. */
    if (!has_dot) {
        for (size_t s = 0; s < sizeof(search_dirs) / sizeof(search_dirs[0]); ++s) {
            const char *sdir = search_dirs[s];
            if (sdir == NULL || sdir[0] == '\0') continue;

            char *found = try_search_dir_for_name(sdir, rel_path, rel_len);
            if (found != NULL) return found;

            if (sub != rel_path) {
                found = try_search_dir_for_name(sdir, sub, sub_len);
                if (found != NULL) return found;
            }
        }
    }

    /* 2. User library imports (e.g. import "xyz.pb") or fallback:
     * try project-relative to importing file first. */
    char *found = try_search_dir_for_name(dir, rel_path, rel_len);
    if (found != NULL) {
        return found;
    }

    /* 3. Fallback search in stdlib directories for quoted stdlib imports like import "math.pb" */
    if (has_dot) {
        for (size_t s = 0; s < sizeof(search_dirs) / sizeof(search_dirs[0]); ++s) {
            const char *sdir = search_dirs[s];
            if (sdir == NULL || sdir[0] == '\0') continue;

            found = try_search_dir_for_name(sdir, rel_path, rel_len);
            if (found != NULL) return found;

            if (sub != rel_path) {
                found = try_search_dir_for_name(sdir, sub, sub_len);
                if (found != NULL) return found;
            }
        }
    }

    /* Fall back to project-relative so error reporting shows the original path */
    return join_paths(dir, rel_path, rel_len);
}

static char *resolve_canonical_path(const char *path)
{
#ifdef _WIN32
    return _fullpath(NULL, path, 0);
#else
    return realpath(path, NULL);
#endif
}

static int resolve_imports(ASTNode *program, const char *current_file_path, SourceManager *sm)
{
    char *dir = get_directory(current_file_path);
    if (dir == NULL) return 0;

    size_t i = 0;
    while (i < program->data.program.statement_count) {
        ASTNode *stmt = program->data.program.statements[i];
        if (stmt == NULL || stmt->kind != AST_IMPORT) {
            i++;
            continue;
        }

        const char *rel_path = stmt->data.import_decl.path;
        size_t rel_len = stmt->data.import_decl.path_length;
        if (rel_len >= 2 && rel_path[0] == '"' && rel_path[rel_len - 1] == '"') {
            rel_path += 1;
            rel_len -= 2;
        }

        char *joined = find_import_path(dir, rel_path, rel_len);
        if (joined == NULL) {
            free(dir);
            return 0;
        }

        char *canonical = resolve_canonical_path(joined);
        const char *lookup_path = canonical ? canonical : joined;

        if (source_manager_has(sm, lookup_path)) {
            free(joined);
            free(canonical);
            i++;
            continue;
        }

        char *content = read_file(joined);
        if (content == NULL) {
            fprintf(stderr, "peb: failed to import '%.*s'\n", (int)rel_len, rel_path);
            free(joined);
            free(canonical);
            free(dir);
            return 0;
        }

        char *stored_path = canonical ? canonical : strdup(joined);
        free(joined);
        if (stored_path == NULL) {
            free(content);
            free(dir);
            return 0;
        }

        if (!source_manager_add(sm, stored_path, content)) {
            free(stored_path);
            free(content);
            free(dir);
            return 0;
        }

        Parser imported_parser;
        parser_init(&imported_parser, content);
        ASTNode *imported_prog = parser_parse_program(&imported_parser);
        if (imported_prog == NULL) {
            fprintf(stderr, "peb: parsing imported file failed\n");
            free(dir);
            return 0;
        }

        if (!resolve_imports(imported_prog, stored_path, sm)) {
            ast_free(imported_prog);
            free(dir);
            return 0;
        }

        size_t imported_stmt_count = imported_prog->data.program.statement_count;
        if (imported_stmt_count > 0) {
            size_t new_total = program->data.program.statement_count + imported_stmt_count;
            ASTNode **new_stmts = realloc(program->data.program.statements,
                                          new_total * sizeof(ASTNode *));
            if (new_stmts == NULL) {
                ast_free(imported_prog);
                free(dir);
                return 0;
            }
            program->data.program.statements = new_stmts;

            for (size_t j = 0; j < imported_stmt_count; ++j) {
                ASTNode *s = imported_prog->data.program.statements[j];
                if (s != NULL && s->kind != AST_IMPORT) {
                    program->data.program.statements[program->data.program.statement_count++] = s;
                } else {
                    ast_free(s);
                }
            }

            imported_prog->data.program.statements = NULL;
            imported_prog->data.program.statement_count = 0;
        }

        ast_free(imported_prog);
        i++;
    }

    free(dir);
    return 1;
}

static char *default_output_name(const char *source)
{
    size_t length = strlen(source);
    size_t stem_length = length;
    const char *slash = strrchr(source, '/');
#if defined(_WIN32)
    const char *bslash = strrchr(source, '\\');
    if (bslash != NULL && (slash == NULL || bslash > slash)) {
        slash = bslash;
    }
#endif
    const char *dot = strrchr(source, '.');
    if (dot != NULL && (slash == NULL || dot > slash)) {
        stem_length = (size_t)(dot - source);
    }

#if defined(_WIN32)
    const char *ext = ".exe";
    size_t ext_len = 4;
#else
    const char *ext = "";
    size_t ext_len = 0;
#endif

    char *output = malloc(stem_length + ext_len + 1);
    if (output == NULL) return NULL;
    memcpy(output, source, stem_length);
    memcpy(output + stem_length, ext, ext_len);
    output[stem_length + ext_len] = '\0';
    if (output[0] == '\0') {
        free(output);
        return NULL;
    }
    return output;
}

#if defined(_WIN32)
static char *windows_build_cmdline(const char *const *argv)
{
    size_t len = 0;
    for (int i = 0; argv[i] != NULL; ++i) {
        if (i > 0) len++;
        const char *arg = argv[i];
        int needs_quote = (strchr(arg, ' ') != NULL || strchr(arg, '\t') != NULL || arg[0] == '\0');
        if (needs_quote) len += 2;
        for (const char *p = arg; *p; ++p) {
            if (*p == '"' || *p == '\\') len++;
            len++;
        }
    }

    char *cmd = malloc(len + 1);
    if (cmd == NULL) return NULL;
    char *out = cmd;

    for (int i = 0; argv[i] != NULL; ++i) {
        if (i > 0) *out++ = ' ';
        const char *arg = argv[i];
        int needs_quote = (strchr(arg, ' ') != NULL || strchr(arg, '\t') != NULL || arg[0] == '\0');
        if (needs_quote) *out++ = '"';
        for (const char *p = arg; *p; ++p) {
            if (*p == '"') {
                *out++ = '\\';
            }
            *out++ = *p;
        }
        if (needs_quote) *out++ = '"';
    }
    *out = '\0';
    return cmd;
}

static int platform_run_command(const char *const *argv)
{
    char *cmdline = windows_build_cmdline(argv);
    if (cmdline == NULL) return -1;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "peb: failed to execute '%s' (error %lu)\n", argv[0], GetLastError());
        free(cmdline);
        return -1;
    }
    free(cmdline);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}

static int platform_get_executable_path(char *buffer, size_t size, const char *argv0)
{
    (void)argv0;
    DWORD len = GetModuleFileNameA(NULL, buffer, (DWORD)size);
    if (len > 0 && len < size) {
        return 1;
    }
    return 0;
}
#else
static int platform_run_command(const char *const *argv)
{
    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "peb: cannot start executable: %s\n", strerror(errno));
        return 1;
    }
    if (child == 0) {
        execvp(argv[0], (char *const *)argv);
        fprintf(stderr, "peb: cannot execute '%s': %s\n", argv[0], strerror(errno));
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

static int platform_get_executable_path(char *buffer, size_t size, const char *argv0)
{
#if defined(__APPLE__)
    (void)argv0;
    uint32_t bufsize = (uint32_t)size;
    if (_NSGetExecutablePath(buffer, &bufsize) == 0) {
        char real[PATH_MAX];
        if (realpath(buffer, real) != NULL) {
            strncpy(buffer, real, size);
            buffer[size - 1] = '\0';
        }
        return 1;
    }
    return 0;
#elif defined(__linux__)
    (void)argv0;
    ssize_t len = readlink("/proc/self/exe", buffer, size - 1);
    if (len > 0) {
        buffer[len] = '\0';
        return 1;
    }
    return 0;
#else
    if (argv0 != NULL && realpath(argv0, buffer) != NULL) {
        return 1;
    }
    return 0;
#endif
}
#endif

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

static char *find_runtime_path(const char *compiler_path)
{
    /* 1. Explicit environment variable PEBBLE_RUNTIME_PATH */
    const char *env_path = getenv("PEBBLE_RUNTIME_PATH");
    if (env_path != NULL && file_exists(env_path)) {
        return strdup(env_path);
    }

    /* 2. Directory override PEBBLE_RUNTIME_DIR */
    const char *env_dir = getenv("PEBBLE_RUNTIME_DIR");
    if (env_dir != NULL && env_dir[0] != '\0') {
        char buf[PATH_MAX];
        snprintf(buf, sizeof(buf), "%s/pebble_runtime.c", env_dir);
        if (file_exists(buf)) return strdup(buf);
        snprintf(buf, sizeof(buf), "%s/runtime/pebble_runtime.c", env_dir);
        if (file_exists(buf)) return strdup(buf);
    }

    /* 3. Discovered directory of compiler binary */
    if (compiler_path != NULL && compiler_path[0] != '\0') {
        char *compiler_dir = get_directory(compiler_path);
        if (compiler_dir != NULL) {
            char buf[PATH_MAX];

            /* In repository: <dir>/runtime/pebble_runtime.c */
            snprintf(buf, sizeof(buf), "%s/runtime/pebble_runtime.c", compiler_dir);
            if (file_exists(buf)) {
                free(compiler_dir);
                return strdup(buf);
            }

            /* Bundled co-located: <dir>/pebble_runtime.c */
            snprintf(buf, sizeof(buf), "%s/pebble_runtime.c", compiler_dir);
            if (file_exists(buf)) {
                free(compiler_dir);
                return strdup(buf);
            }

            /* Package directory layout: <dir>/../runtime/pebble_runtime.c */
            snprintf(buf, sizeof(buf), "%s/../runtime/pebble_runtime.c", compiler_dir);
            if (file_exists(buf)) {
                free(compiler_dir);
                return strdup(buf);
            }

            /* Standard unix install: <dir>/../lib/pebble/pebble_runtime.c */
            snprintf(buf, sizeof(buf), "%s/../lib/pebble/pebble_runtime.c", compiler_dir);
            if (file_exists(buf)) {
                free(compiler_dir);
                return strdup(buf);
            }

            /* Standard unix install: <dir>/../share/pebble/pebble_runtime.c */
            snprintf(buf, sizeof(buf), "%s/../share/pebble/pebble_runtime.c", compiler_dir);
            if (file_exists(buf)) {
                free(compiler_dir);
                return strdup(buf);
            }

            free(compiler_dir);
        }
    }

    /* 4. Current working directory fallback */
    if (file_exists("runtime/pebble_runtime.c")) {
        return strdup("runtime/pebble_runtime.c");
    }
    if (file_exists("pebble_runtime.c")) {
        return strdup("pebble_runtime.c");
    }

    return NULL;
}

static const char *get_linker_command(void)
{
    const char *env_cc = getenv("PEBBLE_CC");
    if (env_cc != NULL && env_cc[0] != '\0') return env_cc;
    env_cc = getenv("CC");
    if (env_cc != NULL && env_cc[0] != '\0') return env_cc;

#if defined(_WIN32)
    return "clang";
#elif defined(__APPLE__)
    return "clang";
#else
    return "cc";
#endif
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

static int link_object(const char *object_path, const char *output_path, const char *runtime_path)
{
    const char *linker = get_linker_command();
    const char *args[16];
    int argc = 0;

    args[argc++] = linker;

#if defined(__linux__)
    args[argc++] = "-no-pie";
#endif

    char *runtime_dir = get_directory(runtime_path);
    char include_flag[PATH_MAX + 4];
    if (runtime_dir != NULL) {
        snprintf(include_flag, sizeof(include_flag), "-I%s", runtime_dir);
        args[argc++] = include_flag;
    }

    args[argc++] = object_path;
    args[argc++] = runtime_path;
#if !defined(_WIN32)
    args[argc++] = "-lm";
#endif
    args[argc++] = "-o";
    args[argc++] = output_path;
    args[argc] = NULL;

    int status = platform_run_command(args);
    free(runtime_dir);
    if (status != 0) {
        fprintf(stderr, "peb: system linker failed with status %d\n", status);
        return 0;
    }
    return 1;
}

static int compile_source(const char *source_path, const char *output_path,
                          int keep_ir, const char *compiler_path)
{
    SourceManager sm = {0};

    char *source = read_file(source_path);
    if (source == NULL) {
        return 1;
    }

    char *runtime_path = find_runtime_path(compiler_path);
    if (runtime_path == NULL) {
        fprintf(stderr, "peb: cannot find Pebble runtime (pebble_runtime.c)\n"
                        "     Please ensure Pebble is installed properly or set PEBBLE_RUNTIME_DIR\n");
        free(source);
        return 1;
    }

    char *root_canonical = resolve_canonical_path(source_path);
    char *root_stored = root_canonical ? root_canonical : strdup(source_path);
    if (root_stored == NULL || !source_manager_add(&sm, root_stored, source)) {
        free(root_stored);
        free(runtime_path);
        free(source);
        return 1;
    }

    if (remove(output_path) != 0 && errno != ENOENT) {
        fprintf(stderr, "peb: cannot remove old output '%s': %s\n",
                output_path, strerror(errno));
        free(runtime_path);
        source_manager_free(&sm);
        return 1;
    }

    Parser parser;
    parser_init(&parser, source);
    ASTNode *program = parser_parse_program(&parser);
    if (program == NULL) {
        fprintf(stderr, "peb: parsing failed; executable not produced\n");
        free(runtime_path);
        source_manager_free(&sm);
        return 1;
    }

    if (!resolve_imports(program, source_path, &sm)) {
        fprintf(stderr, "peb: import resolution failed; executable not produced\n");
        free(runtime_path);
        ast_free(program);
        source_manager_free(&sm);
        return 1;
    }

    SemanticContext *semantic = semantic_analyze(program);
    if (semantic == NULL) {
        fprintf(stderr, "peb: semantic analysis failed; executable not produced\n");
        free(runtime_path);
        ast_free(program);
        source_manager_free(&sm);
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
    }

    if (main_count != 1 || main_function == NULL) {
        fprintf(stderr, "peb: program must define exactly one function number main()\n");
        free(runtime_path);
        semantic_free(semantic);
        ast_free(program);
        source_manager_free(&sm);
        return 1;
    }
    if (main_function->data.function_decl.parameter_count != 0) {
        fprintf(stderr, "peb: entry point main() must not have parameters\n");
        free(runtime_path);
        semantic_free(semantic);
        ast_free(program);
        source_manager_free(&sm);
        return 1;
    }
    if (main_function->data.function_decl.return_type.kind != AST_TYPE_NUMBER ||
        main_function->data.function_decl.return_type.dimension_count != 0) {
        fprintf(stderr, "peb: entry point must return number\n");
        free(runtime_path);
        semantic_free(semantic);
        ast_free(program);
        source_manager_free(&sm);
        return 1;
    }

    Codegen *codegen = codegen_create("pebble_module", semantic);
    if (codegen == NULL) {
        fprintf(stderr, "peb: could not initialize code generator\n");
        free(runtime_path);
        semantic_free(semantic);
        ast_free(program);
        source_manager_free(&sm);
        return 1;
    }

    /* Pass 0: declare all struct types. */
    for (size_t i = 0; i < program->data.program.statement_count; ++i) {
        ASTNode *node = program->data.program.statements[i];
        if (node != NULL && node->kind == AST_STRUCT) {
            codegen_declare_struct(codegen, node);
        }
    }

    /* Pass 1: declare all function signatures (including extern functions). */
    for (size_t i = 0; i < program->data.program.statement_count; ++i) {
        ASTNode *node = program->data.program.statements[i];
        if (node != NULL && (node->kind == AST_FUNCTION || node->kind == AST_EXTERN_FUNCTION)) {
            codegen_declare_function(codegen, node);
        }
    }

    /* Pass 2: generate function bodies. */
    if (!codegen_has_error(codegen)) {
        for (size_t i = 0; i < program->data.program.statement_count; ++i) {
            ASTNode *node = program->data.program.statements[i];
            if (node != NULL && node->kind == AST_FUNCTION) {
                codegen_function(codegen, node);
            }
        }
    }

    /* Do not emit or link partially generated LLVM IR. */
    if (codegen_has_error(codegen)) {
        fprintf(stderr,
                "peb: code generation failed; executable not produced\n");
        (void)remove(output_path);
        free(runtime_path);
        codegen_free(codegen);
        semantic_free(semantic);
        ast_free(program);
        source_manager_free(&sm);
        return 1;
    }

    size_t output_len = strlen(output_path);
    char *object_path = malloc(output_len + 3);
    char *ir_path = malloc(output_len + 4);
    if (object_path == NULL || ir_path == NULL) {
        fprintf(stderr, "peb: out of memory creating output paths\n");
        free(object_path);
        free(ir_path);
        free(runtime_path);
        codegen_free(codegen);
        semantic_free(semantic);
        ast_free(program);
        source_manager_free(&sm);
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
    if (ok) ok = link_object(object_path, output_path, runtime_path);
    free(runtime_path);
    (void)remove(object_path);
    if (!keep_ir) (void)remove(ir_path);

    if (ok) fprintf(stderr, "peb: built %s\n", output_path);
    else (void)remove(output_path);

    free(object_path);
    free(ir_path);
    codegen_free(codegen);
    semantic_free(semantic);
    ast_free(program);
    source_manager_free(&sm);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    char compiler_path[PATH_MAX];
    if (!platform_get_executable_path(compiler_path, sizeof(compiler_path), argv[0])) {
        strncpy(compiler_path, argv[0], sizeof(compiler_path) - 1);
        compiler_path[sizeof(compiler_path) - 1] = '\0';
    }

    const char *mode = "run";
    const char *source = NULL;
    const char *output = NULL;
    int keep_ir = 0;
    int index = 1;

    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 ||
         strcmp(argv[1], "-h") == 0)) {
        usage(argv[0], stdout);
        return 0;
    }

    if (argc < 2) {
        usage(argv[0], stderr);
        return 2;
    }

    if (strcmp(argv[index], "build") == 0 ||
        strcmp(argv[index], "run") == 0) {
        mode = argv[index++];
        if (index >= argc) {
            fprintf(stderr, "peb: missing source file\n");
            usage(argv[0], stderr);
            return 2;
        }
    }
    source = argv[index++];

    if (source[0] == '-') {
        fprintf(stderr, "peb: unknown option '%s'\n", source);
        usage(argv[0], stderr);
        return 2;
    }

    while (index < argc) {
        if (strcmp(argv[index], "-o") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr,
                        "peb: option '-o' requires an output path\n");
                return 2;
            }

            if (output != NULL) {
                fprintf(stderr, "peb: option '-o' specified more than once\n");
                return 2;
            }

            output = argv[index + 1];
            if (output[0] == '\0') {
                fprintf(stderr, "peb: output path cannot be empty\n");
                return 2;
            }

            index += 2;
        } else if (strcmp(argv[index], "--keep-ir") == 0) {
            if (keep_ir) {
                fprintf(stderr,
                        "peb: option '--keep-ir' specified more than once\n");
                return 2;
            }

            keep_ir = 1;
            ++index;
        } else {
            fprintf(stderr, "peb: unknown argument '%s'\n", argv[index]);
            usage(argv[0], stderr);
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

    int status = compile_source(source, output, keep_ir, compiler_path);
    if (status == 0 && strcmp(mode, "run") == 0) {
        char *run_target = NULL;
#if !defined(_WIN32)
        if (strchr(output, '/') == NULL) {
            size_t n = strlen(output);
            run_target = malloc(n + 3);
            if (run_target != NULL) {
                snprintf(run_target, n + 3, "./%s", output);
            }
        } else {
            run_target = strdup(output);
        }
#else
        run_target = strdup(output);
#endif
        if (run_target == NULL) {
            fprintf(stderr, "peb: out of memory preparing executable path\n");
            free(generated_output);
            return 1;
        }

        const char *run_argv[] = { run_target, NULL };
        status = platform_run_command(run_argv);
        free(run_target);
    }

    free(generated_output);
    return status;
}
