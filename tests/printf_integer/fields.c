#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("width:[%6d][%-6d][%06d][%06d]\n", 42, 42, 42, -42);
    printf("precision:[%.5d][%8.5d][%-8.5d][%08.5d]\n",
           -42, -42, -42, -42);
    printf("unsigned:[%08u][%.6x][%8.4X][%-8u]\n",
           42U, 0x2aU, 0x2aU, 42U);
    printf("text:[%8.3s][%-8.3s][%.0s][%4c][%-4c][%%]\n",
           "abcdef", "abcdef", "hidden", 'A', 'B');
    printf("empty:[%.0d][%.0i][%.0u][%.0x][%.0X][%5.0d]\n",
           0, 0, 0U, 0U, 0U, 0);
    printf("dynamic:[%*.*d][%*.*s][%0*d]\n",
           8, 4, -12, -7, 3, "abcdef", 6, -12);
    if (fflush(stdout) != 0)
        return 90;
    printf("FINAL fields:[%06d][%.0u][%-5.2s]", -9, 0U, "done");
    if (write(1, "RETURN\n", 7) != 7)
        return 91;
    return 7;
}
