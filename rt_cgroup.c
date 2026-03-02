/* rt_cgroup.c - cgroups v2 (Phase 2)
 *
 * Responsibilities:
 * - Create a cgroup directory under /sys/fs/cgroup
 * - Configure memory.max, cpu.max, pids.max
 * - Attach the container process (host PID) to the cgroup
 * - Cleanup cgroup directory after the container exits
 *
 * Notes:
 * - This expects a unified hierarchy (cgroups v2).
 * - If cgroups are not available, limits are treated as best-effort.
 */

static int write_file_str(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    size_t len = strlen(s);
    ssize_t w = write(fd, s, len);
    (void)close(fd);

    if (w < 0) return -1;
    if ((size_t)w != len) return -1;
    return 0;
}

static int path_exists(const char *path)
{
    return (access(path, F_OK) == 0) ? 1 : 0;
}

static int parse_mem_bytes(const char *s, unsigned long long *out)
{
    /* Accept: 123, 123k, 123m, 123g (case-insensitive). */
    if (!s || !*s) return -1;

    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s) return -1;

    unsigned long long mul = 1;
    if (*end != '\0') {
        char c = *end;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');

        if (c == 'k') mul = 1024ULL;
        else if (c == 'm') mul = 1024ULL * 1024ULL;
        else if (c == 'g') mul = 1024ULL * 1024ULL * 1024ULL;
        else return -1;

        end++;
        if (*end != '\0') return -1;
    }

    *out = v * mul;
    return 0;
}

static int parse_cpu_to_cpu_max(const char *s, char *out, size_t out_sz)
{
    /* Convert a CPU fraction to cpu.max format: "quota period".
     * Example: 0.5 => "50000 100000" (50ms quota per 100ms period)
     *
     * Limitations:
     * - This is intentionally simple and uses strtod().
     */
    if (!s || !*s) return -1;

    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0') return -1;
    if (v <= 0.0) return -1;

    const unsigned long period = 100000; /* 100ms */
    unsigned long quota;

    if (v >= 1000.0) {
        /* Treat very large values as effectively unlimited. */
        quota = 0;
    } else {
        double q = v * (double)period;
        if (q < 1000.0) q = 1000.0; /* keep it non-trivial */
        quota = (unsigned long)q;
    }

    if (quota == 0) {
        /* "max" means no limit. */
        int n = snprintf(out, out_sz, "max %lu", period);
        if (n <= 0 || (size_t)n >= out_sz) return -1;
        return 0;
    }

    int n = snprintf(out, out_sz, "%lu %lu", quota, period);
    if (n <= 0 || (size_t)n >= out_sz) return -1;
    return 0;
}

struct cgroup_limits {
    int enabled;
    const char *memory;
    const char *cpu;
    const char *pids;

    char path[512];
};

static int cgroupv2_is_available(void)
{
    /* Minimal check: /sys/fs/cgroup exists and cgroup.controllers exists. */
    if (!path_exists("/sys/fs/cgroup")) return 0;
    if (!path_exists("/sys/fs/cgroup/cgroup.controllers")) return 0;
    return 1;
}

static int cgroup_create(struct cgroup_limits *cg, pid_t container_pid)
{
    if (!cg || !cg->enabled) return 0;

    if (!cgroupv2_is_available()) {
        fprintf(stderr, "cgroups v2 not available; continuing without limits\n");
        cg->enabled = 0;
        return 0;
    }

    int n = snprintf(cg->path, sizeof(cg->path), "/sys/fs/cgroup/runtime.sh-%d", (int)container_pid);
    if (n <= 0 || (size_t)n >= sizeof(cg->path)) return -1;

    if (mkdir(cg->path, 0755) < 0) {
        perror("mkdir cgroup");
        cg->enabled = 0;
        return 0;
    }

    /* memory.max */
    if (cg->memory && cg->memory[0]) {
        unsigned long long bytes;
        if (parse_mem_bytes(cg->memory, &bytes) == 0) {
            char p[600];
            char v[64];
            (void)snprintf(p, sizeof(p), "%s/memory.max", cg->path);
            (void)snprintf(v, sizeof(v), "%llu", bytes);
            if (write_file_str(p, v) < 0) {
                perror("write memory.max");
            }
        } else {
            fprintf(stderr, "invalid --mem value: %s\n", cg->memory);
        }
    }

    /* pids.max */
    if (cg->pids && cg->pids[0]) {
        char p[600];
        (void)snprintf(p, sizeof(p), "%s/pids.max", cg->path);
        if (write_file_str(p, cg->pids) < 0) {
            perror("write pids.max");
        }
    }

    /* cpu.max */
    if (cg->cpu && cg->cpu[0]) {
        char cpu_max[64];
        if (parse_cpu_to_cpu_max(cg->cpu, cpu_max, sizeof(cpu_max)) == 0) {
            char p[600];
            (void)snprintf(p, sizeof(p), "%s/cpu.max", cg->path);
            if (write_file_str(p, cpu_max) < 0) {
                perror("write cpu.max");
            }
        } else {
            fprintf(stderr, "invalid --cpu value: %s\n", cg->cpu);
        }
    }

    /* Attach process by writing PID to cgroup.procs */
    {
        char p[600];
        char v[64];
        (void)snprintf(p, sizeof(p), "%s/cgroup.procs", cg->path);
        (void)snprintf(v, sizeof(v), "%d", (int)container_pid);
        if (write_file_str(p, v) < 0) {
            perror("write cgroup.procs");
        }
    }

    return 0;
}

static void cgroup_cleanup(struct cgroup_limits *cg)
{
    if (!cg || !cg->enabled) return;

    if (cg->path[0] == '\0') return;

    if (rmdir(cg->path) < 0) {
        /* Best-effort cleanup. */
        perror("rmdir cgroup");
    }
}
