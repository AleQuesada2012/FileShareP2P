# FileShareP2P

P2P file-sharing simulator over TCP sockets for IC-6600 Principios de Sistemas Operativos, ITCR I Semestre 2026.

This repository is now moving through **Phase 2 - Core features** from `AGENTS.md`: shared interfaces are frozen, the central server can register peers and answer searches, and the client can already use `find -s` against the server.

## Phase 1 Scope

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

The binaries compile, but core Phase 2 behavior is still under development.

```sh
build/server/p2p-server <listen_port>
build/client/p2p-client <server_ip> <server_port> <data_port> <share_folder> [--ttl <n>] [--search-timeout <ms>]
```

Current runtime limitations:

| Command or feature | Current behavior |
|---|---|
| Server registration handling | Implemented in `server/query_handler.c` |
| Server `FIND` handling | Implemented for filename search and `(S,H)` identity lookup |
| Client startup scan | Implemented locally, then sends a `REGISTER` request to the server |
| REPL `find -s <name>` | Sends a central-server `FIND` request and prints returned `(S, H, IP, port, name)` results |
| REPL `find -d <name>` / `find <name>` | Parsed, but distributed search and fallback remain TODO |
| REPL `request` command | Parsed as TODO message |
| Segmented transfer | Stubbed with `ENOSYS` |
| Distributed search flood | Stubbed with `ENOSYS` |

For `request <S> <H>`, the frozen protocol uses the existing
`P2P_MSG_FIND_REQ` / `P2P_MSG_FIND_RESP` exchange. Send `find_req_t.term` as
`"<S> <H>"`, `"S=<S> H=<H>"`, or `"<S>:<H>"`; the server returns all peers
whose file identity matches `size_bytes == S` and `hash == H`.

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

Recommended next implementation work:

| Owner | Next task |
|---|---|
| Student 1 | Stress-test central server with 3+ clients and malformed inputs |
| Student 2 | Add the transfer listener thread and implement `request <S> <H>` segmented download flow using server identity lookup |
| Student 3 | Seed `search/neighbors.c` from `register_resp_t`, then implement flood receive/forward logic |

Keep commits focused by ownership area. Changes to `common/` need extra care because all modules depend on it.

## Portability

Target environment is standard Linux with C99 and POSIX sockets/pthreads. The Makefile also supports local macOS development by adding `_DARWIN_C_SOURCE` when needed. External dependencies should stay within standard POSIX facilities unless the team updates the project plan.
