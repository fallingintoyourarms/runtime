/* rt_pid1.c - minimal PID 1 implementation
 *
 * PID 1 in a PID namespace has special semantics:
 * - It is the reaper for orphaned processes.
 * - Default signal handling differs from normal processes.
 * This module implements a minimal init that forks/execs the requested command
 * and reaps all child processes.
 */

static volatile sig_atomic_t g_init_term = 0;

static void init_sig_handler(int sig)
{
    (void)sig;
    g_init_term = 1;
}

static int run_as_pid1(char **cmd_argv)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = init_sig_handler;
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) < 0) return -1;
    if (sigaction(SIGTERM, &sa, NULL) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        set_child_default_signals();
        execvp(cmd_argv[0], cmd_argv);
        perror("execvp");
        _exit(127);
    }

    int exit_status = 0;
    int child_exited = 0;

    for (;;) {
        int status;
        pid_t w = waitpid(-1, &status, 0);
        if (w < 0) {
            return -1;
        }

        if (w == pid) {
            child_exited = 1;
            exit_status = status;
        }

        /* Once the main child exits, keep reaping until no children remain. */
        if (child_exited) {
            int any_left = 0;
            for (;;) {
                pid_t w2 = waitpid(-1, &status, WNOHANG);
                if (w2 == 0) { any_left = 1; break; }
                if (w2 < 0) break;
            }
            if (!any_left) break;
        }

        /* On SIGINT/SIGTERM, forward a termination request to the main child. */
        if (g_init_term && !child_exited) {
            (void)kill(pid, SIGTERM);
        }
    }

    if (WIFEXITED(exit_status)) return WEXITSTATUS(exit_status);
    if (WIFSIGNALED(exit_status)) return 128 + WTERMSIG(exit_status);
    return 1;
}
