# Repository Breakdown

This document explains the current repository state against the project spec in
`AGENTS.md`, organized by student ownership. It is meant to help the team decide
what to work on next, where the work belongs, and how to keep the Git branches
aligned.

## Current Project State

The repository is in Phase 2 integration. Phase 1 foundations are in place, and
the Student 1 server work, Student 2 client/transfer work, and Student 3
distributed-search work have been merged on `dev` into a shared integration
base.

Phase 1 foundations are mostly in place:

- `common/protocol.h` defines the shared message opcodes and structs.
- `common/hash.c` implements deterministic FNV-1a file hashing.
- `common/net.c` implements shared TCP connection and framed-message helpers.
- The build system compiles server and client binaries.
- Unit tests exist for hashing, network framing, and a protocol round-trip.
- Skeletons exist for server, client, transfer, search, and LaTeX docs.

Early Phase 2 work now works end to end for centralized discovery, distributed
`find -d`, plain `find` fallback, and transfer:

- The client scans the share folder at startup.
- The client sends a `REGISTER` request after scanning.
- The REPL supports `find -s <name>` by sending a central-server `FIND` request.
- The REPL supports `find -d <name>` by sending a distributed flood query to
  known neighbors and printing collected responses.
- The server accepts `REGISTER` and `FIND`, updates the registry, returns recent
  peers on registration, and returns matching file metadata on centralized
  search.
- The server also accepts `(S,H)` identity lookup through the existing `FIND`
  request, so clients can ask for all peers holding a specific file identity.
- Plain `find <name>` tries the server first and falls back to distributed
  search if the server fails or returns no results.
- The REPL supports `request <S> <H>` by refreshing peers through server
  identity `FIND`, falling back to matching cached results if the refresh cannot
  provide peers, and then downloading/assembling the requested file while
  preserving the original extension from the search metadata.
- The server now logs each accepted connection, `REGISTER`, `FIND`, malformed
  request, and unsupported opcode.
- Transfer code logs requested ranges, segment splitting, and hot-unplug-style
  safety fallbacks when source or destination share folders disappear.
- Integration tests cover central registration/search and central
  search-to-transfer download, including identity search followed by
  `request <S> <H>`, direct request identity refresh, distributed search,
  distributed search followed by `request <S> <H>`, plain `find` fallback, and
  hot-unplug request behavior. A gated large-file test covers a 3-client `.mkv`
  download split across two source peers.

The most important integration boundary is still `common/protocol.h`. Do not
change it casually. Any protocol change affects all three students.

## Module Map

| Path | Owner | Current role | Current status |
|---|---|---|---|
| `common/protocol.h` | Student 1 | Shared wire contract: opcodes, headers, payload structs, constants | Defined and treated as frozen for Phase 1 |
| `common/hash.c/h` | Student 1 | Deterministic `uint64_t` file hashing | Implemented and tested |
| `common/net.c/h` | Student 2 | TCP connect/listen and length-prefixed send/receive helpers | Implemented and tested |
| `server/main.c` | Student 1 | Server startup, SIGPIPE handling, registry lifecycle | Implemented |
| `server/registry.c/h` | Student 1 | Thread-safe in-memory peer registry and file lookup | Implements registration, name lookup, identity lookup, recent peers, and recent peers excluding the registering peer |
| `server/query_handler.c/h` | Student 1 | Accept client messages and dispatch `REGISTER` / `FIND` | Implements threaded `REGISTER`, filename `FIND`, identity `FIND`, `ERROR` handling, and request logging |
| `client/main.c` | Student 2 | Parse CLI, scan share folder, register with server, start REPL | Registration now wired |
| `client/server_api.c/h` | Student 2 | Client-side protocol calls to central server | Implements `REGISTER` and `FIND` request paths |
| `client/scanner.c/h` | Student 2 | Recursive folder scan and hash metadata creation | Implemented |
| `client/repl.c/h` | Student 2 | Interactive commands | `find -s`, `find -d`, plain `find` fallback, and `request <S> <H>` identity refresh are wired |
| `transfer/listener.c/h` | Student 2 | Accept incoming transfer requests on the client data port | Implemented listener thread and per-request dispatch |
| `transfer/sender.c/h` | Student 2 | Send requested byte ranges to peers | Sends `TRANSFER_DATA` frames for validated byte ranges |
| `transfer/receiver.c/h` | Student 2 | Request segments, collect them, assemble file | Splits ranges across peers and assembles into the share folder with the original extension preserved |
| `search/neighbors.c/h` | Student 3 | Neighbor list management and distributed-search API boundary | Neighbor list works; `search_distributed` builds query IDs, floods to neighbors, waits for responses, and returns collected results |
| `search/flood.c/h` | Student 3 | Flood listener, query forwarding, TTL behavior | Integrated with unified `data_port` listener; handles `QUERY_FLOOD` / `QUERY_RESULT`, deduplicates query IDs, searches cached startup metadata, and forwards with TTL |
| `search/aggregator.c/h` | Student 3 | Collect distributed search responses | Basic thread-safe aggregator implemented and used by distributed search |
| `docs/` | Student 3 | LaTeX report | Skeleton exists |
| `tests/unit/` | All | Unit and smoke tests for shared behavior | Hash, net, protocol, registry, query handler, and transfer tests exist |
| `tests/integration/` | All | Local smoke tests for process-level behavior | Central search, request transfer, distributed search, plain fallback, request identity refresh, hot-unplug, and gated large `.mkv` split-transfer smoke tests exist |

## Student 1: Server And Protocol Architect

Student 1 owns `server/`, `common/hash.c`, and `common/protocol.h`.

### What Already Exists

- `common/protocol.h` defines:
  - `p2p_msg_header_t`
  - `file_meta_t`
  - `peer_entry_t`
  - `register_req_t` / `register_resp_t`
  - `find_req_t` / `find_resp_t`
  - transfer and distributed-search payloads
  - `error_msg_t`
- Protocol framing is documented:
  - `net_send_msg` / `net_recv_msg` provide the outer length-prefixed frame.
  - The frame payload starts with `p2p_msg_header_t`.
  - Multi-byte integer fields must use network byte order on the wire.
- `common/hash.c` implements FNV-1a hashing.
- `server/registry.c` can register peers, search by filename substring, search
  by `(hash, size)`, return recent peers, and return recent peers excluding the
  registering peer.
- `server/query_handler.c` accepts client connections, spawns per-client
  threads, decodes `REGISTER` / `FIND`, sends `REGISTER_RESP` / `FIND_RESP`,
  supports identity lookup through the existing `FIND` request, and returns
  `ERROR` for bad messages.
- Server logs make registration, search, malformed request, and unsupported
  opcode processing visible during demos.
- `server/main.c` initializes the registry and starts the query server.
- Tests cover registry behavior, query handler behavior, malformed messages,
  disconnect safety, and a central client/server smoke run.

### What Student 1 Should Work On Next

1. Stress-test server behavior with three or more clients.
   - Confirm repeated registrations update an existing peer instead of
     duplicating it.
   - Confirm recent-peer ordering remains useful for Student 3 neighbor seeding.
   - Confirm malformed or disconnected clients do not stop the server loop.

2. Coordinate the identity lookup convention with Students 2 and 3.
   - `request <S> <H>` can use `P2P_MSG_FIND_REQ` if the client needs to refresh
     peers by identity.
   - Populate `find_req_t.term` as `"<S> <H>"`, `"<S>:<H>"`, `"S=<S> H=<H>"`,
     or `"H=<H> S=<S>"`.
   - The server returns matching peers in the normal `P2P_MSG_FIND_RESP`.

3. Coordinate any future protocol change with Students 2 and 3.
   - The current protocol is sufficient for centralized registration/search.
   - Changes to `common/protocol.h` should remain rare and heavily tested.

### Student 1 Cautions

- Do not change `common/protocol.h` unless Students 2 and 3 agree.
- Any shared registry state must stay protected by `pthread_mutex_t`.
- Do not use `volatile` for thread safety.
- Every socket and file operation must check return values.

## Student 2: Client Core And File Transfer

Student 2 owns `client/`, `transfer/`, and `common/net.c`.

### What Already Exists

- `common/net.c` implements:
  - `net_connect`
  - `net_listen`
  - `net_send_all`
  - `net_recv_all`
  - `net_send_msg`
  - `net_recv_msg`
  - `net_close`
- `client/main.c` parses:
  - `<server_ip>`
  - `<server_port>`
  - `<data_port>`
  - `<share_folder>`
  - optional `--ttl <n>`
  - optional `--search-timeout <ms>`
- `client/scanner.c` recursively scans files and computes hash plus size.
- `client/server_api.c` sends:
  - `REGISTER` after scanning
  - central-server `FIND` for `find -s`
  - identity `FIND` terms used by `request <S> <H>` refresh
- `client/repl.c` supports:
  - `find -s <name>` through the central server
  - `find -d <name>` through Student 3's distributed search path
  - `find <name>` with server-first, distributed-search fallback behavior
  - `request <S> <H>` with server identity refresh and cached-result fallback
- `transfer/listener.c` starts a detached listener on the client data port.
- `transfer/sender.c` locates local files by `(hash, size)` and sends requested
  byte ranges as `P2P_MSG_TRANSFER_DATA` frames.
- `transfer/receiver.c` splits byte ranges across available peers, receives
  `P2P_MSG_TRANSFER_DATA` frames concurrently, and writes a completed
  file using the original result filename into the share folder.
- Integration coverage now verifies that one client can register a file, another
  client can find it through the central server, distributed search can find it
  through `find -d`, and `request <S> <H>` downloads it through the transfer
  path. Newer integration tests verify identity-search-to-request,
  request-without-prior-find identity refresh, distributed-search-to-request,
  plain `find` fallback, share-folder removal during a request, and a gated
  3-client `.mkv` transfer split across two source peers.

### What Student 2 Should Work On Next

1. Repeat large-file split-transfer and hot-unplug checks on Linux.
   - `RUN_LARGE_INTEGRATION=1 sh tests/integration/test_large_multi_peer_transfer.sh`
     validates a 500 MiB+ `.mkv` download from two peers.
   - Local integration now removes the requester's share folder during runtime
     and confirms the client logs a warning, keeps the REPL alive, and does not
     crash the server.
   - The final demo should repeat this on the Linux target environment.

2. Capture transfer measurements for the report.
   - Record file size, peer count, elapsed time, and observed behavior for the
     large `.mkv` transfer.

### Student 2 Cautions

- `client/server_api.c` expects server responses to use the frozen protocol
  contract. If Student 1 changes response shapes, update both sides together.
- Hot-unplug behavior is required. Do not assume the share folder remains
  available after startup.

## Student 3: Distributed Search And Documentation

Student 3 owns `search/` and `docs/`.

### What Already Exists

- `search/neighbors.c` implements:
  - neighbor list initialization and destruction
  - adding/updating a peer
  - copying a snapshot of known peers
- `search_distributed` builds a query ID, sends the query through the flood
  module, waits for the configured timeout, and returns aggregated results.
- `search/aggregator.c` implements:
  - initialization and destruction
  - adding a result
  - collecting a snapshot of results
- `search/flood.c` implements:
  - integrated flood handling on the unified `data_port` (no longer on `data_port + 100`)
  - `P2P_MSG_QUERY_FLOOD` handling
  - matching against cached startup scan metadata
  - `P2P_MSG_QUERY_RESULT` response handling
  - query ID deduplication with expiry
  - TTL-limited forwarding to known neighbors
- Large video `find -d` failures were caused by timing, not by the video being
  too large for metadata messages. The previous path re-scanned and re-hashed
  the live share folder during each flood query, which could exceed the
  response window for a 1.5GB file. The current path reuses startup metadata and
  dynamically sizes query-result buffers.
- `docs/main.tex` and report sections exist as a LaTeX shell.

### What Student 3 Should Work On Next

1. Harden distributed search beyond the current local two-client smoke tests.
   - Run three or more clients and confirm multi-hop forwarding works.
   - Verify TTL expiration prevents uncontrolled propagation.
   - Verify duplicate query IDs are discarded under repeated or cyclic delivery.

2. Keep the LaTeX report alive.
   - Update architecture decisions as implementation changes.
   - Document TTL reasoning and empirical behavior.
   - Record challenges and conclusions during development, not only at the end.

### Student 3 Cautions

- Flood logic must not spam the LAN. Respect TTL and deduplication.
- Any shared query cache or result aggregator must be mutex-protected.
- Distributed results should look like server results to the user.

## Git Workflow

The project plan defines these branches:

| Branch | Purpose |
|---|---|
| `main` | Protected final branch. Merge by PR only. |
| `dev` | Integration branch. Feature branches merge here first. |
| `feat/common` | Shared structs, protocol, hash, net tests. |
| `feat/s1-server` | Student 1 server and registry work. |
| `feat/s2-client` | Student 2 client and transfer work. |
| `feat/s3-search` | Student 3 search and documentation work. |

### Normal Feature Workflow

Use this when the feature branch is already aligned with `dev`.

```sh
git switch dev
git pull
git switch feat/s2-client   # or feat/s1-server / feat/s3-search
git merge dev

# make changes
make
make test

git status
git add <files>
git commit -m "feat(client): short description"
git push
```

Use the correct scope in commit messages:

- `feat(server): ...`
- `fix(server): ...`
- `feat(client): ...`
- `feat(transfer): ...`
- `feat(search): ...`
- `docs(latex): ...`
- `test(hash): ...`
- `test(common): ...`

### Realigning An Old Feature Branch

Some feature branches were created before their work was merged into `dev`.
Git can then report that the branch cannot fast-forward because the old branch
history diverges from `dev`, even though the actual work already exists in
`dev`.

Use this process only when you have confirmed the old feature branch's unique
commits are already represented in `dev`.

```sh
git switch dev
git pull
git branch -f feat/s3-search dev   # replace with the branch you are fixing
git switch feat/s3-search

# make changes
make
make test

git add <files>
git commit -m "feat(search): short description"
git push --force-with-lease
```

`--force-with-lease` is safer than `--force` because it refuses to overwrite
remote work that appeared after your last fetch.

### Branch Guidance By Student

Student 1:

- Work on `feat/s1-server`.
- If the branch still points to old history, realign it to `dev` before new
  work.
- Avoid changing `common/protocol.h` unless the whole team agrees.

Student 2:

- Work on `feat/s2-client`.
- This branch has already been realigned to current `dev` and contains the
  central-server client API work.
- Future pushes should normally be regular `git push` unless the branch is
  rewritten again.

Student 3:

- Work on `feat/s3-search`.
- This branch may need the same realignment process used for `feat/s2-client`
  if it still points at old pre-merge history.
- Search code and documentation can share this branch when the work is part of
  the same Student 3 task.

Shared changes:

- Use `feat/common`.
- Require extra review from all students.
- Keep shared commits small and explicit.

## Suggested Next Checkpoints

Checkpoint 1: central server demo — complete locally

- Student 1 implemented server `REGISTER` and `FIND`.
- Student 2 client registration and `find -s` are covered by integration tests.
- Expected result: one client registers files, and another process can search
  through the server.

Checkpoint 2: distributed search and transfer demo — local two-client version complete

- Student 3 implemented local distributed `find -d`.
- Student 2 implemented segmented transfer.
- Expected result: `find -d` returns results, and `request <S> <H>` downloads
  and reassembles a file from multiple peers.
- A gated 3-client large-file test now verifies that a `.mkv` can be
  reassembled from two source peers in two byte ranges.

Final integration:

- Run three clients on a LAN or local machine with different ports.
- Share files from different folders.
- Test `find -s`, `find -d`, and `request`.
- Run the gated large `.mkv` integration test when the local `./share/` video is
  available.
- Remove or rename a share folder while the client runs to verify hot-unplug
  safety. A local automated smoke test exists, but repeat this on Linux.
- Update the LaTeX report with measured behavior and design decisions.

## Current Risks

- Distributed search is implemented for local two-client smoke tests, but still
  needs multi-hop and LAN hardening.
- Large-file multi-peer request splitting is covered locally by a gated test,
  but still needs a final Linux/LAN demo run.
- Hot-unplug behavior has local smoke coverage; it still needs final Linux demo
  validation.
- Protocol structs are frozen enough to proceed, but any future change will
  require coordinated updates across client, server, search, and tests.
- The report exists, but content should be filled continuously to avoid a last
  minute documentation crunch.
