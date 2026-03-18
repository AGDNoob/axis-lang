/*
 * main.c – AXIS language driver
 *
 * Usage:
 *   axis run   <file.axis>                 Run a script-mode file
 *   axis build <file.axis> [-o <output>]   Compile a compile-mode file
 *   axis <file.axis>                       Auto-detect mode
 *
 * Pipeline:  source → Lexer → Parser → Semantic → IR → x86-64 → PE → .exe
 */

#include "axis_common.h"
#include "axis_arena.h"
#include "axis_token.h"
#include "axis_lexer.h"
#include "axis_ast.h"
#include "axis_parser.h"
#include "axis_semantic.h"
#include "axis_ir.h"
#include "axis_x64.h"
#include "axis_pe.h"
#include "axis_elf.h"

#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <direct.h>
    #include <process.h>
    #define axis_mkdir(p) _mkdir(p)
    #define PATH_SEP '\\'
#else
    #include <sys/wait.h>
    #include <unistd.h>
    #define axis_mkdir(p) mkdir(p, 0755)
    #define PATH_SEP '/'
#endif

/* ═════════════════════════════════════════════════════════════
 * Command modes
 * ═════════════════════════════════════════════════════════════ */

typedef enum {
    CMD_NONE,       /* auto-detect */
    CMD_RUN,        /* axis run file.axis   */
    CMD_BUILD,      /* axis build file.axis */
    CMD_CHECK,      /* axis check file.axis */
    CMD_HELP,
    CMD_VERSION,
} CmdMode;

typedef enum {
    FMT_DEFAULT,    /* use current OS format */
    FMT_PE,         /* Windows PE */
    FMT_ELF,        /* Linux ELF64 */
} OutputFormat;

/* ═════════════════════════════════════════════════════════════
 * Command-line options
 * ═════════════════════════════════════════════════════════════ */

typedef struct {
    CmdMode      command;
    const char  *input_file;
    const char  *output_file;   /* only for build */
    OutputFormat format;
    bool         dump_tokens;
    bool         dump_ast;
    bool         dump_ir;
    bool         dump_x64;
    bool         verbose;
    /* check mode flags */
    bool         check_dead;
    bool         check_unused;
    bool         check_all;
} Options;

/* ═════════════════════════════════════════════════════════════
 * Usage / Help
 * ═════════════════════════════════════════════════════════════ */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "AXIS Language %s\n"
        "\n"
        "Usage:\n"
        "  %s <file.axis>                   Auto-detect mode and run/compile\n"
        "  %s run <file.axis>               Run a script (mode script)\n"
        "  %s build <file.axis> [-o out]    Compile to binary (mode compile)\n"
        "  %s check <file.axis> [flags]     Check source for errors\n"
        "\n"
        "Options:\n"
        "  -o <file>       Output file (build mode)\n"
        "  --pe            Output Windows PE executable\n"
        "  --elf           Output Linux ELF64 executable\n"
        "  --dump-tokens   Print token stream\n"
        "  --dump-ir       Print IR\n"
        "  --dump-x64      Print x64 code info\n"
        "  -v, --verbose   Verbose output\n"
        "  --version       Show version\n"
        "  -h, --help      Show this help\n"
        "\n"
        "Check flags:\n"
        "  --unused        Warn about unused variables\n"
        "  --dead          Warn about dead code\n"
        "  --all           Enable all check warnings\n"
        "\n"
        "Examples:\n"
        "  %s hello.axis                    # auto-detect and run/compile\n"
        "  %s run script.axis               # run script mode file\n"
        "  %s build prog.axis -o prog.exe   # compile to binary\n"
        "  %s check prog.axis --all         # check for all issues\n",
        AXIS_VERSION_STR, prog, prog, prog, prog, prog, prog, prog, prog);
}

static void print_version(void) {
    printf("axis %s\n", AXIS_VERSION_STR);
}

/* ═════════════════════════════════════════════════════════════
 * Argument parsing
 * ═════════════════════════════════════════════════════════════ */

static int parse_args(int argc, char **argv, Options *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->command = CMD_NONE;

    if (argc < 2) {
        opts->command = CMD_HELP;
        return 0;
    }

    /* Check first arg for a command word */
    const char *first = argv[1];
    int arg_start = 1;

    if (strcmp(first, "-h") == 0 || strcmp(first, "--help") == 0
        || strcmp(first, "help") == 0) {
        opts->command = CMD_HELP;
        return 0;
    }
    if (strcmp(first, "--version") == 0 || strcmp(first, "-V") == 0) {
        opts->command = CMD_VERSION;
        return 0;
    }
    if (strcmp(first, "run") == 0) {
        opts->command = CMD_RUN;
        arg_start = 2;
    } else if (strcmp(first, "build") == 0) {
        opts->command = CMD_BUILD;
        arg_start = 2;
    } else if (strcmp(first, "check") == 0) {
        opts->command = CMD_CHECK;
        arg_start = 2;
    }

    /* Parse remaining arguments */
    for (int i = arg_start; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            opts->command = CMD_HELP;
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            opts->command = CMD_VERSION;
            return 0;
        }
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -o requires an argument\n");
                return -1;
            }
            opts->output_file = argv[++i];
            continue;
        }
        if (strcmp(arg, "--pe") == 0)          { opts->format = FMT_PE; continue; }
        if (strcmp(arg, "--elf") == 0)         { opts->format = FMT_ELF; continue; }
        if (strcmp(arg, "--unused") == 0)      { opts->check_unused = true; continue; }
        if (strcmp(arg, "--dead") == 0)        { opts->check_dead = true; continue; }
        if (strcmp(arg, "--all") == 0)         { opts->check_all = true; continue; }
        if (strcmp(arg, "--dump-tokens") == 0) { opts->dump_tokens = true; continue; }
        if (strcmp(arg, "--dump-ir") == 0)     { opts->dump_ir = true; continue; }
        if (strcmp(arg, "--dump-x64") == 0)    { opts->dump_x64 = true; continue; }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opts->verbose = true;
            continue;
        }
        /* Unknown flag */
        if (arg[0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", arg);
            return -1;
        }
        /* Positional: input file */
        if (opts->input_file) {
            fprintf(stderr, "error: multiple input files not supported\n");
            return -1;
        }
        opts->input_file = arg;
    }

    if (opts->command != CMD_HELP && opts->command != CMD_VERSION
        && !opts->input_file) {
        fprintf(stderr, "error: no input file\n");
        return -1;
    }

    return 0;
}

/* ═════════════════════════════════════════════════════════════
 * Read source file
 * ═════════════════════════════════════════════════════════════ */

static char *read_source(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fprintf(stderr, "error: cannot determine size of '%s'\n", path);
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "error: out of memory reading '%s'\n", path);
        fclose(f);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);

    buf[nread] = '\0';
    *out_len = nread;
    return buf;
}

/* ═════════════════════════════════════════════════════════════
 * Dump tokens (--dump-tokens)
 * ═════════════════════════════════════════════════════════════ */

static const char *token_kind_name(TokenType t) {
    switch (t) {
    case TOK_INT_LIT:     return "INT";
    case TOK_STRING_LIT:  return "STR";
    case TOK_TRUE:        return "TRUE";
    case TOK_FALSE:       return "FALSE";
    case TOK_IDENT:       return "IDENT";
    case TOK_NEWLINE:     return "NEWLINE";
    case TOK_INDENT:      return "INDENT";
    case TOK_DEDENT:      return "DEDENT";
    case TOK_EOF:         return "EOF";
    default:              return "OP/KW";
    }
}

static void dump_tokens(const Token *tokens, int count) {
    fprintf(stderr, "=== TOKENS (%d) ===\n", count);
    for (int i = 0; i < count; i++) {
        const Token *t = &tokens[i];
        fprintf(stderr, "  [%4d] %-8s line=%d col=%d",
                i, token_kind_name(t->type), t->loc.line, t->loc.col);
        if (t->type == TOK_INT_LIT) {
            fprintf(stderr, "  val=%lld", (long long)t->int_val);
        } else if (t->type == TOK_STRING_LIT || t->type == TOK_IDENT) {
            fprintf(stderr, "  \"%.*s\"", (int)t->length, t->start);
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "=== END TOKENS ===\n\n");
}

/* ═════════════════════════════════════════════════════════════
 * Ensure __axcache__ directory exists
 * ═════════════════════════════════════════════════════════════ */

static void ensure_cache_dir(const char *input_path) {
    /* Put __axcache__ next to the input file */
    char dir[1024];
    const char *last_sep = strrchr(input_path, PATH_SEP);
#ifdef _WIN32
    /* Also check forward slash on Windows */
    const char *last_fwd = strrchr(input_path, '/');
    if (last_fwd && (!last_sep || last_fwd > last_sep)) last_sep = last_fwd;
#endif
    if (last_sep) {
        size_t prefix_len = (size_t)(last_sep - input_path + 1);
        if (prefix_len >= sizeof(dir) - 16) return;
        memcpy(dir, input_path, prefix_len);
        snprintf(dir + prefix_len, sizeof(dir) - prefix_len, "__axcache__");
    } else {
        snprintf(dir, sizeof(dir), "__axcache__");
    }

    struct stat st;
    if (stat(dir, &st) != 0) {
        axis_mkdir(dir);
    }
}

/* ═════════════════════════════════════════════════════════════
 * Build cache path for a given input file
 *
 * e.g. "examples/hello.axis" → "examples/__axcache__/hello.exe"
 * ═════════════════════════════════════════════════════════════ */

static void build_cache_path(const char *input_path, char *out, size_t out_size) {
    /* Extract directory and basename */
    const char *last_sep = strrchr(input_path, PATH_SEP);
#ifdef _WIN32
    const char *last_fwd = strrchr(input_path, '/');
    if (last_fwd && (!last_sep || last_fwd > last_sep)) last_sep = last_fwd;
#endif

    const char *basename = last_sep ? last_sep + 1 : input_path;
    size_t prefix_len = last_sep ? (size_t)(last_sep - input_path + 1) : 0;

    /* Strip .axis extension */
    const char *dot = strrchr(basename, '.');
    size_t name_len = dot ? (size_t)(dot - basename) : strlen(basename);

    /* Cache always uses the host OS format */
    const char *cache_ext;
#ifdef _WIN32
    cache_ext = ".exe";
#else
    cache_ext = "";
#endif
    if (prefix_len > 0) {
        snprintf(out, out_size, "%.*s__axcache__%c%.*s%s",
                 (int)prefix_len, input_path, PATH_SEP,
                 (int)name_len, basename, cache_ext);
    } else {
        snprintf(out, out_size, "__axcache__%c%.*s%s",
                 PATH_SEP, (int)name_len, basename, cache_ext);
    }
}

/* ═════════════════════════════════════════════════════════════
 * Check if cache is fresh (cached exe newer than source file)
 * ═════════════════════════════════════════════════════════════ */

static bool cache_is_fresh(const char *source_path, const char *cache_path) {
    struct stat src_st, cache_st;
    if (stat(source_path, &src_st) != 0) return false;
    if (stat(cache_path, &cache_st) != 0) return false;
    return cache_st.st_mtime >= src_st.st_mtime;
}

/* ═════════════════════════════════════════════════════════════
 * Run an executable and return its exit code
 * ═════════════════════════════════════════════════════════════ */

static int run_executable(const char *path, bool verbose) {
    if (verbose) {
        fprintf(stderr, "[axis] running '%s'...\n", path);
    }

#ifdef _WIN32
    /* On Windows, use _spawnl to run and wait */
    intptr_t rc = _spawnl(_P_WAIT, path, path, NULL);
    if (rc == -1) {
        fprintf(stderr, "error: cannot execute '%s': %s\n", path, strerror(errno));
        return 1;
    }
    return (int)rc;
#else
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "error: fork failed: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        execl(path, path, NULL);
        fprintf(stderr, "error: exec failed: %s\n", strerror(errno));
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
#endif
}

/* ═════════════════════════════════════════════════════════════
 * Compile pipeline (shared by run + build)
 * Returns 0 on success, nonzero on error.
 * ═════════════════════════════════════════════════════════════ */

static int compile_to_exe(const char *input_path, const char *output_path,
                          const Options *opts) {

    /* ── Read source ────────────────────────────────────── */
    if (opts->verbose) {
        fprintf(stderr, "[axis] reading '%s'...\n", input_path);
    }

    size_t src_len = 0;
    char *source = read_source(input_path, &src_len);
    if (!source) return 1;

    /* ── Arena ──────────────────────────────────────────── */
    Arena arena;
    arena_init(&arena);

    /* ── Lexer ──────────────────────────────────────────── */
    if (opts->verbose) fprintf(stderr, "[axis] lexing...\n");

    Lexer lex;
    lexer_init(&lex, source, src_len, input_path, &arena);

    int token_count = 0;
    Token *tokens = lexer_tokenize_all(&lex, &token_count);
    if (!tokens) {
        fprintf(stderr, "error: lexer failed\n");
        free(source); arena_free(&arena);
        return 1;
    }

    if (opts->dump_tokens) dump_tokens(tokens, token_count);
    if (opts->verbose) fprintf(stderr, "[axis] %d tokens\n", token_count);

    /* ── Parser ─────────────────────────────────────────── */
    if (opts->verbose) fprintf(stderr, "[axis] parsing...\n");

    Parser parser;
    parser_init(&parser, tokens, token_count, &arena, input_path, source);

    ASTProgram *ast = parser_parse(&parser);
    if (!ast) {
        fprintf(stderr, "error: parser failed\n");
        free(source); arena_free(&arena);
        return 1;
    }

    if (opts->verbose) {
        fprintf(stderr, "[axis] %d functions, %d top-level statements\n",
                ast->func_count, ast->stmt_count);
    }

    /* ── Mode checks ────────────────────────────────────── */
    if (opts->command == CMD_RUN && ast->mode == MODE_COMPILE) {
        fprintf(stderr,
            "error: '%s' is a compile-mode file.\n\n"
            "  This file uses 'mode compile' and cannot be run as a script.\n"
            "  Use 'axis build %s -o output' to compile it.\n",
            input_path, input_path);
        free(source); arena_free(&arena);
        return 1;
    }

    if (opts->command == CMD_BUILD && ast->mode == MODE_SCRIPT) {
        fprintf(stderr,
            "error: '%s' is a script-mode file.\n\n"
            "  This file uses 'mode script' and is meant to be run, not compiled.\n"
            "  Use 'axis run %s' to execute it.\n",
            input_path, input_path);
        free(source); arena_free(&arena);
        return 1;
    }

    /* ── Semantic analysis ──────────────────────────────── */
    if (opts->verbose) fprintf(stderr, "[axis] analyzing...\n");

    Semantic sem;
    semantic_init(&sem, &arena, input_path);

    int sem_result = semantic_analyze(&sem, ast);
    if (sem_result != 0) {
        fprintf(stderr, "error: semantic analysis failed (%d error%s)\n",
                sem_result, sem_result == 1 ? "" : "s");
        free(source); arena_free(&arena);
        return 1;
    }

    if (opts->verbose) fprintf(stderr, "[axis] semantic analysis OK\n");

    /* ── IR generation ──────────────────────────────────── */
    if (opts->verbose) fprintf(stderr, "[axis] generating IR...\n");

    IRProgram ir;
    ir_program_init(&ir, &arena);
    ir_generate(&ir, ast, input_path);

    /* ── Optimization passes (compile mode only) ──────── */
    if (ast->mode == MODE_COMPILE) {
        if (opts->verbose) fprintf(stderr, "[axis] running optimization passes...\n");
        opt_dce(&ir);
        opt_inline(&ir);
        opt_constfold(&ir);
        opt_copyprop(&ir);
        opt_strength_reduce(&ir);
        opt_peephole(&ir);
        opt_loadstore_elim(&ir);
        opt_licm(&ir);
        opt_unroll(&ir);
        opt_loadstore_elim(&ir); /* re-run after unrolling */
        opt_rie(&ir);
        opt_dce(&ir);            /* final cleanup */
    } else if (opts->verbose) {
        fprintf(stderr, "[axis] skipping optimizations (script mode)\n");
    }

    if (opts->dump_ir) ir_dump(&ir, stderr);
    if (opts->verbose) {
        fprintf(stderr, "[axis] %d IR functions, %d strings\n",
                ir.func_count, ir.str_count);
    }

    /* ── x86-64 code generation ─────────────────────────── */
    if (opts->verbose) fprintf(stderr, "[axis] generating x86-64 code...\n");

    X64Ctx x64;
    memset(&x64, 0, sizeof(x64));
    x64_codegen(&x64, &ir, &arena);

    if (opts->dump_x64) x64_dump(&x64, stderr);
    if (opts->verbose) {
        fprintf(stderr, "[axis] code: %d bytes, rdata: %d bytes, %d relocs\n",
                x64.code.len, x64.rdata_len, x64.reloc_count);
    }

    /* ── Determine output format ───────────────────────── */
    OutputFormat fmt = opts->format;
    if (fmt == FMT_DEFAULT) {
#ifdef _WIN32
        fmt = FMT_PE;
#else
        fmt = FMT_ELF;
#endif
    }

    if (fmt == FMT_PE) {
        /* ── PE generation ──────────────────────────────── */
        if (opts->verbose) fprintf(stderr, "[axis] generating PE...\n");

        PECtx pe;
        memset(&pe, 0, sizeof(pe));

        int pe_result = pe_write(&pe, &x64);
        if (pe_result != 0) {
            fprintf(stderr, "error: PE generation failed\n");
            free(source); arena_free(&arena);
            return 1;
        }

        if (opts->verbose) fprintf(stderr, "[axis] PE image: %d bytes\n", pe.len);

        int save_result = pe_save(&pe, output_path);
        if (save_result != 0) {
            fprintf(stderr, "error: cannot write '%s'\n", output_path);
            pe_free(&pe); free(source); arena_free(&arena);
            return 1;
        }

        if (opts->verbose) fprintf(stderr, "[axis] wrote '%s'\n", output_path);
        pe_free(&pe);
    } else {
        /* ── ELF generation ─────────────────────────────── */
        if (opts->verbose) fprintf(stderr, "[axis] generating ELF64...\n");

        ELFCtx elf;
        memset(&elf, 0, sizeof(elf));

        int elf_result = elf_write(&elf, &x64);
        if (elf_result != 0) {
            fprintf(stderr, "error: ELF generation failed\n");
            free(source); arena_free(&arena);
            return 1;
        }

        if (opts->verbose) fprintf(stderr, "[axis] ELF image: %d bytes\n", elf.len);

        int save_result = elf_save(&elf, output_path);
        if (save_result != 0) {
            fprintf(stderr, "error: cannot write '%s'\n", output_path);
            elf_free(&elf); free(source); arena_free(&arena);
            return 1;
        }

        if (opts->verbose) fprintf(stderr, "[axis] wrote '%s'\n", output_path);
        elf_free(&elf);
    }

    /* ── Cleanup ────────────────────────────────────────── */
    free(source);
    arena_free(&arena);

    return 0;
}

/* ═════════════════════════════════════════════════════════════
 * Quick mode detection (peek at source without full parse)
 * Returns MODE_SCRIPT or MODE_COMPILE.
 * ═════════════════════════════════════════════════════════════ */

static ProgramMode detect_mode_quick(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return MODE_COMPILE; /* fallback */

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Skip blank lines and comments */
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r') continue;
        if (*p == '/' && *(p+1) == '/') continue;
        if (*p == '#') continue;

        /* Check for mode declaration */
        if (strncmp(p, "mode ", 5) == 0) {
            const char *m = p + 5;
            while (*m == ' ' || *m == '\t') m++;
            if (strncmp(m, "script", 6) == 0) {
                fclose(f);
                return MODE_SCRIPT;
            }
        }
        fclose(f);
        return MODE_COMPILE;
    }
    fclose(f);
    return MODE_COMPILE;
}

/* ═════════════════════════════════════════════════════════════
 * Derive default output path from input (strip .axis, add .exe)
 * ═════════════════════════════════════════════════════════════ */

static void default_output_path(const char *input, OutputFormat format,
                                char *out, size_t out_size) {
    const char *last_sep = strrchr(input, PATH_SEP);
#ifdef _WIN32
    const char *last_fwd = strrchr(input, '/');
    if (last_fwd && (!last_sep || last_fwd > last_sep)) last_sep = last_fwd;
#endif
    const char *basename = last_sep ? last_sep + 1 : input;
    const char *dot = strrchr(basename, '.');
    size_t prefix_len = last_sep ? (size_t)(last_sep - input + 1) : 0;
    size_t name_len = dot ? (size_t)(dot - basename) : strlen(basename);

    /* Determine extension based on format */
    OutputFormat fmt = format;
    if (fmt == FMT_DEFAULT) {
#ifdef _WIN32
        fmt = FMT_PE;
#else
        fmt = FMT_ELF;
#endif
    }
    const char *ext = (fmt == FMT_PE) ? ".exe" : "";

    snprintf(out, out_size, "%.*s%.*s%s",
             (int)prefix_len, input,
             (int)name_len, basename, ext);
}

/* ═════════════════════════════════════════════════════════════
 * Check file (axis check)
 * Runs lexer + parser + optionally semantic analysis,
 * collecting and reporting ALL errors.
 * ═════════════════════════════════════════════════════════════ */

static int check_file(const char *input_path, const Options *opts) {
    size_t src_len = 0;
    char *source = read_source(input_path, &src_len);
    if (!source) return 1;

    Arena arena;
    arena_init(&arena);

    int total_errors = 0;

    /* ── Lex ──────────────────────────────────────────── */
    Lexer lex;
    lexer_init(&lex, source, src_len, input_path, &arena);
    lex.check_mode = true;

    int token_count = 0;
    Token *tokens = lexer_tokenize_all(&lex, &token_count);
    total_errors += lex.error_count;

    if (!tokens) {
        fprintf(stderr, "error: lexer produced no tokens\n");
        free(source); arena_free(&arena);
        return 1;
    }

    /* ── Parse ────────────────────────────────────────── */
    Parser parser;
    parser_init(&parser, tokens, token_count, &arena, input_path, source);
    parser.check_mode = true;

    ASTProgram *ast = parser_parse(&parser);
    total_errors += parser.error_count;

    /* ── Semantic (only with --unused, --dead, or --all) ─ */
    bool run_semantic = opts->check_unused || opts->check_dead || opts->check_all;

    if (run_semantic && ast && parser.error_count == 0) {
        Semantic sem;
        semantic_init(&sem, &arena, input_path);
        sem.check_mode   = true;
        sem.check_unused = opts->check_unused || opts->check_all;
        sem.check_dead   = opts->check_dead   || opts->check_all;

        semantic_analyze(&sem, ast);
        total_errors += sem.error_count;
    }

    /* ── Summary ──────────────────────────────────────── */
    if (total_errors == 0) {
        fprintf(stderr, "%s: OK\n", input_path);
    } else {
        fprintf(stderr, "\n%d error%s found in %s\n",
                total_errors, total_errors == 1 ? "" : "s", input_path);
    }

    free(source);
    arena_free(&arena);
    return total_errors > 0 ? 1 : 0;
}

/* ═════════════════════════════════════════════════════════════
 * Main
 * ═════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    Options opts;
    if (parse_args(argc, argv, &opts) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (opts.command == CMD_HELP) {
        print_usage(argv[0]);
        return 0;
    }
    if (opts.command == CMD_VERSION) {
        print_version();
        return 0;
    }

    /* ── CMD_CHECK: syntax/semantic check ───────────────── */
    if (opts.command == CMD_CHECK) {
        return check_file(opts.input_file, &opts);
    }

    /* ── CMD_RUN: compile to cache, execute ─────────────── */
    if (opts.command == CMD_RUN) {
        /* Format flags are not supported in run mode */
        if (opts.format != FMT_DEFAULT) {
            fprintf(stderr,
                "\033[33mwarning: --pe/--elf flags are ignored in run mode.\033[0m\n"
                "  Use 'axis build %s -o output --pe/--elf' to cross-compile.\n",
                opts.input_file);
            opts.format = FMT_DEFAULT;
        }

        /* Quick mode check */
        ProgramMode mode = detect_mode_quick(opts.input_file);
        if (mode == MODE_COMPILE) {
            fprintf(stderr,
                "error: '%s' is a compile-mode file.\n\n"
                "  This file uses 'mode compile' and cannot be run as a script.\n"
                "  Use 'axis build %s -o output' to compile it.\n",
                opts.input_file, opts.input_file);
            return 1;
        }

        char cache_path[1024];
        build_cache_path(opts.input_file, cache_path, sizeof(cache_path));

        /* Check cache freshness */
        if (!cache_is_fresh(opts.input_file, cache_path)) {
            ensure_cache_dir(opts.input_file);
            int rc = compile_to_exe(opts.input_file, cache_path, &opts);
            if (rc != 0) return rc;
        } else if (opts.verbose) {
            fprintf(stderr, "[axis] cache hit: '%s'\n", cache_path);
        }

        return run_executable(cache_path, opts.verbose);
    }

    /* ── CMD_BUILD: compile to specified output ─────────── */
    if (opts.command == CMD_BUILD) {
        char out_path[1024];
        if (opts.output_file) {
            snprintf(out_path, sizeof(out_path), "%s", opts.output_file);
        } else {
            default_output_path(opts.input_file, opts.format, out_path, sizeof(out_path));
        }

        int rc = compile_to_exe(opts.input_file, out_path, &opts);
        if (rc == 0) {
            fprintf(stderr, "%s -> %s\n", opts.input_file, out_path);
        }
        return rc;
    }

    /* ── CMD_NONE: auto-detect ──────────────────────────── */
    {
        ProgramMode mode = detect_mode_quick(opts.input_file);

        if (mode == MODE_SCRIPT) {
            /* Format flags are not supported when running scripts */
            if (opts.format != FMT_DEFAULT) {
                fprintf(stderr,
                    "\033[33mwarning: --pe/--elf flags are ignored when running scripts.\033[0m\n"
                    "  Use 'axis build %s -o output --pe/--elf' to cross-compile.\n",
                    opts.input_file);
                opts.format = FMT_DEFAULT;
            }

            /* Script mode → compile to cache + run */
            char cache_path[1024];
            build_cache_path(opts.input_file, cache_path, sizeof(cache_path));

            if (!cache_is_fresh(opts.input_file, cache_path)) {
                ensure_cache_dir(opts.input_file);
                int rc = compile_to_exe(opts.input_file, cache_path, &opts);
                if (rc != 0) return rc;
            } else if (opts.verbose) {
                fprintf(stderr, "[axis] cache hit: '%s'\n", cache_path);
            }

            return run_executable(cache_path, opts.verbose);
        } else {
            /* Compile mode → ask for output if not given */
            char out_path[1024];
            if (opts.output_file) {
                snprintf(out_path, sizeof(out_path), "%s", opts.output_file);
            } else {
                char default_out[1024];
                default_output_path(opts.input_file, opts.format, default_out, sizeof(default_out));
                fprintf(stderr, "Output file [%s]: ", default_out);
                if (fgets(out_path, sizeof(out_path), stdin)) {
                    /* Trim newline */
                    size_t len = strlen(out_path);
                    while (len > 0 && (out_path[len-1] == '\n' || out_path[len-1] == '\r'))
                        out_path[--len] = '\0';
                    /* Blank → use default */
                    if (len == 0) {
                        snprintf(out_path, sizeof(out_path), "%s", default_out);
                    }
                } else {
                    snprintf(out_path, sizeof(out_path), "%s", default_out);
                }
            }

            int rc = compile_to_exe(opts.input_file, out_path, &opts);
            if (rc == 0) {
                fprintf(stderr, "%s -> %s\n", opts.input_file, out_path);
            }
            return rc;
        }
    }
}
