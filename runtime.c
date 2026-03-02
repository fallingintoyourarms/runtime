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
#include "rt_net.c"
#include "rt_container.c"
#include "rt_cgroup.c"
#include "rt_state.c"

static void usage(FILE *f)
{
    fprintf(f,
        "usage:\n"
        "  runtime run --root <path> [--hostname <name>] [--mem <size>] [--cpu <fraction>] [--pids <n>] [--net] <cmd> [args...]\n"
        "  runtime ps\n"
        "  runtime stop <pid>\n"
    );
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "run") != 0) {
        if (strcmp(argv[1], "ps") == 0) {
            return (state_list() < 0) ? 1 : 0;
        }

        if (strcmp(argv[1], "stop") == 0) {
            if (argc < 3) {
                usage(stderr);
                return 1;
            }
            pid_t pid = (pid_t)atoi(argv[2]);
            if (pid <= 0) {
                fprintf(stderr, "invalid pid\n");
                return 1;
            }
            if (kill(pid, SIGTERM) < 0) {
                perror("kill");
                return 1;
            }
            (void)state_rewrite_without_pid(pid);
            return 0;
        }

        usage(stderr);
        return 1;
    }

    const char *rootfs = NULL;
    const char *hostname = "container";
    struct cgroup_limits cg;
    memset(&cg, 0, sizeof(cg));

    struct net_config net;
    memset(&net, 0, sizeof(net));
    net.enabled = 0;
    net.pipe_read_fd = -1;
    net.pipe_write_fd = -1;

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
        if (strcmp(argv[i], "--mem") == 0) {
            if (i + 1 >= argc) { usage(stderr); return 1; }
            cg.enabled = 1;
            cg.memory = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "--cpu") == 0) {
            if (i + 1 >= argc) { usage(stderr); return 1; }
            cg.enabled = 1;
            cg.cpu = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "--pids") == 0) {
            if (i + 1 >= argc) { usage(stderr); return 1; }
            cg.enabled = 1;
            cg.pids = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "--net") == 0) {
            net.enabled = 1;
            i += 1;
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
    cfg.net_enabled = net.enabled;
    cfg.net_pipe_fd = -1;

    /* clone(2) is used instead of fork(2) because clone can create new namespaces
     * in a single step. We want the child to start life already in:
     * - a new PID namespace (CLONE_NEWPID)
     * - a new UTS namespace (CLONE_NEWUTS)
     * - a new mount namespace (CLONE_NEWNS)
     */
    int flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD;
    if (net.enabled) {
        flags |= CLONE_NEWNET;
    }

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

    if (net.enabled) {
        int pfd[2];
        /* We use a pipe to pass veth/ip configuration from the parent to the child.
         * Set close-on-exec to avoid leaking fds into the container payload.
         */
#ifdef __linux__
        if (pipe2(pfd, O_CLOEXEC) < 0) {
            if (pipe(pfd) < 0) {
                perror("pipe");
                return 1;
            }
            (void)fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
            (void)fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
        }
#else
        if (pipe(pfd) < 0) {
            perror("pipe");
            return 1;
        }
        (void)fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
        (void)fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
#endif
        net.pipe_read_fd = pfd[0];
        net.pipe_write_fd = pfd[1];
        cfg.net_pipe_fd = net.pipe_read_fd;
    }

    pid_t pid = clone(child_main, stack_top, flags, &cfg);
    if (pid < 0) {
        perror("clone");
        return 1;
    }

    g_container_pid = pid;

    if (net.enabled) {
        /* Parent does not read from the pipe. */
        (void)close(net.pipe_read_fd);
        net.pipe_read_fd = -1;

        if (net_parent_setup(pid, &net) < 0) {
            fprintf(stderr, "network setup failed\n");
        }
    }

    if (cg.enabled) {
        if (cgroup_create(&cg, pid) < 0) {
            fprintf(stderr, "cgroup setup failed\n");
        }
    }

    (void)state_append(pid, cfg.argv[0]);

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

    (void)state_rewrite_without_pid(pid);
    cgroup_cleanup(&cg);
    net_cleanup(&net);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
