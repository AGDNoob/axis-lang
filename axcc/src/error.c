/*
 * error.c – Centralized diagnostic reporting for AXIS.
 *
 * Produces Python-style error output:
 *
 *   File "program.axis", line 15
 *       set x to "hello" + 5
 *                         ^
 *   Error: type mismatch in addition
 *
 * Colors:  Error → red,  Warning → yellow,  Hint → green
 */

#include "axis_error.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#else
#  include <unistd.h>
#endif

/* ── ANSI escape codes ──────────────────────────────────── */

#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_DIM     "\033[2m"

/* ── State ──────────────────────────────────────────────── */

static int colors_enabled = 0;

/* ── Init ───────────────────────────────────────────────── */

void diag_init(void)
{
#ifdef _WIN32
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hErr, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(hErr, mode))
                colors_enabled = 1;
        }
    }
#else
    if (isatty(fileno(stderr)))
        colors_enabled = 1;
#endif
}

/* ── Helpers ────────────────────────────────────────────── */

static const char *sev_label(DiagSeverity sev)
{
    switch (sev) {
    case DIAG_ERROR:   return "Error";
    case DIAG_WARNING: return "Warning";
    case DIAG_HINT:    return "Hint";
    }
    return "Error";
}

static const char *sev_color(DiagSeverity sev)
{
    if (!colors_enabled) return "";
    switch (sev) {
    case DIAG_ERROR:   return ANSI_BOLD ANSI_RED;
    case DIAG_WARNING: return ANSI_BOLD ANSI_YELLOW;
    case DIAG_HINT:    return ANSI_BOLD ANSI_GREEN;
    }
    return "";
}

static const char *col_reset(void)
{
    return colors_enabled ? ANSI_RESET : "";
}

static const char *col_cyan(void)
{
    return colors_enabled ? ANSI_CYAN : "";
}

static const char *col_dim(void)
{
    return colors_enabled ? ANSI_DIM : "";
}

static const char *col_bold(void)
{
    return colors_enabled ? ANSI_BOLD : "";
}

/*
 * Find the start of a given 1-based line number in the source buffer.
 * Returns NULL if source is NULL or line is out of range.
 */
static const char *find_line_start(const char *src, int target_line)
{
    if (!src || target_line <= 0) return NULL;
    const char *p = src;
    int line = 1;
    while (*p && line < target_line) {
        if (*p == '\n') line++;
        p++;
    }
    return (*p || line == target_line) ? p : NULL;
}

/*
 * Print a source line, expanding tabs to spaces for alignment.
 */
static void print_source_line(FILE *f, const char *line_start)
{
    for (const char *p = line_start; *p && *p != '\n' && *p != '\r'; p++) {
        if (*p == '\t') {
            fputs("    ", f);  /* tab → 4 spaces */
        } else {
            fputc(*p, f);
        }
    }
    fputc('\n', f);
}

/*
 * Print the caret line: spaces up to col, then ^ in the severity color.
 * col is 1-based; we need to account for tabs in the source line.
 */
static void print_caret(FILE *f, const char *line_start, int col,
                         DiagSeverity sev)
{
    /* Calculate display offset, expanding tabs */
    int display_col = 0;
    if (line_start) {
        const char *p = line_start;
        for (int i = 1; i < col && *p && *p != '\n'; i++, p++) {
            display_col += (*p == '\t') ? 4 : 1;
        }
    } else {
        display_col = col > 0 ? col - 1 : 0;
    }

    fprintf(f, "    ");  /* match source line indent */
    for (int i = 0; i < display_col; i++)
        fputc(' ', f);
    fprintf(f, "%s^%s\n", sev_color(sev), col_reset());
}

/* ── Core reporting ─────────────────────────────────────── */

void diag_reportv(DiagSeverity sev, const char *filename,
                  const char *source, int line, int col,
                  const char *fmt, va_list ap)
{
    /* Line 1: File "name", line N */
    fprintf(stderr, "\n  %sFile \"%s%s%s%s\", line %s%d%s\n",
            col_dim(), col_reset(),
            col_cyan(), filename ? filename : "<unknown>", col_reset(),
            col_bold(), line, col_reset());

    /* Line 2: Source line (indented) */
    const char *line_start = find_line_start(source, line);
    if (line_start) {
        fprintf(stderr, "    ");
        print_source_line(stderr, line_start);

        /* Line 3: Caret */
        print_caret(stderr, line_start, col, sev);
    }

    /* Line 4: Severity: message */
    fprintf(stderr, "  %s%s:%s ", sev_color(sev), sev_label(sev), col_reset());
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void diag_report(DiagSeverity sev, const char *filename,
                 const char *source, int line, int col,
                 const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag_reportv(sev, filename, source, line, col, fmt, ap);
    va_end(ap);
}

void diag_simple(DiagSeverity sev, const char *fmt, ...)
{
    fprintf(stderr, "\n  %s%s:%s ", sev_color(sev), sev_label(sev), col_reset());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
