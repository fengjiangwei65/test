/* Test sysconf, rongtao 2021年 02月 09日 星期二 14:45:44 CST */
#include <stdio.h>
#include <unistd.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    printf("_SC_XOPEN_SHM = %ld\n", sysconf(_SC_XOPEN_SHM));
    return 0;
}
