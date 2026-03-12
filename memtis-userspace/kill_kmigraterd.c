#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <sys/wait.h>

int syscall_htmm_migrater_start = 451;
int syscall_htmm_migrater_end = 452;

long htmm_migrater_start(void)
{
    return syscall(syscall_htmm_migrater_start);
}

long htmm_migrater_end(void)
{
    return syscall(syscall_htmm_migrater_end);
}

int main(int argc, char** argv)
{
    htmm_migrater_end();
    return 0;
}
