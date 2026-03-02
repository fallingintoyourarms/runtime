/* rt_state.c - extremely small process state tracking for ps/stop
 *
 * This is intentionally minimal: it records container monitor PIDs in a text file.
 * It is not a full supervisor/daemon; it exists to support the required CLI.
 */

#define RUNTIME_STATE_FILE "/tmp/runtime.state"

static int state_append(pid_t pid, const char *cmd0)
{
    int fd = open(RUNTIME_STATE_FILE, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return -1;

    char line[512];
    const char *c = (cmd0 && cmd0[0]) ? cmd0 : "?";
    int n = snprintf(line, sizeof(line), "%d %s\n", (int)pid, c);
    if (n <= 0 || (size_t)n >= sizeof(line)) {
        (void)close(fd);
        return -1;
    }

    ssize_t w = write(fd, line, (size_t)n);
    (void)close(fd);

    if (w < 0) return -1;
    if (w != n) return -1;
    return 0;
}

static int state_rewrite_without_pid(pid_t pid)
{
    int in = open(RUNTIME_STATE_FILE, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;

    char tmp_path[256];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", RUNTIME_STATE_FILE);
    if (n <= 0 || (size_t)n >= sizeof(tmp_path)) {
        (void)close(in);
        return -1;
    }

    int out = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
        (void)close(in);
        return -1;
    }

    char buf[4096];
    char line[1024];
    size_t line_len = 0;

    for (;;) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r <= 0) break;

        for (ssize_t i = 0; i < r; i++) {
            char ch = buf[i];
            if (line_len + 1 < sizeof(line)) {
                line[line_len++] = ch;
            }

            if (ch == '\n') {
                line[line_len] = '\0';

                /* Parse leading pid */
                char *end = NULL;
                long lp = strtol(line, &end, 10);
                if (end != line && lp != (long)pid) {
                    (void)write(out, line, line_len);
                }

                line_len = 0;
            }
        }
    }

    (void)close(in);
    (void)close(out);

    /* Best-effort atomic-ish replace. */
    if (rename(tmp_path, RUNTIME_STATE_FILE) < 0) {
        (void)unlink(tmp_path);
        return -1;
    }

    return 0;
}

static int state_list(void)
{
    int fd = open(RUNTIME_STATE_FILE, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("open state");
        return -1;
    }

    char buf[4096];
    for (;;) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            perror("read state");
            (void)close(fd);
            return -1;
        }
        if (r == 0) break;
        (void)write(STDOUT_FILENO, buf, (size_t)r);
    }

    (void)close(fd);
    return 0;
}
