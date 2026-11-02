/* Hosted test-only journal for published model and allocator instructions. */
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern uint64_t __bennett_dev_clock_c;
extern uint32_t __bennett_dev_clock_bound;
extern void __bennett_dev_clock_read(void *);
extern const unsigned char *__bennett_dev_entropy_base, *__bennett_dev_block_base;
extern uint64_t __bennett_dev_entropy_size, __bennett_dev_entropy_pos, __bennett_dev_block_nsect;
extern uint32_t __bennett_dev_entropy_bound, __bennett_dev_block_bound;
extern void *__bennett_dev_block_overlay;
extern void __bennett_dev_entropy_read(void *, uint64_t);
extern void __bennett_dev_block_read(uint64_t, void *);
extern void __bennett_dev_block_write(uint64_t, const void *);

static _Alignas(4096) unsigned char heap[16384];
static uint64_t cursor, count;
static struct allocation { void *p; uint64_t n, live; } allocations[256];
static struct entry { void *p; size_t n; unsigned char old[1024]; } history[16384];
static size_t history_n;
void test_record(void *p, uint64_t n) {
    assert(n <= 1024 && history_n < 16384);
    struct entry *r = &history[history_n++];
    r->p = p; r->n = n; memcpy(r->old, p, n);
}
static jmp_buf fault_target;
static uint64_t expected_fault;
_Noreturn void __bennett_nz_fault(uint64_t code) {
    assert(code == expected_fault && code);
    longjmp(fault_target, 1);
}
void *__bennett_nz_alloc(uint64_t n) {
    uint64_t extent = n ? n : 1;
    assert(extent <= sizeof heap - cursor && count < 256);
    void *p = heap + cursor;
    test_record(&cursor, sizeof cursor); test_record(&count, sizeof count);
    test_record(&allocations[count], sizeof allocations[count]); test_record(p, extent);
    allocations[count++] = (struct allocation){p, n, 1};
    cursor += extent; memset(p, 0, extent); return p;
}
void __bennett_nz_free(void *p) {
    if (!p) return;
    for (uint64_t i = 0; i < count; ++i) if (allocations[i].p == p && allocations[i].live) {
        test_record(&allocations[i].live, sizeof allocations[i].live);
        allocations[i].live = 0; return;
    }
    __bennett_nz_fault(7);
}
static void reverse(void) {
    while (history_n) { struct entry *r = &history[--history_n]; memcpy(r->p, r->old, r->n); }
}
#define FAULT(code, operation) do { \
    size_t before = history_n; expected_fault = (code); \
    if (!setjmp(fault_target)) { operation; assert(!"missing fault"); } \
    assert(history_n == before); expected_fault = 0; \
} while (0)
static unsigned char out[514], tape[17], image[2048], sector[512];
static void check_out_reset(void) {
    for (unsigned i = 0; i < sizeof out; ++i) assert(out[i] == 0xa5);
}
static void run(void) {
    uint64_t clocks[] = {0, 1, UINT64_C(0x8877665544332211), UINT64_MAX};
    for (unsigned j = 0; j < 4; ++j) {
        __bennett_dev_clock_c = clocks[j];
        __bennett_dev_clock_read(out+1);
        for (unsigned i = 0; i < 8; ++i) assert(out[i+1] == (unsigned char)(clocks[j] >> (8*i)));
        assert(__bennett_dev_clock_c == clocks[j]+1 && out[0] == 0xa5 && out[9] == 0xa5);
        reverse(); check_out_reset(); assert(__bennett_dev_clock_c == clocks[j]);
    }
    __bennett_dev_clock_bound = 0; FAULT(8, __bennett_dev_clock_read(0));
    __bennett_dev_clock_bound = 1; FAULT(7, __bennett_dev_clock_read(0));
    __bennett_dev_clock_c = UINT64_MAX;
    __bennett_dev_clock_read(out+1); __bennett_dev_clock_read(out+9);
    assert(__bennett_dev_clock_c == 1);
    for (unsigned i = 1; i <= 8; ++i) assert(out[i] == 255 && !out[i+8]);
    reverse(); check_out_reset(); assert(__bennett_dev_clock_c == UINT64_MAX);
    for (uint64_t pos = 0; pos <= sizeof tape; ++pos) {
        for (uint64_t len = 0; len <= sizeof tape+1; ++len) {
            __bennett_dev_entropy_pos = pos;
            if (len > sizeof tape-pos) { FAULT(10, __bennett_dev_entropy_read((void *)1, len)); }
            else {
                __bennett_dev_entropy_read(out+1, len);
                assert(__bennett_dev_entropy_pos == pos+len && out[0] == 0xa5 && out[len+1] == 0xa5);
                assert(!memcmp(out+1, tape+pos, len));
                reverse(); assert(__bennett_dev_entropy_pos == pos); check_out_reset();
            }
        }
    }
    __bennett_dev_entropy_pos = 0;
    __bennett_dev_entropy_read(out+1, 8); __bennett_dev_entropy_read(out+9, 9);
    assert(!memcmp(out+1, tape, sizeof tape) && __bennett_dev_entropy_pos == sizeof tape);
    reverse(); assert(!__bennett_dev_entropy_pos); check_out_reset();
    __bennett_dev_entropy_bound = 0; FAULT(8, __bennett_dev_entropy_read(0, 0));
    __bennett_dev_entropy_bound = 1;
    __bennett_dev_entropy_pos = 18; FAULT(7, __bennett_dev_entropy_read(0, 0));
    __bennett_dev_entropy_pos = 0; FAULT(7, __bennett_dev_entropy_read(0, 1));
    FAULT(10, __bennett_dev_entropy_read((void *)1, UINT64_MAX));
    __bennett_dev_entropy_base = 0; FAULT(7, __bennett_dev_entropy_read(out, 1));
    __bennett_dev_entropy_size = 0; __bennett_dev_entropy_read(0, 0); reverse();
    __bennett_dev_entropy_base = tape; __bennett_dev_entropy_size = sizeof tape;
    uint64_t sectors[] = {0, 1, 3, 4, UINT64_MAX};
    for (unsigned j = 0; j < 5; ++j) {
        uint64_t s = sectors[j]; __bennett_dev_block_read(s, out+1);
        for (unsigned i = 0; i < 512; ++i) assert(out[i+1] == (s < 4 ? image[s*512+i] : 0));
        reverse(); check_out_reset();
    }
    /* New, overwritten, distant and maximum-number sectors; disk is immutable. */
    for (unsigned j = 0; j < 5; ++j) __bennett_dev_block_write(sectors[j], sector);
    uint64_t after_first_writes = cursor;
    for (unsigned j = 0; j < 5; ++j) {
        __bennett_dev_block_read(sectors[j], out+1); assert(!memcmp(out+1, sector, 512));
        __bennett_dev_block_write(sectors[j], image+512);
        __bennett_dev_block_read(sectors[j], out+1); assert(!memcmp(out+1, image+512, 512));
    }
    assert(cursor == after_first_writes && __bennett_dev_block_overlay);
    for (unsigned i = 0; i < sizeof image; ++i) assert(image[i] == (unsigned char)(i*13+i/512));
    reverse(); check_out_reset(); assert(!__bennett_dev_block_overlay && !cursor && !count);
    for (unsigned i = 0; i < sizeof heap; ++i) assert(heap[i] == 0xaa);
    static const struct allocation empty[256]; assert(!memcmp(allocations, empty, sizeof empty));
    __bennett_dev_block_bound = 0;
    FAULT(8, __bennett_dev_block_read(0, 0)); FAULT(8, __bennett_dev_block_write(0, 0));
    __bennett_dev_block_bound = 1;
    FAULT(7, __bennett_dev_block_read(0, 0)); FAULT(7, __bennett_dev_block_write(0, 0));
    assert(!cursor && !count && !__bennett_dev_block_overlay);
    __bennett_dev_block_base = 0; FAULT(7, __bennett_dev_block_read(0, out+1));
    __bennett_dev_block_nsect = 0; __bennett_dev_block_read(0, out+1);
    for (unsigned i = 0; i < 512; ++i) assert(!out[i+1]);
    reverse(); check_out_reset();
    __bennett_dev_block_base = image; __bennett_dev_block_nsect = 4;
    __bennett_dev_block_nsect = UINT64_MAX;
    FAULT(7, __bennett_dev_block_read(UINT64_MAX/512+1, out+1));
    __bennett_dev_block_nsect = 4;
}
int main(void) {
    memset(heap, 0xaa, sizeof heap); memset(out, 0xa5, sizeof out);
    for (unsigned i = 0; i < sizeof tape; ++i) tape[i] = (unsigned char)(i*19+7);
    for (unsigned i = 0; i < sizeof image; ++i) image[i] = (unsigned char)(i*13+i/512);
    for (unsigned i = 0; i < sizeof sector; ++i) sector[i] = (unsigned char)(i*7+3);
    __bennett_dev_clock_bound = __bennett_dev_entropy_bound = __bennett_dev_block_bound = 1;
    __bennett_dev_entropy_base = tape; __bennett_dev_entropy_size = sizeof tape;
    __bennett_dev_block_base = image; __bennett_dev_block_nsect = 4;
    for (unsigned pass = 0; pass < 2; ++pass) run();
    return 0;
}
