#define _GNU_SOURCE
#include <errno.h>
#include <unistd.h>
#include <sys/uio.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#ifdef TEST_PRODUCTION
#include <stdint.h>
#include "device_effects.c"
static unsigned effects;
void __bennett_effect_console_write(const void *p, uint64_t n) { (void)p; (void)n; ++effects; }
_Noreturn void __bennett_nz_fault(uint64_t code) { (void)code; __builtin_trap(); }
extern void __bennett_rt_prepare_process(int, char **, char **);
#endif
int main(void) {
    struct iovec v = {(void *)"X", 1};
    for (int fd = 1; fd <= 2; ++fd) {
        CHECK(close(fd) == 0);
        errno = 0; CHECK(write(fd, "X", 1) == -1 && errno == EBADF);
        errno = 0; CHECK(write(fd, 0, 0) == -1 && errno == EBADF);
        errno = 0; CHECK(writev(fd, &v, 1) == -1 && errno == EBADF);
        errno = 0; CHECK(lseek(fd, 0, SEEK_SET) == -1 && errno == EBADF);
        errno = 0; CHECK(close(fd) == -1 && errno == EBADF);
    }
#ifdef TEST_PRODUCTION
    CHECK(effects == 0);
    static char *empty[] = {0};
    __bennett_rt_prepare_process(0, empty, empty);
    CHECK(write(1, 0, 0) == 0 && write(2, 0, 0) == 0 && effects == 0);
    CHECK(close(1) == 0 && close(1) == -1 && errno == EBADF);
#endif
    return 0;
}
