#include <stdint.h>
#include <stdlib.h>
extern uint64_t __bennett_dev_clock_c;
extern uint32_t __bennett_dev_clock_bound, __bennett_dev_entropy_bound, __bennett_dev_block_bound;
extern const unsigned char *__bennett_dev_entropy_base, *__bennett_dev_block_base;
extern uint64_t __bennett_dev_entropy_size, __bennett_dev_entropy_pos, __bennett_dev_block_nsect;
extern void __bennett_dev_clock_read(void *);
extern void __bennett_dev_entropy_read(void *, uint64_t);
extern void __bennett_dev_block_read(uint64_t, void *);
extern void __bennett_dev_block_write(uint64_t, const void *);
static unsigned char short7[7], short511[511], image[512], output[514];
int main(int argc, char **argv) {
    __bennett_dev_clock_bound = __bennett_dev_entropy_bound = __bennett_dev_block_bound = 1;
    __bennett_dev_entropy_base = image; __bennett_dev_entropy_size = sizeof image;
    __bennett_dev_block_base = image; __bennett_dev_block_nsect = 1;
    if (argc > 1) {
        if (argv[1][0] == 'c') __bennett_dev_clock_read(short7);
        if (argv[1][0] == 'e') __bennett_dev_entropy_read(short7, 8);
        if (argv[1][0] == 'b') __bennett_dev_block_write(0, short511);
        if (argv[1][0] == 'r') __bennett_dev_block_read(0, short511);
        if (argv[1][0] == 'i') { __bennett_dev_block_base = short511; __bennett_dev_block_read(0, output); }
        if (argv[1][0] == 't') { __bennett_dev_entropy_base = short7; __bennett_dev_entropy_read(output, 8); }
        return 99;
    }
    for (unsigned i = 0; i < 512; ++i) image[i] = (unsigned char)(i*17+5);
    __bennett_dev_clock_c = UINT64_MAX; __bennett_dev_clock_read(output+1);
    for (unsigned i = 1; i <= 8; ++i) if (output[i] != 255) return 1;
    if (__bennett_dev_clock_c) return 2;
    __bennett_dev_entropy_read(output+1, 512);
    for (unsigned i = 0; i < 512; ++i) if (output[i+1] != image[i]) return 3;
    if (__bennett_dev_entropy_pos != 512) return 4;
    __bennett_dev_block_write(UINT64_MAX, output+1);
    __bennett_dev_block_read(UINT64_MAX, output+1);
    for (unsigned i = 0; i < 512; ++i) if (output[i+1] != image[i]) return 5;
    __bennett_dev_block_read(1, output+1);
    for (unsigned i = 0; i < 512; ++i) if (output[i+1]) return 6;
    __bennett_dev_block_read(0, output+1);
    for (unsigned i = 0; i < 512; ++i) if (output[i+1] != image[i]) return 7;
    return 0;
}
