/* Native comparison ABI v1. Driver-owned, required (no weak/default object).
 * Values come from the resolved §19.5 layout, not __bennett_rt_blk.<f>.
 * All fields are little-endian LP64 u64; object alignment is 8 bytes.
 */
#ifndef BENNETT_NATIVE_LAYOUT_H
#define BENNETT_NATIVE_LAYOUT_H
#include <stdint.h>
struct bennett_native_layout {
    uint64_t abi_version;
    uint64_t heap_base;
    uint64_t heap_limit_max;
};
extern const struct bennett_native_layout __bennett_rt_native_layout;
long __bennett_rt_native_os(long,long,long,long,long,long,long);
_Noreturn void __bennett_nz_fault(uint64_t);
#endif
