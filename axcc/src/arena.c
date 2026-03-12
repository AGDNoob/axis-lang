/*
 * arena.c – Arena allocator implementation.
 */
#include "axis_arena.h"

static ArenaBlock *arena_new_block(size_t min_size)
{
    size_t cap = AXIS_MAX(min_size, ARENA_BLOCK_SIZE);
    ArenaBlock *b = (ArenaBlock *)malloc(sizeof(ArenaBlock) + cap);
    if (!b) axis_fatal("out of memory");
    b->next = NULL;
    b->used = 0;
    b->cap  = cap;
    return b;
}

void arena_init(Arena *a)
{
    a->head    = arena_new_block(ARENA_BLOCK_SIZE);
    a->current = a->head;
}

void arena_free(Arena *a)
{
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = a->current = NULL;
}

void *arena_alloc(Arena *a, size_t size)
{
    /* align to 8 bytes */
    size = AXIS_ALIGN(size, 8);

    ArenaBlock *cur = a->current;
    if (cur->used + size > cur->cap) {
        ArenaBlock *nb = arena_new_block(size);
        cur->next  = nb;
        a->current = nb;
        cur = nb;
    }
    void *ptr = cur->data + cur->used;
    cur->used += size;
    memset(ptr, 0, size);
    return ptr;
}

char *arena_strdup(Arena *a, const char *s)
{
    size_t len = strlen(s);
    char *p = (char *)arena_alloc(a, len + 1);
    memcpy(p, s, len + 1);
    return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n)
{
    char *p = (char *)arena_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}
