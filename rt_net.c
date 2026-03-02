/* rt_net.c - network namespace setup (Phase 3)
 *
 * This implementation uses system("ip ...") (iproute2) on the host.
 * It creates a veth pair, moves one end into the container's netns, and then
 * the container configures its side using system("ip ...") inside its namespace.
 */

struct net_config {
    int enabled;
    char host_if[64];
    char cont_if[64];
    char host_ip_cidr[64];
    char cont_ip_cidr[64];
    char gw_ip[64];

    int pipe_read_fd;
    int pipe_write_fd;
};

static int net_parent_setup(pid_t container_pid, struct net_config *net)
{
    if (!net || !net->enabled) return 0;

    /* Interface names must be short (IFNAMSIZ is 16); keep them compact. */
    (void)snprintf(net->host_if, sizeof(net->host_if), "rt%dh", (int)container_pid);
    (void)snprintf(net->cont_if, sizeof(net->cont_if), "rt%dc", (int)container_pid);

    /* Default addressing (can be extended later): 10.200.X.Y based on pid mod 250. */
    int oct = ((int)container_pid % 250) + 1;
    (void)snprintf(net->host_ip_cidr, sizeof(net->host_ip_cidr), "10.200.%d.1/24", oct);
    (void)snprintf(net->cont_ip_cidr, sizeof(net->cont_ip_cidr), "10.200.%d.2/24", oct);
    (void)snprintf(net->gw_ip, sizeof(net->gw_ip), "10.200.%d.1", oct);

    char cmd[512];

    (void)snprintf(cmd, sizeof(cmd), "ip link del %s >/dev/null 2>&1", net->host_if);
    (void)system(cmd);

    (void)snprintf(cmd, sizeof(cmd), "ip link add %s type veth peer name %s", net->host_if, net->cont_if);
    if (system(cmd) != 0) goto fail;

    (void)snprintf(cmd, sizeof(cmd), "ip addr add %s dev %s", net->host_ip_cidr, net->host_if);
    (void)system(cmd);

    (void)snprintf(cmd, sizeof(cmd), "ip link set %s up", net->host_if);
    (void)system(cmd);

    /* Move container side into the container netns by PID. */
    (void)snprintf(cmd, sizeof(cmd), "ip link set %s netns %d", net->cont_if, (int)container_pid);
    if (system(cmd) != 0) goto fail;

    /* Tell the container init what interface name and addressing to use. */
    char msg[256];
    int n = snprintf(msg, sizeof(msg), "%s %s %s\n", net->cont_if, net->cont_ip_cidr, net->gw_ip);
    if (n <= 0 || (size_t)n >= sizeof(msg)) goto fail;

    ssize_t w = write(net->pipe_write_fd, msg, (size_t)n);
    if (w < 0) goto fail;

    (void)close(net->pipe_write_fd);
    net->pipe_write_fd = -1;

    return 0;

fail:
    if (net->pipe_write_fd >= 0) {
        (void)close(net->pipe_write_fd);
        net->pipe_write_fd = -1;
    }
    return -1;
}

static void net_cleanup(struct net_config *net)
{
    if (!net || !net->enabled) return;
    if (net->host_if[0] == '\0') return;

    char cmd[256];
    (void)snprintf(cmd, sizeof(cmd), "ip link del %s >/dev/null 2>&1", net->host_if);
    (void)system(cmd);
}

static int net_child_configure(int pipe_read_fd)
{
    /* Read one line: "ifname ipcidr gw" */
    char buf[256];
    size_t pos = 0;
    while (pos + 1 < sizeof(buf)) {
        ssize_t r = read(pipe_read_fd, &buf[pos], 1);
        if (r < 0) return -1;
        if (r == 0) break;
        if (buf[pos] == '\n') { pos++; break; }
        pos++;
    }
    buf[pos] = '\0';

    (void)close(pipe_read_fd);

    char *ifname = buf;
    char *sp1 = strchr(buf, ' ');
    if (!sp1) return 0;
    *sp1++ = '\0';
    char *ipcidr = sp1;
    char *sp2 = strchr(sp1, ' ');
    if (!sp2) return 0;
    *sp2++ = '\0';
    char *gw = sp2;

    char cmd[512];

    /* Bring up loopback and the veth interface, then assign IP and default route. */
    (void)snprintf(cmd, sizeof(cmd), "ip link set lo up");
    (void)system(cmd);

    (void)snprintf(cmd, sizeof(cmd), "ip link set %s up", ifname);
    (void)system(cmd);

    (void)snprintf(cmd, sizeof(cmd), "ip addr add %s dev %s", ipcidr, ifname);
    (void)system(cmd);

    (void)snprintf(cmd, sizeof(cmd), "ip route add default via %s", gw);
    (void)system(cmd);

    return 0;
}
