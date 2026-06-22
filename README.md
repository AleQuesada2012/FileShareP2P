# FileShareP2P

P2P file-sharing simulator over TCP sockets for IC-6600 Principios de Sistemas Operativos, ITCR I Semestre 2026.

This repository is now in Phase 2 integration. Phase 1 foundations are in
place, and the main Phase 2 discovery and transfer pieces are available:
clients can register, run centralized search, run distributed `find -d`, use
plain `find` with server-first fallback, refresh request peers by file identity,
and request a found file from one or more peers.

## Current Development Phase

The current development phase is **Phase 2**. The project has moved beyond the
Phase 1 shared-contract milestone because server registration/search, client
registration, distributed search, segmented transfer, plain `find` fallback,
hot-unplug smoke validation, and a gated large-file multi-peer transfer test
are now implemented.

## Phase 1 Foundation Status

Phase 1 is about stable shared contracts, not a complete simulator.

Current Phase 1 deliverables:

| Area | Owner | Status |
|---|---|---|
| Wire protocol structs and opcodes | Student 1 | Defined in `common/protocol.h`; changes require team review |
| Deterministic file hash | Student 1 | Implemented with FNV-1a `uint64_t` hashing |
| Hash unit tests | Student 1 | Covers known answer, empty file, binary data, large file, missing path |
| Socket wrapper API | Student 2 | Implemented in `common/net.c` / `common/net.h` |
| Net/protocol smoke tests | Student 2 + all | Length-frame and protocol round-trip tests pass |
| Repository skeleton | Student 3 | Module directories, Makefile, and LaTeX shell exist |

Phase 1 exit criteria:

| Criterion | Current result |
|---|---|
| `make` compiles without errors | Passing |
| Hash function passes tests | Passing |
| Two endpoints can exchange a framed message | Passing via `test_net` and `test_protocol_roundtrip` |

## Repository Layout

```text
├── common/           # Shared protocol structs, hash, socket wrappers
├── server/           # Student 1: registry and server query handling
├── client/           # Student 2: startup, scanner, REPL
├── transfer/         # Student 2: segmented file sender and receiver
├── search/           # Student 3: neighbors, flood search, aggregator
├── docs/             # Student 3: LaTeX report
├── tests/
│   ├── unit/         # Unit and smoke tests
│   └── integration/  # Integration test notes/scaffold
├── Makefile
├── AGENTS.md         # Full project plan, ownership, rubric, timeline
└── README.md
```

## Build And Test

```sh
make
make test
```

The default integration suite includes a large-file smoke script that is skipped
unless explicitly enabled. To run the 3-client `.mkv` split-transfer scenario
against the local `./share/` video:

```sh
RUN_LARGE_INTEGRATION=1 sh tests/integration/test_large_multi_peer_transfer.sh
```

Build outputs:

```text
build/server/p2p-server
build/client/p2p-client
```

Documentation build:

```sh
make docs
```

`make docs` requires `pdflatex`. Generated LaTeX artifacts and `build/` are removed by:

```sh
make clean
```

## Running The Current Binaries

The binaries compile, and central-server search, distributed `find -d`, plain
`find <name>` fallback, request identity refresh, and transfer are available for
local integration testing.

```sh
build/server/p2p-server <listen_port>
build/client/p2p-client <server_ip> <server_port> <data_port> <share_folder> [--ttl <n>] [--search-timeout <ms>]
```

Current runtime status:

| Command or feature | Current behavior |
|---|---|
| Server registration handling | Accepts `REGISTER` requests, stores peer metadata, and returns recent peers |
| Server `FIND` handling | Accepts filename searches and `(S,H)` identity lookups, returning matching `(S, H, IP, port, name)` results |
| Client startup scan | Implemented locally, then sends a `REGISTER` request to the server |
| REPL `find -s <name>` | Sends a central-server `FIND` request and prints returned `(S, H, IP, port, name)` results |
| REPL `find -d <name>` | Sends a distributed flood query to known neighbors and prints collected results |
| REPL `find <name>` | Tries the server first; falls back to distributed search when the server fails or returns no results |
| REPL `request <S> <H>` | Refreshes peers with central identity `FIND`, falls back to cached matching results if needed, downloads equal byte ranges from available peers, and writes the file with its original result filename |
| Incoming transfer listener | Starts on the client data port and accepts `TRANSFER_REQ` messages |
| Transfer sender | Sends requested byte ranges as `TRANSFER_DATA` frames |
| Transfer receiver / file assembly | Splits ranges across peers and assembles a completed file |
| Distributed search flood | Runs on the unified client data port, forwards query messages with TTL, deduplicates query IDs, and aggregates responses |
| Hot-unplug handling | Missing source or destination share folders produce warnings and the REPL/server continue running; local smoke coverage exists |
| Runtime logging | The server logs accepted connections, `REGISTER`, `FIND`, malformed requests, and unsupported opcodes; transfer paths log range requests and safety fallbacks |

For `request <S> <H>`, the frozen protocol uses the existing
`P2P_MSG_FIND_REQ` / `P2P_MSG_FIND_RESP` exchange. Send `find_req_t.term` as
`"<S> <H>"`, `"S=<S> H=<H>"`, or `"<S>:<H>"`; the server returns all peers
whose file identity matches `size_bytes == S` and `hash == H`.

Distributed `find -d` uses the client's startup scan metadata when answering
flood queries. Earlier video searches could miss the default 2-second response
window because the receiver re-scanned and re-hashed large files during each
query; the file size was slowing local metadata generation, not exceeding a wire
protocol size limit. The flood path now reuses the already computed metadata
and uses dynamically sized query-result buffers.

## Protocol Contract

The shared protocol lives in `common/protocol.h` and is the main Phase 1 freeze point.

Important rules:

- TCP messages use `net_send_msg` / `net_recv_msg` as the outer length-prefixed frame.
- The frame payload starts with `p2p_msg_header_t`, followed by the opcode-specific payload.
- `p2p_msg_header_t.payload_len` is the number of bytes after the header.
- Multi-byte integers must be encoded in network byte order before transmission.
- Fixed-size string fields must be null-terminated when populated.
- Structs must be zeroed or serialized into explicit byte buffers before sending.
- `register_resp_t.neighbors` excludes the peer that just registered.
- Distributed search uses TCP for this iteration.

Do not change `common/protocol.h` without coordinating with all owners.

## Phase 2 Starting Points

Recommended next implementation and validation work:

| Owner | Next task |
|---|---|
| Student 1 | Stress-test server registration and `FIND` with 3+ clients |
| Student 2 | Repeat large-file and hot-unplug checks on Linux, then capture measurements for the report |
| Student 3 | Harden distributed search for multi-hop LAN runs and document TTL/window behavior |

Keep commits focused by ownership area. Changes to `common/` need extra care because all modules depend on it.

## Portability

Target environment is standard Linux with C99 and POSIX sockets/pthreads. The Makefile also supports local macOS development by adding `_DARWIN_C_SOURCE` when needed. External dependencies should stay within standard POSIX facilities unless the team updates the project plan.
