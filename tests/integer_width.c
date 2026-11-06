/* Invalid internal widths must fault before operand access or stack allocation. */
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "../src/runtime/integer.h"
_Noreturn void __bennett_nz_fault(uint64_t code) { _exit(100+code); }
int main(int argc,char **argv) {
    if(argc!=3) return 1;
    uint32_t width=strtoul(argv[1],0,10), op=strtoul(argv[2],0,10);
    __bennett_rt_integer_op(0,0,0,0,width,op,0);
    return 2;
}
