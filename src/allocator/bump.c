/* Trivial deterministic bump allocator: fixed base, monotonic, dead-marking free.
 *
 * Determinism requirements: the base address is a compile-time constant, the
 * allocation cursor only moves forward, and free() never reuses memory; it
 * only marks the block dead. Same call sequence => same addresses. */
#include <stddef.h>
#include <stdint.h>

#ifndef BENNETT_HEAP_BASE
#define BENNETT_HEAP_BASE ((uintptr_t)0x10000000u)
#endif
#ifndef BENNETT_HEAP_SIZE
#define BENNETT_HEAP_SIZE ((size_t)0x01000000u)
#endif

#define HDR_ALIGN 16u
#define DEAD 0x1u

typedef struct { size_t size; size_t flags; } bennett_hdr;

static uintptr_t cursor = BENNETT_HEAP_BASE;

static size_t align_up(size_t n) { return (n + (HDR_ALIGN - 1)) & ~(size_t)(HDR_ALIGN - 1); }

void *bennett_alloc(size_t n) {
  size_t need = align_up(sizeof(bennett_hdr)) + align_up(n);
  if (need < n || cursor + need > BENNETT_HEAP_BASE + BENNETT_HEAP_SIZE) {
    return NULL; /* exhaustion is explicit; callers must check */
  }
  bennett_hdr *h = (bennett_hdr *)cursor;
  h->size = n;
  h->flags = 0;
  cursor += need;
  return (void *)((uintptr_t)h + align_up(sizeof(bennett_hdr)));
}

void bennett_free(void *p) {
  if (!p) return;
  bennett_hdr *h = (bennett_hdr *)((uintptr_t)p - align_up(sizeof(bennett_hdr)));
  h->flags |= DEAD; /* never reused */
}
