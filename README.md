# FileShareP2P

Barebones C99 scaffold for the IC-6600 P2P file-sharing simulator.

## Build

```sh
make
make test
```

Binaries are written under `build/`:

```sh
build/server/p2p-server <listen_port>
build/client/p2p-client <server_ip> <server_port> <data_port> <share_folder> [--ttl <n>] [--search-timeout <ms>]
```

## Portability notes

The grading target is standard Linux. The Makefile also supports local macOS ARM development by using POSIX APIs only, avoiding Linux-only socket flags, and adding `_DARWIN_C_SOURCE` when `uname -s` reports `Darwin`.

Current implementation status is intentionally first-commit level: shared contracts, hash tests, socket wrapper tests, recursive scanner, registry shell, REPL command shell, transfer/search stubs, and LaTeX documentation skeleton.
