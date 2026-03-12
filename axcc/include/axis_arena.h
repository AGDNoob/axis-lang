/*
 * axis_arena.h – Fast bump-pointer arena allocator.
 *
 * All AST nodes, IR instructions, and temporary strings are allocated
 * from arenas so the compiler never needs individual free() calls.
 */
#ifndef AXIS_ARENA_H
#define AXIS_ARENA_H

#include "axis_common.h"

#define ARENA_BLOCK_SIZE (1024 * 1024)  /* 1 MiB per block */

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t             used;
    size_t             cap;
    uint8_t            data[];    /* flexible array member */
} ArenaBlock;

typedef struct {
    ArenaBlock *head;
    ArenaBlock *current;
} Arena;

void   arena_init(Arena *a);
void   arena_free(Arena *a);
void  *arena_alloc(Arena *a, size_t size);
char  *arena_strdup(Arena *a, const char *s);
char  *arena_strndup(Arena *a, const char *s, size_t n);

/* Convenience: allocate a zeroed struct */
#define ARENA_NEW(arena, T) ((T *)arena_alloc((arena), sizeof(T)))

#endif /* AXIS_ARENA_H */
