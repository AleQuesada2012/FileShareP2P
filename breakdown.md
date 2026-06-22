# Repository Breakdown

This document records the final repository state against `Proyecto2.tex` and
`AGENTS.md`, organized by student ownership. At this point the implementation is
complete on `dev` and ready for the final merge into `main`.

## Final Project State

The simulator is no longer in an implementation phase. Phase 1 foundations,
Phase 2 core features, and the final documentation/test polish are present on
`dev`.

Completed capabilities:

- Shared protocol structs and opcodes are defined in `common/protocol.h`.
- File identity is based on `(size_bytes, hash)`.
- The custom deterministic hash function is implemented and tested.
- TCP socket wrappers support framed sends/receives and are used across modules.
- The server accepts registrations, central search, identity search, malformed
  requests, unsupported opcodes, and unexpected disconnects.
- Clients scan share folders, register file metadata, start their data listener,
  and provide the interactive REPL.
- The REPL supports `find -s <name>`, `find -d <name>`, plain `find <name>`,
  and `request <S> <H>`.
- Distributed search uses neighbors, TCP flood messages, query-ID
  deduplication, TTL, and response aggregation.
- Transfer requests download byte ranges from one or more peers and assemble the
  final file with the original result filename.
- Hot-unplug-style failures are handled defensively so clients and the server do
  not crash when source or destination share folders disappear.
- Unit tests and integration tests cover the main protocol, server, search,
  transfer, fallback, and hot-unplug paths.
- The Spanish LaTeX report lives in `docs/main.tex`, uses `docs/img/` and
  `docs/ref.bib`, and the built deliverable is `docs/main.pdf`.

## Module Map

| Path | Owner | Final role | Status |
|---|---|---|---|
| `common/protocol.h` | Student 1 | Shared wire contract: opcodes, headers, payload structs, constants | Complete and frozen |
| `common/hash.c/h` | Student 1 | Deterministic `uint64_t` file hashing | Complete and tested |
| `common/net.c/h` | Student 2 | TCP connect/listen and length-prefixed send/receive helpers | Complete and tested |
| `server/main.c` | Student 1 | Server startup, SIGPIPE handling, registry lifecycle | Complete |
| `server/registry.c/h` | Student 1 | Thread-safe in-memory peer registry and file lookup | Complete |
| `server/query_handler.c/h` | Student 1 | Per-connection `REGISTER`, `FIND`, identity lookup, `ERROR`, and logging | Complete and tested |
| `client/main.c` | Student 2 | CLI parsing, share scan, registration, listener startup, REPL startup | Complete |
| `client/server_api.c/h` | Student 2 | Client-side `REGISTER` and `FIND` protocol calls | Complete |
| `client/scanner.c/h` | Student 2 | Recursive folder scan and file metadata collection | Complete |
| `client/repl.c/h` | Student 2 | User commands and request orchestration | Complete |
| `transfer/listener.c/h` | Student 2 | Unified client data-port listener dispatching transfer/search messages | Complete |
| `transfer/sender.c/h` | Student 2 | Validated byte-range file sender | Complete and tested |
| `transfer/receiver.c/h` | Student 2 | Multi-peer range requests and file assembly | Complete and tested |
| `search/neighbors.c/h` | Student 3 | Neighbor list and `search_distributed` API | Complete |
| `search/flood.c/h` | Student 3 | Flood handling, local matching, forwarding, TTL, deduplication | Complete |
| `search/aggregator.c/h` | Student 3 | Thread-safe distributed result collection | Complete |
| `docs/` | Student 3 | Spanish LaTeX report, images, bibliography, built PDF | Complete |
| `tests/unit/` | All | Unit coverage for shared and module behavior | Complete for final scope |
| `tests/integration/` | All | Multi-process smoke coverage | Complete for final scope |

## Student 1: Server And Protocol Architect

Student 1 owns `server/`, `common/hash.c`, and `common/protocol.h`.

Final delivered work:

- `common/protocol.h` defines the shared message header, file metadata, peer
  metadata, registration messages, search messages, transfer payloads,
  distributed-search payloads, and error messages.
- The protocol uses `net_send_msg` / `net_recv_msg` as the outer framed TCP
  transport.
- Multi-byte fields are treated as network-order wire values where required.
- `common/hash.c` implements deterministic FNV-1a hashing.
- `server/registry.c` handles peer registration, replacement/update of existing
  peers, filename search, `(hash, size)` identity search, and recent-peer
  lookup.
- `server/query_handler.c` accepts concurrent clients, dispatches `REGISTER` and
  `FIND`, returns `ERROR` for invalid messages, and logs request processing for
  demos.
- Server-related unit and integration tests are present.

Final caution:

- `common/protocol.h` should remain frozen for submission. Any future change
  would require coordinated updates to server, client, search, transfer, tests,
  and documentation.

## Student 2: Client Core And File Transfer

Student 2 owns `client/`, `transfer/`, and `common/net.c`.

Final delivered work:

- `common/net.c` implements TCP helper functions for connect, listen, exact
  send/receive, framed send/receive, and close.
- `client/main.c` parses server address, server port, client data port, share
  folder, TTL, and search timeout arguments.
- `client/scanner.c` recursively scans the share folder and computes hash/size
  metadata.
- `client/server_api.c` sends registration and central search messages.
- `client/repl.c` implements `find -s`, `find -d`, plain `find` fallback, and
  `request <S> <H>`.
- `request <S> <H>` refreshes peers through server identity lookup and falls
  back to cached compatible search results when needed.
- `transfer/listener.c` provides the unified client data-port listener.
- `transfer/sender.c` serves validated byte ranges to remote peers.
- `transfer/receiver.c` splits downloads across available peers, writes
  segments into the target file, preserves the original filename, and handles
  hot-unplug-style failures.
- Integration tests cover central request, identity request, distributed
  request, plain fallback, hot-unplug, and the gated large multi-peer `.mkv`
  scenario.

Final caution:

- The final LAN/demo validation should still be run on the machines used for
  presentation, because firewall, routing, and Wi-Fi isolation behavior can vary
  outside the repository.

## Student 3: Distributed Search And Documentation

Student 3 owns `search/` and `docs/`.

Final delivered work:

- `search/neighbors.c` maintains known peers and provides the
  `search_distributed` API used by the REPL.
- `search/flood.c` handles `QUERY_FLOOD` and `QUERY_RESULT` messages on the
  unified client data port.
- Query IDs are deduplicated with expiry, and TTL limits propagation.
- Local search responses use startup scan metadata, which avoids re-hashing
  large files during each distributed query.
- `search/aggregator.c` collects distributed responses during the configured
  response window.
- Distributed results are formatted so `request <S> <H>` can use them like
  server results.
- The report is written in Spanish in `docs/main.tex`; images are beside it in
  `docs/img/`, bibliography entries are in `docs/ref.bib`, and the current PDF
  deliverable is `docs/main.pdf`.

Final caution:

- TTL and timeout choices should be explained during the demo as propagation
  controls, not arbitrary constants.

## Test Coverage

Unit tests:

- `tests/unit/test_hash.c`
- `tests/unit/test_net.c`
- `tests/unit/test_protocol_roundtrip.c`
- `tests/unit/test_registry.c`
- `tests/unit/test_query_handler.c`
- `tests/unit/test_transfer_sender.c`
- `tests/unit/test_transfer_receiver.c`

Integration tests:

- `tests/integration/test_central_server_client.sh`
- `tests/integration/test_central_request_transfer.sh`
- `tests/integration/test_identity_request_transfer.sh`
- `tests/integration/test_distributed_search.sh`
- `tests/integration/test_distributed_request_transfer.sh`
- `tests/integration/test_request_identity_refresh.sh`
- `tests/integration/test_plain_find_fallback.sh`
- `tests/integration/test_hot_unplug_request.sh`
- `tests/integration/test_large_multi_peer_transfer.sh`

Default validation:

```sh
make
make test
make docs
```

Optional final large-file validation:

```sh
RUN_LARGE_INTEGRATION=1 sh tests/integration/test_large_multi_peer_transfer.sh
```

## Git Workflow For Final Merge

The final project state should be committed on `dev` first. After that, merge
`dev` into `main` according to the project workflow.

Recommended local sequence:

```sh
git switch dev
git pull
make
make test
make docs
git status
git switch main
git pull
git merge --no-ff dev
git push
```

If GitHub branch protection is enabled, open a PR from `dev` to `main` instead
of pushing directly.

## Remaining Work

There is no remaining implementation work planned for the project scope. The
only remaining actions are release actions:

- Run the final validation commands on the submission/demo machine.
- Merge `dev` into `main`.
- Prepare the source ZIP and submit it with the PDF report as required by the
  course instructions.
