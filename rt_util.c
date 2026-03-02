/* rt_util.c - helpers and pivot_root syscall glue
 *
 * This module provides small utility functions and a minimal wrapper for invoking
 * pivot_root(2) via syscall(2). The project constraints avoid extra headers and
 * helper libraries.
 */

#ifndef __NR_pivot_root
#define __NR_pivot_root 155
#endif

static int xsyscall(long n, const char *a, const char *b)
{
    /* syscall(2) is declared by glibc via unistd.h when _GNU_SOURCE is set. */
    return (int)syscall(n, a, b);
}

static void die_perror(const char *msg)
{
    perror(msg);
    _exit(1);
}

static void die_msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    _exit(1);
}

static int ensure_dir(const char *path, mode_t mode)
{
    if (mkdir(path, mode) == 0) return 0;

    /* If already exists, accept. */
    if (access(path, F_OK) == 0) return 0;

    return -1;
}
