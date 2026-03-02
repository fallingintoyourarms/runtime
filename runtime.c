#define _GNU_SOURCE

#include <unistd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * runtime.sh - minimal container runtime (Phase 1)
 *
 * Build: gcc runtime.c -o runtime.sh
 *
 * This file is the entry point and includes small implementation modules.
 * The modules are included directly to preserve a single-command build.
 */

#include "rt_util.c"
#include "rt_signals.c"
#include "rt_mount.c"
#include "rt_pid1.c"
#include "rt_container.c"

static void usage(FILE *f)
{
    fprintf(f,
        "usage:\n"
        "  runtime.sh run --root <path> [--hostname <name>] <cmd> [args...]\n"
        "  runtime.sh ps\n"
        "  runtime.sh stop <pid>\n"
    );
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "run") != 0) {
        if (strcmp(argv[1], "ps") == 0 || strcmp(argv[1], "stop") == 0) {
            fprintf(stderr, "subcommand not implemented in Phase 1\n");
            return 1;
        }
        usage(stderr);
        return 1;
    }

    const char *rootfs = NULL;
    const char *hostname = "container";

    int i = 2;
    while (i < argc) {
        if (strcmp(argv[i], "--root") == 0) {
            if (i + 1 >= argc) { usage(stderr); return 1; }
            rootfs = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "--hostname") == 0) {
            if (i + 1 >= argc) { usage(stderr); return 1; }
            hostname = argv[i + 1];
            i += 2;
            continue;
        }
        break;
    }

    if (rootfs == NULL) {
        fprintf(stderr, "--root <path> is required\n");
        return 1;
    }

    if (i >= argc) {
        fprintf(stderr, "missing command\n");
        return 1;
    }

    struct child_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.argv = &argv[i];
    cfg.rootfs = rootfs;
    cfg.hostname = hostname;

    /* clone(2) is used instead of fork(2) because clone can create new namespaces
     * in a single step. We want the child to start life already in:
     * - a new PID namespace (CLONE_NEWPID)
     * - a new UTS namespace (CLONE_NEWUTS)
     * - a new mount namespace (CLONE_NEWNS)
     */
    const int flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD;

    const size_t stack_size = 1024 * 1024;
    void *stack = malloc(stack_size);
    if (!stack) {
        perror("malloc");
        return 1;
    }

    /* Stack grows downward on x86_64; pass end of allocation. */
    void *stack_top = (char *)stack + stack_size;

    if (set_parent_signal_handlers() < 0) {
        perror("sigaction");
        return 1;
    }

    pid_t pid = clone(child_main, stack_top, flags, &cfg);
    if (pid < 0) {
        perror("clone");
        return 1;
    }

    g_container_pid = pid;

    /* Parent: wait and reap the child. This ensures we don't leave zombies.
     * The cloned child is the container monitor process (host PID = pid).
     */
    int status;
    pid_t w = waitpid(pid, &status, 0);

    free(stack);

    if (w < 0) {
        perror("waitpid");
        return 1;
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
