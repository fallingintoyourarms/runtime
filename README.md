# runtime.sh

Minimal Linux container runtime I wrote mostly to mess around with C and learn Linux primitives.

This produces a **binary named `runtime.sh`**.

## Build

```sh
gcc runtime.c -o runtime.sh
```

## Run (Phase 1)

```sh
sudo ./runtime.sh run --root <rootfs-path> [--hostname <name>] <cmd> [args...]
```

Optional flags:

```sh
sudo ./runtime.sh run --root <rootfs-path> --mem 256m --cpu 0.5 --pids 128 --net <cmd> [args...]
```

Example:

```sh
sudo ./runtime.sh run --root /srv/rootfs --hostname demo /bin/bash
```

## Other commands

```sh
./runtime.sh ps
sudo ./runtime.sh stop <pid>
```
