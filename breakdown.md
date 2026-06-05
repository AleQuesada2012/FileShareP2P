# Repository Breakdown

This document summarizes the final experimental implementation state against
the assignment specification in `Proyecto2.tex` and the execution plan in
`PLAN.md`.

## Final Project State

The repository now contains an end-to-end P2P simulator:

- Clients scan a share folder, hash files, and register metadata with the
  central server.
- The central server handles `REGISTER`, `FIND`, and malformed request errors.
- Clients support `find -s`, `find -d`, plain `find` fallback, and
  `request S H`.
- File requests split byte ranges across all peers from the latest search
  results and assemble the completed file in the requester share folder.
- Distributed search uses TCP flooding over the client data port, with TTL,
  query-ID deduplication, and response aggregation.
- The client data port has one listener that dispatches transfer and search
  opcodes.
- Hot-unplug behavior was validated by removing a share folder while clients
  remained running.
- The LaTeX report contains the required rubric sections.

## Module Map

| Path | Final role |
|---|---|
| `common/protocol.h` | Frozen shared wire structs, opcodes, constants |
| `common/hash.c/h` | FNV-1a file hashing with size support |
| `common/net.c/h` | TCP connect/listen and framed send/receive helpers |
| `server/main.c` | Server startup and registry lifecycle |
| `server/registry.c/h` | Mutex-protected peer/file registry |
| `server/query_handler.c/h` | Threaded accept loop, `REGISTER`, `FIND`, `ERROR` |
| `client/main.c` | CLI parsing, scan/register, search runtime, peer listener |
| `client/server_api.c/h` | Central server client protocol calls |
| `client/scanner.c/h` | Recursive share-folder scan and metadata generation |
| `client/repl.c/h` | `find -s`, `find -d`, fallback `find`, `request` |
| `transfer/listener.c/h` | Unified data-port peer dispatcher |
| `transfer/sender.c/h` | Hash/size validated byte-range sender |
| `transfer/receiver.c/h` | Multi-peer segment requester and assembler |
| `search/neighbors.c/h` | Neighbor list and search runtime API |
| `search/flood.c/h` | Query IDs, TTL, forwarding, result handling |
| `search/aggregator.c/h` | Thread-safe distributed result collection |
| `docs/` | Completed LaTeX report sections |
| `tests/unit/` | Hash, net, protocol, server, search, transfer tests |
| `tests/integration/` | Repeatable central and three-client smoke procedures |

## Validation Summary

Commands run during the experimental completion:

```sh
make
make test
sh tests/integration/three_client_smoke.sh
make docs
```

The three-client smoke covers:

- one server,
- three clients with separate share folders and data ports,
- centralized `find -s`,
- distributed `find -d`,
- plain `find` fallback for a file created after registration,
- multi-peer `request S H` and file assembly,
- share-folder removal while all processes remain alive.

## Design Notes

- `common/protocol.h` was not changed during the completion work.
- Distributed search uses the frozen `query_msg_t`; because it carries the
  originator but not the immediate sender data port, cycle control relies on
  the seen-query cache instead of same-host sender exclusion.
- Server metadata is in-memory. A future improvement would add peer heartbeats
  or expiry for clients that disappear without re-registering.
- File downloads use `download_<S>_<H>.bin`; the original filename is kept in
  search output but is not used as the destination name.
