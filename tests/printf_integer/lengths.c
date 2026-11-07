#include <limits.h>
#include <stdio.h>
#include <unistd.h>

_Static_assert(LONG_MAX == 9223372036854775807L, "fixture requires LP64");
_Static_assert(LLONG_MAX == 9223372036854775807LL, "fixture requires 64-bit long long");

int main(void)
{
    printf("long:%ld|%li|%lu|%lx|%lX\n",
           LONG_MIN, LONG_MAX, ULONG_MAX, ULONG_MAX, ULONG_MAX);
    printf("long-long:%lld|%lli|%llu|%llx|%llX\n",
           LLONG_MIN, LLONG_MAX, ULLONG_MAX, ULLONG_MAX, ULLONG_MAX);
    printf("wide-fields:[%022ld][%24.20lld][%-24.20llu]\n",
           LONG_MIN, LLONG_MIN, ULLONG_MAX);
    /* More than six mixed GP arguments crosses the native register/stack split. */
    printf("walk:%d|%s|%ld|%u|%lld|%x|%c|%lu|%i|%llu|%X|%%\n",
           -1, "two", -3000000000L, 4U, -5000000000LL, 0x66U,
           'G', 8000000000UL, -9, 10000000000ULL, 0xABU);
    if (fflush(stdout) != 0)
        return 90;
    printf("FINAL lengths:%ld|%lld|%llu", -3000000000L,
           -5000000000LL, ULLONG_MAX);
    if (write(1, "RETURN\n", 7) != 7)
        return 91;
    return 23;
}
