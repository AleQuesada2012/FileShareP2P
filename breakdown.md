# Repository Breakdown

This document explains the current repository state against the project spec in
`AGENTS.md`, organized by student ownership. It is meant to help the team decide
what to work on next, where the work belongs, and how to keep the Git branches
aligned.

## Current Project State

The repository is between the end of Phase 1 and the start of Phase 2.

Phase 1 foundations are mostly in place:

- `common/protocol.h` defines the shared message opcodes and structs.
- `common/hash.c` implements deterministic FNV-1a file hashing.
- `common/net.c` implements shared TCP connection and framed-message helpers.
- The build system compiles server and client binaries.
- Unit tests exist for hashing, network framing, and a protocol round-trip.
- Skeletons exist for server, client, transfer, search, and LaTeX docs.

Phase 2 work has started:

- The client scans the share folder at startup.
- The client sends a `REGISTER` request after scanning.
- The REPL supports `find -s <name>` by sending a central-server `FIND` request.
- The server-side handler for `REGISTER` and `FIND` is implemented.
- The server can also use the existing `FIND` request to look up peers by file
  identity for `request <S> <H>`.

The most important integration boundary is still `common/protocol.h`. Do not
change it casually. Any protocol change affects all three students.

## Module Map

| Path | Owner | Current role | Current status |
|---|---|---|---|
| `common/protocol.h` | Student 1 | Shared wire contract: opcodes, headers, payload structs, constants | Defined and treated as frozen for Phase 1 |
| `common/hash.c/h` | Student 1 | Deterministic `uint64_t` file hashing | Implemented and tested |
| `common/net.c/h` | Student 2 | TCP connect/listen and length-prefixed send/receive helpers | Implemented and tested |
| `server/main.c` | Student 1 | Server startup, SIGPIPE handling, registry lifecycle | Implemented |
| `server/registry.c/h` | Student 1 | Thread-safe in-memory peer registry and file lookup | Implemented scaffold with useful operations |
| `server/query_handler.c/h` | Student 1 | Accept client messages and dispatch `REGISTER` / `FIND` | Implemented |
| `client/main.c` | Student 2 | Parse CLI, scan share folder, register with server, start REPL | Registration now wired |
| `client/server_api.c/h` | Student 2 | Client-side protocol calls to central server | Implements `REGISTER` and `FIND` request paths |
| `client/scanner.c/h` | Student 2 | Recursive folder scan and hash metadata creation | Implemented |
| `client/repl.c/h` | Student 2 | Interactive commands | `find -s` wired; `find -d`, plain `find`, and `request` still incomplete |
| `transfer/sender.c/h` | Student 2 | Send requested byte ranges to peers | Stubbed |
| `transfer/receiver.c/h` | Student 2 | Request segments, collect them, assemble file | Stubbed |
| `search/neighbors.c/h` | Student 3 | Neighbor list management and distributed-search API boundary | Neighbor list works; `search_distributed` stubbed |
| `search/flood.c/h` | Student 3 | Flood listener, query forwarding, TTL behavior | Stubbed |
| `search/aggregator.c/h` | Student 3 | Collect distributed search responses | Basic thread-safe aggregator implemented |
| `docs/` | Student 3 | LaTeX report | Skeleton exists |
| `tests/unit/` | All | Unit and smoke tests for shared behavior | Hash, net, and protocol tests exist |

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
  by `(hash,size)`, and return recent peers.
- `server/main.c` initializes the registry and starts the query server.
- `server/query_handler.c` handles `REGISTER`, filename `FIND`, and identity
  `FIND` using the frozen protocol.

### What Student 1 Should Work On Next

1. Run 3+ client central-server stress tests.
   - Confirm repeated registration updates the peer instead of duplicating it.
   - Confirm the server keeps running after early disconnects and malformed
     frames.

2. Coordinate the existing identity lookup convention with Student 2.
   - `request <S> <H>` should use `P2P_MSG_FIND_REQ`.
   - Populate `find_req_t.term` as `"<S> <H>"`, `"<S>:<H>"`, or
     `"S=<S> H=<H>"`.
   - The server returns matching peers in the normal `P2P_MSG_FIND_RESP`.

3. Keep adding focused tests for any new server edge cases discovered during
   Student 2 and Student 3 integration.

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
- `client/repl.c` supports:
  - `find -s <name>` through the central server
  - TODO messages for distributed search and transfer commands

### What Student 2 Should Work On Next

1. Finish central-server integration after Student 1 implements the server.
   - Test that startup registration succeeds.
   - Test that `find -s <name>` prints real server results.
   - Add integration notes or tests for a client/server smoke run.

2. Implement the transfer listener.
   - Start one listener thread per client process.
   - Listen on `<data_port>`.
   - Accept incoming transfer requests.
   - Spawn a per-request sender thread.

3. Implement `transfer/sender.c`.
   - Receive or accept a `transfer_req_t`.
   - Locate the requested file by `(hash, size)`.
   - Validate byte range.
   - Send the requested byte range.
   - Handle hot-unplug with `access()` or `stat()` before opening files.

4. Implement `transfer/receiver.c`.
   - Query the server for peers that hold `(S, H)`.
   - Split the requested file into byte ranges.
   - Start one receiving thread per peer/segment.
   - Track received segments with a mutex-protected bitmap or equivalent state.
   - Assemble the final file only after all segments arrive.

5. Wire `request <S> <H>` in the REPL.
   - Parse size and hash.
   - Call `transfer_request`.
   - Print success/failure clearly.

6. Later, wire plain `find <name>`.
   - Try server first.
   - If the server search times out or returns no results, call Student 3's
     `search_distributed`.

### Student 2 Cautions

- `client/server_api.c` currently expects server responses to use the frozen
  protocol contract. If Student 1 changes response shapes, update both sides
  together.
- The current server is still a stub, so registration and `find -s` can fail
  until Student 1 implements message handling.
- Hot-unplug behavior is required. Do not assume the share folder remains
  available after startup.

## Student 3: Distributed Search And Documentation

Student 3 owns `search/` and `docs/`.

### What Already Exists

- `search/neighbors.c` implements:
  - neighbor list initialization and destruction
  - adding/updating a peer
  - copying a snapshot of known peers
- `search/aggregator.c` implements:
  - initialization and destruction
  - adding a result
  - collecting a snapshot of results
- `search/flood.c` is currently stubbed.
- `search_distributed` in `search/neighbors.c` is currently stubbed.
- `docs/main.tex` and report sections exist as a LaTeX shell.

### What Student 3 Should Work On Next

1. Seed neighbors from registration responses.
   - Student 2 receives `register_resp_t` during startup.
   - Student 3 should expose or coordinate an API that stores those peers in
     `neighbor_list_t`.
   - Avoid making the REPL or registration path block on flood setup.

2. Implement flood listener startup.
   - Use the team decision from `protocol.h`: TCP for distributed search in this
     first iteration.
   - Run listener logic in a dedicated thread.
   - Do not block the REPL or file transfers.

3. Implement query ID deduplication.
   - Use 64-bit random IDs represented as 16 hex chars.
   - Store seen IDs with expiry.
   - Ignore repeated queries.
   - Expire IDs after the configured window, currently specified as 60 seconds
     in the plan.

4. Implement forwarding.
   - Decrement TTL.
   - Do not forward to the sender.
   - Stop forwarding when TTL reaches zero.
   - Cap TTL at `P2P_MAX_TTL`.

5. Implement local search for distributed queries.
   - Scan or maintain metadata for the local share folder.
   - If files match, respond directly to the originator.

6. Implement `search_distributed`.
   - Build `query_msg_t`.
   - Send it to known neighbors.
   - Collect responses through the aggregator for the configured timeout.
   - Return results in the same shape expected by the REPL.

7. Keep the LaTeX report alive.
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

Checkpoint 1: central server demo

- Student 1 implements server `REGISTER` and `FIND`.
- Student 2 verifies client registration and `find -s`.
- Expected result: one client registers files, and another process can search
  through the server.

Checkpoint 2: distributed search and transfer demo

- Student 3 implements flood search.
- Student 2 implements segmented transfer.
- Expected result: `find -d` returns results, and `request <S> <H>` downloads
  and reassembles a file from multiple peers.

Final integration:

- Run three clients on a LAN or local machine with different ports.
- Share files from different folders.
- Test `find -s`, `find -d`, and `request`.
- Remove or rename a share folder while the client runs to verify hot-unplug
  safety.
- Update the LaTeX report with measured behavior and design decisions.

## Current Risks

- Server message handling is still the main blocker for central search.
- Distributed search has neighbor and aggregator scaffolding, but no flood
  implementation yet.
- Transfer request, sender, receiver, and assembly are still stubs.
- Protocol structs are frozen enough to proceed, but any future change will
  require coordinated updates across client, server, search, and tests.
- The report exists, but content should be filled continuously to avoid a last
  minute documentation crunch.
