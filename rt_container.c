/* rt_container.c - container init setup
 *
 * This module implements the clone(2) child entrypoint.
 * The child configures namespaces, switches the root filesystem, mounts /proc,
 * and then runs a minimal PID 1 supervisor.
 */

struct child_config {
    char **argv;
    const char *rootfs;
    const char *hostname;
};

static int child_main(void *arg)
{
    struct child_config *cfg = (struct child_config *)arg;

    /* sethostname(2) uses the UTS namespace (CLONE_NEWUTS) so it only affects
     * the container, not the host.
     */
    if (cfg->hostname && cfg->hostname[0]) {
        if (sethostname(cfg->hostname, strlen(cfg->hostname)) < 0) {
            die_perror("sethostname");
        }
    }

    if (mount_make_private() < 0) {
        die_perror("mount MS_PRIVATE");
    }

    if (cfg->rootfs == NULL) {
        die_msg("rootfs is required");
    }

    if (bind_mount_rootfs(cfg->rootfs) < 0) {
        die_perror("bind-mount rootfs");
    }

    if (do_pivot_root(cfg->rootfs) < 0) {
        /* If pivot_root fails, attempt chroot fallback. */
        if (do_chroot_fallback(cfg->rootfs) < 0) {
            die_perror("pivot_root/chroot");
        }
    }

    if (mount_proc() < 0) {
        die_perror("mount /proc");
    }

    /* Now act as PID 1 and supervise the requested command. */
    int rc = run_as_pid1(cfg->argv);
    _exit(rc);
}
