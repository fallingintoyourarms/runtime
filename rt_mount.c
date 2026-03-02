/* rt_mount.c - mount namespace setup and rootfs switching
 *
 * This module configures mount propagation, switches the container root filesystem,
 * and mounts /proc inside the container.
 */

static int mount_make_private(void)
{
    /* mount(2) with MS_PRIVATE is required because mount namespaces start with
     * the parent mount propagation settings. If mounts are shared, mount/unmount
     * operations performed inside the container can propagate back to the host.
     * MS_REC ensures it applies recursively.
     */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        return -1;
    }
    return 0;
}

static int bind_mount_rootfs(const char *newroot)
{
    /* Bind-mount the rootfs onto itself so it becomes a mount point.
     * pivot_root(2) requires new_root to be a mount point.
     */
    if (mount(newroot, newroot, NULL, MS_BIND | MS_REC, NULL) < 0) {
        return -1;
    }
    return 0;
}

static int do_pivot_root(const char *newroot)
{
    /* pivot_root(2) swaps the current root mount with newroot.
     * This is more correct than chroot(2) because it can fully detach the old root.
     */
    char put_old[4096];
    int n = snprintf(put_old, sizeof(put_old), "%s/.oldroot", newroot);
    if (n <= 0 || (size_t)n >= sizeof(put_old)) {
        return -1;
    }

    if (ensure_dir(put_old, 0700) < 0) return -1;

    if (xsyscall(__NR_pivot_root, newroot, put_old) < 0) {
        return -1;
    }

    if (chdir("/") < 0) return -1;

    /* After pivot_root, the old root is mounted at /.oldroot in the new namespace.
     * MNT_DETACH lazily unmounts to allow processes with open fds to continue.
     */
    if (umount2("/.oldroot", MNT_DETACH) < 0) return -1;
    if (rmdir("/.oldroot") < 0) return -1;

    return 0;
}

static int do_chroot_fallback(const char *newroot)
{
    /* chroot(2) is a fallback when pivot_root(2) is unavailable.
     * It's weaker isolation because the old root can still be reachable
     * via open file descriptors, but is sufficient for a minimal Phase 1.
     */
    if (chdir(newroot) < 0) return -1;
    if (chroot(".") < 0) return -1;
    if (chdir("/") < 0) return -1;
    return 0;
}

static int mount_proc(void)
{
    if (ensure_dir("/proc", 0555) < 0) return -1;

    /* /proc is essential for many tools and for process introspection.
     * In a PID namespace, /proc must be mounted from inside the namespace
     * to reflect PIDs as seen by the container.
     */
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
        return -1;
    }
    return 0;
}
