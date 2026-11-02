/* Dialect 0.2.12 §11.3. Bindings are prepared by the selected module's
 * wrapper, outside control. State and allocations are ordinary reversible
 * memory; no host device, production undo log or hidden history lives here. */
#include <stdint.h>
#include <stdlib.h>

extern _Noreturn void __bennett_nz_fault(uint64_t);

uint64_t __bennett_dev_clock_c;
uint32_t __bennett_dev_clock_bound;

void __bennett_dev_clock_read(void *p) {
    if (!__bennett_dev_clock_bound) __bennett_nz_fault(8);
    if (!p) __bennett_nz_fault(7);
    uint64_t c = __bennett_dev_clock_c;
    unsigned char *bytes = p;
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (unsigned char)(c >> (8*i));
    __bennett_dev_clock_c = c+1;
}

const unsigned char *__bennett_dev_entropy_base;
uint64_t __bennett_dev_entropy_size, __bennett_dev_entropy_pos;
uint32_t __bennett_dev_entropy_bound;

void __bennett_dev_entropy_read(void *p, uint64_t len) {
    if (!__bennett_dev_entropy_bound) __bennett_nz_fault(8);
    uint64_t pos = __bennett_dev_entropy_pos, size = __bennett_dev_entropy_size;
    if (pos > size) __bennett_nz_fault(7);
    /* Exhaustion precedes any read/write, including destination validation. */
    if (len > size-pos) __bennett_nz_fault(10);
    if (!len) return;
    if (!p || !__bennett_dev_entropy_base) __bennett_nz_fault(7);
    unsigned char *bytes = p;
    for (uint64_t i = 0; i < len; ++i) bytes[i] = __bennett_dev_entropy_base[pos+i];
    __bennett_dev_entropy_pos = pos+len;
}

struct block_sector {
    uint64_t sector;
    struct block_sector *next;
    unsigned char bytes[512];
};
const unsigned char *__bennett_dev_block_base;
uint64_t __bennett_dev_block_nsect;
uint32_t __bennett_dev_block_bound;
struct block_sector *__bennett_dev_block_overlay;

static struct block_sector *find_sector(uint64_t sector) {
    for (struct block_sector *entry = __bennett_dev_block_overlay; entry; entry = entry->next)
        if (entry->sector == sector) return entry;
    return 0;
}

void __bennett_dev_block_read(uint64_t sector, void *p) {
    if (!__bennett_dev_block_bound) __bennett_nz_fault(8);
    if (!p) __bennett_nz_fault(7);
    struct block_sector *entry = find_sector(sector);
    const unsigned char *source = 0;
    if (entry) source = entry->bytes;
    else if (sector < __bennett_dev_block_nsect) {
        if (!__bennett_dev_block_base || sector > UINT64_MAX/512) __bennett_nz_fault(7);
        source = __bennett_dev_block_base + sector*512;
    }
    unsigned char *bytes = p;
    for (unsigned i = 0; i < 512; ++i) bytes[i] = source ? source[i] : 0;
}

void __bennett_dev_block_write(uint64_t sector, const void *p) {
    if (!__bennett_dev_block_bound) __bennett_nz_fault(8);
    if (!p) __bennett_nz_fault(7);
    struct block_sector *entry = find_sector(sector);
    if (!entry) {
        entry = malloc(sizeof *entry);
        entry->sector = sector;
        entry->next = __bennett_dev_block_overlay;
        __bennett_dev_block_overlay = entry;
    }
    const unsigned char *bytes = p;
    for (unsigned i = 0; i < 512; ++i) entry->bytes[i] = bytes[i];
}
