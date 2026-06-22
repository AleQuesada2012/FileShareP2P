# FileShareP2P

P2P file-sharing simulator over TCP sockets for IC-6600 Principios de Sistemas Operativos, ITCR I Semestre 2026.

## Final Project Status

The project is complete on `dev` and ready to be merged into `main`.

The implementation now satisfies the planned simulator flow from `Proyecto2.tex`
and `AGENTS.md`:

- Clients scan their share folder, compute file metadata, and register with the
  central server.
- The server stores peer/file metadata, handles `REGISTER`, filename `FIND`,
  identity `(S,H)` lookup, recent-peer responses, error responses, and demo
  logging.
- The client REPL supports `find -s <name>`, `find -d <name>`, plain
  `find <name>` with server-first fallback, and `request <S> <H>`.
- Transfers are performed over TCP by byte range, can split a download across
  multiple peers, reassemble the file locally, and preserve the original
  filename reported by search results.
- Distributed search uses neighbor seeding, TCP flood messages, query-ID
  deduplication, TTL control, response aggregation, and the unified client data
  port.
- Hot-unplug behavior is handled defensively so missing source or destination
  share folders produce warnings instead of crashing the client or server.
- Unit tests and integration tests cover the main shared contracts, server,
  client/search/transfer behavior, and final smoke scenarios.
- The LaTeX report is in `docs/main.tex`, uses local images from `docs/img/`,
  references from `docs/ref.bib`, and builds to `docs/main.pdf`.

## Repository Layout

```text
├── common/            # Shared protocol structs, hash, socket wrappers
├── server/            # Student 1: registry and server query handling
├── client/            # Student 2: startup, scanner, server API, REPL
├── transfer/          # Student 2: segmented file sender and receiver
├── search/            # Student 3: neighbors, flood search, aggregator
├── docs/
│   ├── img/           # Report images, kept beside main.tex
│   ├── main.tex       # Spanish LaTeX report
│   ├── main.pdf       # Built Spanish report
│   └── ref.bib        # Report bibliography
├── tests/
│   ├── unit/          # Unit and smoke tests
│   └── integration/   # Multi-process integration tests
├── Makefile
├── AGENTS.md          # Project plan, ownership, rubric, timeline
├── breakdown.md       # Final repo state by student role
└── README.md
```

## Build, Test, And Docs

Build the binaries:

```sh
make
```

Run the full default test suite:

```sh
make test
```

`make test` runs unit tests and integration tests. The large 3-client `.mkv`
split-transfer script is part of the integration list, but it skips itself unless
explicitly enabled. To run that final large-file scenario with the local
`./share/` video:

```sh
RUN_LARGE_INTEGRATION=1 sh tests/integration/test_large_multi_peer_transfer.sh
```

Build the Spanish LaTeX report:

```sh
make docs
```

`make docs` requires both `pdflatex` and `bibtex` from a TeX Live/MacTeX style
installation. The build runs from `docs/`, using `docs/img/` and `docs/ref.bib`
the same way an Overleaf project would.

Clean build artifacts:

```sh
make clean
```

## Running The Simulator

After `make`, the binaries are:

```text
build/server/p2p-server
build/client/p2p-client
```

Start the server:

```sh
build/server/p2p-server <listen_port>
```

Start each client:

```sh
build/client/p2p-client <server_ip> <server_port> <data_port> <share_folder> [--ttl <n>] [--search-timeout <ms>]
```

Available REPL commands:

| Command | Behavior |
|---|---|
| `find -s <name>` | Centralized search through the server |
| `find -d <name>` | Distributed search through known neighbors |
| `find <name>` | Server-first search with distributed fallback |
| `request <S> <H>` | Refreshes peers by file identity and downloads the file |
| `quit` / `exit` | Ends the client |

## Final Feature Matrix

| Area | Status |
|---|---|
| Protocol contract | Implemented in `common/protocol.h`; treated as frozen |
| Hash function | Implemented with deterministic FNV-1a `uint64_t` hashing |
| Socket wrappers | Implemented in `common/net.c` / `common/net.h` |
| Server registration | Implemented, logged, and tested |
| Server `FIND` | Filename and `(S,H)` identity lookup implemented |
| Client startup | CLI parsing, scan, listener startup, and registration implemented |
| Central search | `find -s <name>` implemented |
| Distributed search | `find -d <name>` implemented with TTL and deduplication |
| Plain search fallback | `find <name>` implemented |
| File request | `request <S> <H>` implemented with identity refresh and cached fallback |
| Segmented transfer | Implemented across one or more peers |
| Filename preservation | Downloaded files keep the original result filename |
| Hot-unplug safety | Covered by defensive checks and integration smoke behavior |
| Integration tests | Central, distributed, request, fallback, hot-unplug, and gated large-file paths exist |
| Documentation | Spanish LaTeX source and PDF are present under `docs/` |

## Protocol Contract

The shared protocol lives in `common/protocol.h`.

Important rules:

- TCP messages use `net_send_msg` / `net_recv_msg` as the outer
  length-prefixed frame.
- The frame payload starts with `p2p_msg_header_t`, followed by the
  opcode-specific payload.
- `p2p_msg_header_t.payload_len` is the number of bytes after the header.
- Multi-byte integers must be encoded in network byte order before
  transmission.
- Fixed-size string fields must be null-terminated when populated.
- Structs must be zeroed or serialized into explicit byte buffers before
  sending.
- `register_resp_t.neighbors` excludes the peer that just registered.
- Distributed search uses TCP for this iteration.
- `request <S> <H>` refreshes peers through the existing `FIND` request instead
  of adding a separate metadata-only opcode.

Do not change `common/protocol.h` after this point unless all owners agree.

## Final Merge Checklist

Before merging `dev` into `main`:

```sh
git switch dev
git pull
make
make test
make docs
git status
```

Then merge by the workflow agreed in `AGENTS.md`:

```sh
git switch main
git pull
git merge --no-ff dev
git push
```

If the protected-branch workflow is enforced on GitHub, open a PR from `dev` to
`main` instead of pushing directly.

## Portability

Target environment is standard Linux with C99 and POSIX sockets/pthreads. The
Makefile also supports local macOS development by adding `_DARWIN_C_SOURCE` when
needed. External dependencies stay within standard POSIX facilities plus a
standard LaTeX distribution for report generation.
