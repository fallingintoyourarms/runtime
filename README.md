# runtime

Minimal Linux container runtime I wrote mostly to mess around with C and learn Linux primitives.

This produces a **binary named `runtime`**.

## Build

```sh
gcc runtime.c -o runtime
```

## Run (Phase 1)

```sh
sudo ./runtime run --root <rootfs-path> [--hostname <name>] <cmd> [args...]
```

Optional flags:

```sh
sudo ./runtime run --root <rootfs-path> --mem 256m --cpu 0.5 --pids 128 --net <cmd> [args...]
```

Example:

```sh
sudo ./runtime run --root /srv/rootfs --hostname demo /bin/bash
```

## Other commands

```sh
./runtime ps
sudo ./runtime stop <pid>
```
