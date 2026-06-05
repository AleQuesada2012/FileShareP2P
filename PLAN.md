# Experimental Completion Plan

This file is the live execution ledger for the separate experimental Codex
environment. Keep it updated so the run can survive context compaction and be
resumed by another agent without guessing.

## Source Priority

Resolve requirements in this order:

1. `Proyecto2.tex`
2. `AGENTS-copy.md`
3. `AGENTS.md`
4. `PLAN.md`
5. `breakdown.md`
6. Current source code

If sources conflict, preserve the project requirements from `Proyecto2.tex` and
record the decision in this file.

## Current Snapshot To Copy

The experimental environment starts from an exact copy of the current working
directory, not a clean clone.

- Current date of this handoff: 2026-06-05.
- Current branch in the original workdir: `feat/s2-client`.
- Current git status in the original workdir:

```text
## feat/s2-client...origin/feat/s2-client
?? AGENTS-copy.md
?? EXPERIMENT_STARTER.md
?? PLAN.md
?? Proyecto2.pdf
```

- `Proyecto2.tex` is present at the repository root and is ignored by
  `.gitignore`; it must still be treated as the primary source of truth.
- `Proyecto2.pdf` is untracked and present in the copied state.
- `AGENTS.md` is ignored in this repository; use `AGENTS-copy.md` in the
  experiment instead of modifying the original `AGENTS.md`.
- `PLAN.md`, `AGENTS-copy.md`, and `EXPERIMENT_STARTER.md` are intentionally
  untracked handoff files in the original workdir.

## Non-Negotiable Experiment Rules

- Create or switch to `experimental` before implementation work.
- Never push to `main`, `dev`, `feat/common`, `feat/s1-server`,
  `feat/s2-client`, `feat/s3-search`, or any existing remote branch.
- Remote pushes are allowed only with:

```sh
git push -u origin experimental
```

- Keep `common/protocol.h` frozen unless a change is strictly required by the
  project spec. If it changes, document why here and update tests.
- Update this file after every milestone, before long work, after context
  compaction, after each commit, and after any blocker or design decision.
- Update `breakdown.md` near the end as the final project-state summary.
- Run `make test` at every implementation milestone.

## Roadmap

### M0: Baseline and Branch Setup

- Reread `Proyecto2.tex`, `AGENTS-copy.md`, `AGENTS.md`, `PLAN.md`, and
  `breakdown.md`.
- Confirm copied git status matches the snapshot above.
- Create or switch to `experimental`.
- Run `make` and `make test`.
- Record the results here.

### M1: Central Server

- Implement `server/query_handler.c` for `REGISTER`, `FIND`, and `ERROR`.
- Use the existing framed protocol: outer `net_send_msg` / `net_recv_msg`,
  then `p2p_msg_header_t`, then opcode payload.
- Decode and encode all multi-byte integer fields in network byte order.
- Register peers using the connected client IP plus advertised data port.
- Return recent peers in `REGISTER_RESP`, excluding the registering peer.
- Return matching `(S, H, IP, port, name)` entries in `FIND_RESP`.
- Handle disconnects, malformed frames, bad versions, unknown opcodes, and
  registry-full errors without crashing.
- Add focused tests for registration, find, malformed messages, and early
  disconnects.

### M2: Centralized Client Integration

- Verify startup registration succeeds against the real server.
- Verify `find -s <name>` prints server results from registered peers.
- Keep the current `request <S> <H>` behavior using the latest displayed search
  results as the peer source.
- Add a small integration note or script for the server plus one or two clients.

### M3: Unified Peer Listener

- Replace competing listener assumptions with one client data-port listener.
- Dispatch incoming peer messages by opcode:
  - `TRANSFER_REQ` to transfer sender logic.
  - `QUERY_FLOOD` and `QUERY_RESULT` to distributed-search logic.
  - invalid messages to `ERROR`.
- Preserve the existing transfer sender and receiver behavior.
- Ensure the REPL remains on the main thread and peer handling uses worker
  threads.

### M4: Distributed Search

- Seed the neighbor list from `register_resp_t`.
- Start the flood listener in a dedicated thread.
- Generate 64-bit query IDs as 16 hex chars.
- Maintain a mutex-protected seen-query cache with 60-second expiry.
- On `QUERY_FLOOD`, discard duplicate queries, search local shared files, send
  results directly to the originator, decrement TTL, and forward to neighbors
  except the sender.
- Cap TTL at `P2P_MAX_TTL`; default to `P2P_DEFAULT_TTL`.
- Collect distributed responses for `P2P_DEFAULT_SEARCH_TIMEOUT_MS` or the
  configured timeout.
- Wire `find -d <name>` to distributed search.
- Wire plain `find <name>` to try server first, then distributed search when
  the server fails or returns no results.
- Add tests for neighbor deduplication, query ID deduplication, TTL behavior,
  and aggregation.

### M5: Transfer and Hot-Unplug Validation

- Validate `request <S> <H>` after real `find -s`, `find -d`, and plain `find`
  results.
- Confirm multi-peer byte-range splitting and final assembly into the share
  folder.
- Confirm a missing or removed share folder logs warnings and does not crash the
  client or server.
- Keep transfer tests passing.

### M6: Final Integration and Documentation

- Add or update a three-client integration smoke script or documented procedure.
- Run a local three-client test:
  - one server
  - three clients with separate share folders and ports
  - `find -s`
  - `find -d`
  - plain `find`
  - `request <S> <H>`
  - share-folder removal while processes continue
- Complete the LaTeX report sections required by the documentation rubric.
- Run `make docs` if `pdflatex` is available.
- Update `README.md` and `breakdown.md` as final repository-state summaries.

## Interface Decisions

- Use existing server protocol structs: `REGISTER_REQ`, `REGISTER_RESP`,
  `FIND_REQ`, `FIND_RESP`, and `ERROR`.
- Do not add a metadata-only server lookup opcode for `request <S> <H>` unless
  it becomes necessary; requests may use the latest displayed search results.
- Use TCP for distributed search.
- Use one client data-port listener that dispatches peer opcodes.
- Preserve `search_distributed(term, results_out)` as the REPL-facing search
  API. Add minimal runtime initialization APIs only if needed.

## Test Checklist

- `make`
- `make test`
- Existing unit tests:
  - hash
  - net
  - protocol round-trip
  - transfer sender
  - transfer receiver
- New server tests:
  - registration
  - find
  - malformed message
  - disconnect safety
- New search tests:
  - neighbor deduplication
  - query ID deduplication
  - TTL decrement and stop
  - aggregation window/results
- Final smoke:
  - server plus three clients
  - centralized search
  - distributed search
  - fallback search
  - segmented request/download
  - hot-unplug tolerance

## Running Log

### 2026-06-05 12:04 CST — M0 baseline

- Reread required sources in priority order:
  `Proyecto2.tex`, `AGENTS-copy.md`, `AGENTS.md`, `PLAN.md`,
  `breakdown.md`, and `README.md`.
- Confirmed the copied state is on `feat/s2-client` with untracked
  `AGENTS-copy.md`, `EXPERIMENT_STARTER.md`, and `PLAN.md`.
- Created and switched to local branch `experimental` before implementation
  work.
- Baseline `make` passed.
- Baseline `make test` passed:
  `test_hash`, `test_net`, `test_protocol_roundtrip`,
  `test_transfer_sender`, and `test_transfer_receiver`.

### 2026-06-05 12:10 CST — M1 server handler

- Implemented threaded server accept/dispatch in `server/query_handler.c`.
- Added single-client handler entry point for focused tests.
- Implemented `REGISTER`, `FIND`, and `ERROR` responses using the existing
  framed protocol and network byte order conversions.
- `REGISTER` stores the connected client IP plus advertised data port and
  returns recent neighbors excluding the registering peer.
- `FIND` returns matching `(S, H, IP, port, name)` entries from the registry.
- Added `tests/unit/test_server_query_handler.c` covering registration,
  neighbor filtering, centralized find, malformed frames, and early disconnect.
- `make test` passed after M1:
  `test_hash`, `test_net`, `test_protocol_roundtrip`,
  `test_server_query_handler`, `test_transfer_sender`, and
  `test_transfer_receiver`.
- `make` passed after M1 and rebuilt `build/server/p2p-server`.
- Committed M1 as `7e9e264 feat(server): implement central query handler`.

### 2026-06-05 12:12 CST — M2 centralized smoke

- Ran real server on local port `19090`.
- Ran one client with `peer1/alpha-video.txt`; startup scan and registration
  succeeded with zero neighbors.
- Ran a second client with `peer2/beta.txt`; registration succeeded with one
  neighbor.
- `find -s alpha` from the second client returned one result for
  `alpha-video.txt` from the first peer, proving central `REGISTER` + `FIND`
  integration works with the real binaries.
- Added the repeatable M2 procedure to `tests/integration/README.md`.
- `make test` passed after M2:
  `test_hash`, `test_net`, `test_protocol_roundtrip`,
  `test_server_query_handler`, `test_transfer_sender`, and
  `test_transfer_receiver`.
- Committed M2 as `4e35001 docs(integration): add central server smoke`.

### 2026-06-05 12:21 CST — M3/M4 unified peer listener and distributed search

- Replaced the transfer-only data-port listener with one peer listener that
  dispatches `TRANSFER_REQ`, `QUERY_FLOOD`, `QUERY_RESULT`, and invalid peer
  opcodes.
- Implemented distributed-search runtime initialization, neighbor seeding from
  `REGISTER_RESP`, dynamic neighbor updates from search results, query ID
  generation, seen-query deduplication with 60-second expiry, TTL decrement and
  cap behavior, local share-folder scanning, direct `QUERY_RESULT` responses,
  active response aggregation, and `search_distributed(term, results_out)`.
- Wired `find -d <name>` to distributed search.
- Wired plain `find <name>` to try the central server first and fall back to
  distributed search when the server fails or returns no results.
- Kept `common/protocol.h` frozen. Design note: the frozen `query_msg_t` carries
  the originator but not the immediate sender's listening data port; forwarding
  therefore relies on the query-ID cache to suppress cycles instead of guessing
  same-host sender ports.
- Added `tests/unit/test_search.c` covering neighbor deduplication, query ID
  deduplication/expiry, TTL behavior, aggregation reset/deduplication, and an
  empty distributed-search response window.
- Ran a real local distributed smoke with one server and two live clients:
  `find -d alpha` returned peer 1, and plain `find late` found a file created
  on peer 1 after registration, proving server-empty fallback.
- `make test` passed after M3/M4:
  `test_hash`, `test_net`, `test_protocol_roundtrip`,
  `test_server_query_handler`, `test_search`, `test_transfer_sender`, and
  `test_transfer_receiver`.
- `make` passed after M3/M4.

### 2026-06-05 Handoff

- Created this plan for the experimental environment.
- Starting branch in original workdir is `feat/s2-client`.
- Original workdir has untracked `AGENTS-copy.md`, `EXPERIMENT_STARTER.md`,
  `PLAN.md`, and `Proyecto2.pdf`.
- Primary unfinished areas are server query handling, distributed search,
  unified peer dispatch, final integration, hot-unplug validation, and report
  completion.
- No implementation work has been done in this original workdir for the
  experiment.
