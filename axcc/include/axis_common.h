/*
 * axis_common.h – Shared types, macros, and utilities for the AXIS compiler.
 */
#ifndef AXIS_COMMON_H
#define AXIS_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ── Compiler version ─────────────────────────────────────── */
#define AXIS_VERSION_MAJOR 2
#define AXIS_VERSION_MINOR 0
#define AXIS_VERSION_PATCH 0
#define AXIS_VERSION_STR   "2.0.0-alpha"

/* ── Utility macros ───────────────────────────────────────── */
#define AXIS_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define AXIS_UNUSED(x)    ((void)(x))
#define AXIS_ALIGN(n, a)  (((n) + (a) - 1) & ~((a) - 1))
#define AXIS_MAX(a, b)    ((a) > (b) ? (a) : (b))
#define AXIS_MIN(a, b)    ((a) < (b) ? (a) : (b))

/* ── Fatal error ──────────────────────────────────────────── */
static inline _Noreturn void axis_fatal(const char *msg)
{
    fprintf(stderr, "axisc: fatal: %s\n", msg);
    exit(1);
}

/* ── Source location ──────────────────────────────────────── */
typedef struct {
    int line;
    int col;
} SrcLoc;

/* ── AXIS type system tag ─────────────────────────────────── */
typedef enum {
    TYPE_VOID = 0,
    TYPE_BOOL,
    TYPE_I8,  TYPE_I16,  TYPE_I32,  TYPE_I64,
    TYPE_U8,  TYPE_U16,  TYPE_U32,  TYPE_U64,
    TYPE_STR,
    TYPE_ARRAY,          /* element_type + count   */
    TYPE_FIELD,          /* named struct           */
    TYPE_ENUM,           /* named enum             */
    TYPE_COUNT_
} AxisTypeKind;

typedef struct AxisType {
    AxisTypeKind kind;
    union {
        struct { struct AxisType *elem; int count; }   arr;   /* TYPE_ARRAY */
        const char *name;                                     /* TYPE_FIELD / TYPE_ENUM */
    };
} AxisType;

static inline bool axis_type_is_integer(AxisTypeKind k)
{
    return k >= TYPE_I8 && k <= TYPE_U64;
}

static inline bool axis_type_is_signed(AxisTypeKind k)
{
    return k >= TYPE_I8 && k <= TYPE_I64;
}

static inline int axis_type_size(AxisTypeKind k)
{
    switch (k) {
    case TYPE_BOOL: case TYPE_I8:  case TYPE_U8:  return 1;
    case TYPE_I16:  case TYPE_U16:                return 2;
    case TYPE_I32:  case TYPE_U32:                return 4;
    case TYPE_I64:  case TYPE_U64: case TYPE_STR: return 8;
    default: return 0;
    }
}

#endif /* AXIS_COMMON_H */
