#include <limits.h>
#include <stdio.h>
#include <unistd.h>

_Static_assert(INT_MAX == 2147483647, "fixture requires 32-bit int");
_Static_assert(UINT_MAX == 4294967295U, "fixture requires 32-bit unsigned int");

int main(void)
{
    printf("basic:%s|%d|%i|%u|%x|%X|%c|%%\n",
           "musl", -42, -17, 42U, 0xabcdefU, 0xabcdefU, 'Z');
    printf("limits:%d|%d|%u|%x|%X\n",
           INT_MIN, INT_MAX, UINT_MAX, UINT_MAX, UINT_MAX);
    printf("zero:%d|%i|%u|%x|%X\n", 0, 0, 0U, 0U, 0U);
    if (fflush(stdout) != 0)
        return 90;
    printf("FINAL conversions:%s|%d|%c|%%", "buffered", -7, '!');
    /* This must precede FINAL: only normal main return flushes that line. */
    if (write(1, "RETURN\n", 7) != 7)
        return 91;
    return 0;
}
