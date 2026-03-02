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

Example:

```sh
sudo ./runtime.sh run --root /srv/rootfs --hostname demo /bin/bash
```
