# FileShareP2P

P2P file-sharing simulator over TCP sockets for IC-6600 Principios de Sistemas
Operativos, ITCR I Semestre 2026.

## Current State

The simulator implements the required central server, client control, segmented
file request, file assembly, distributed search, TTL/cycle control, and
hot-unplug tolerance paths.

Implemented highlights:

| Area | Status |
|---|---|
| Protocol | Fixed framed TCP protocol in `common/protocol.h` |
| Hashing | Deterministic FNV-1a `uint64_t` file hashing |
| Server | `REGISTER`, `FIND`, `ERROR`, recent-neighbor responses |
| Client | Startup scan/register, REPL, central/distributed/fallback find |
| Transfer | Multi-peer byte-range split, segment threads, assembly |
| Search | TCP flood search, TTL, seen-query cache, response aggregation |
| Listener | One client data-port listener dispatching transfer and search opcodes |
| Docs/tests | Unit tests, integration smoke script, LaTeX report |

## Build And Test

```sh
make
make test
```

Build outputs:

```text
build/server/p2p-server
build/client/p2p-client
```

Run the three-client smoke:

```sh
sh tests/integration/three_client_smoke.sh
```

The smoke starts one server and three clients, covers `find -s`, `find -d`,
plain `find` fallback, multi-peer `request S H`, and share-folder removal while
processes continue.

Build the report:

```sh
make docs
```

`make docs` runs `pdflatex` when it is installed and otherwise prints a warning.

## Running Manually

Start the server:

```sh
build/server/p2p-server <listen_port>
```

Start each client:

```sh
build/client/p2p-client <server_ip> <server_port> <data_port> <share_folder> \
  [--ttl <n>] [--search-timeout <ms>]
```

REPL commands:

```text
find -s <name>       central server search
find -d <name>       distributed neighbor search
find <name>          server first, distributed fallback on failure/empty result
request <S> <H>      download the file from peers in the latest search results
help                 show commands
quit                 exit
```

Downloaded files are written to the share folder as:

```text
download_<S>_<H>.bin
```

## Protocol Notes

- `net_send_msg` / `net_recv_msg` provide the outer length-prefixed TCP frame.
- Each payload begins with `p2p_msg_header_t`.
- Multi-byte fields are encoded in network byte order.
- `REGISTER_RESP` neighbors exclude the registering peer.
- Distributed search uses TCP in this implementation.
- `query_msg_t` carries the originator IP/port and TTL. Query cycles are
  controlled with a 60-second seen-query cache.

## Repository Layout

```text
common/      shared protocol, hash, socket helpers
server/      central registry and query server
client/      startup, scanning, server API, REPL
transfer/    peer listener, sender, segmented receiver
search/      neighbors, flood search, aggregation
docs/        LaTeX report
tests/       unit tests and integration smoke
```

## Cleanup

```sh
make clean
```
