/*
 * axis_error.h – Centralized diagnostic reporting for AXIS.
 *
 * Provides Python-style error messages with:
 *   - Source line display with caret pointing to the error location
 *   - Colored output: errors in red, warnings in yellow, hints in green
 *   - Consistent formatting across lexer, parser, and semantic analyser
 */
#ifndef AXIS_ERROR_H
#define AXIS_ERROR_H

#include <stdarg.h>

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_HINT
} DiagSeverity;

/* Call once at program start to enable ANSI colors on Windows. */
void diag_init(void);

/* Return non-zero when ANSI color output is enabled (stderr is a TTY). */
int  diag_colors_enabled(void);

/* Report a diagnostic with source context (printf-style). */
void diag_report(DiagSeverity sev, const char *filename,
                 const char *source, int line, int col,
                 const char *fmt, ...);

/* Report a diagnostic with source context (va_list variant). */
void diag_reportv(DiagSeverity sev, const char *filename,
                  const char *source, int line, int col,
                  const char *fmt, va_list ap);

/* Report a simple diagnostic without source context (for CLI errors etc.). */
void diag_simple(DiagSeverity sev, const char *fmt, ...);

#endif /* AXIS_ERROR_H */
