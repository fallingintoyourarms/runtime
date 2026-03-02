/* rt_signals.c - signal handling utilities
 *
 * The parent runtime forwards SIGINT/SIGTERM to the container init process.
 * Additionally, when the container init forks/execs the user command, it resets
 * signal handlers to defaults.
 */

static pid_t g_container_pid = -1;

static void parent_forward_handler(int sig)
{
    /* Forward termination signals to the container's init (the child of clone).
     * The child is PID 1 *inside* the namespace, but has a normal PID in the host.
     */
    if (g_container_pid > 0) {
        (void)kill(g_container_pid, sig);
    }
}

static int set_parent_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = parent_forward_handler;

    /* SA_RESTART reduces spurious EINTR failures for syscalls such as waitpid().
     * This also keeps the code simpler under the project constraints.
     */
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) < 0) return -1;
    if (sigaction(SIGTERM, &sa, NULL) < 0) return -1;

    return 0;
}

static void set_child_default_signals(void)
{
    /* When forking/execing the user command, reset handlers to defaults. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;

    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGCHLD, &sa, NULL);
}
