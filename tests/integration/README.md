# Integration Tests

Run all integration tests through:

```sh
make integration-test
```

`make test` runs the unit tests first, builds the server and client binaries,
and then executes every script listed in `INTEGRATION_TESTS` in the root
`Makefile`. Each script creates temporary share folders, starts its own server
on a fixed local port, launches clients through piped REPL commands, checks log
output, and removes temporary data on exit.

The large `.mkv` multi-peer test is listed in the Makefile but skipped by
default. Enable it explicitly when the local `./share/` video is available:

```sh
RUN_LARGE_INTEGRATION=1 sh tests/integration/test_large_multi_peer_transfer.sh
```

## `test_central_server_client.sh`

Verifies Student 1 and Student 2 central registration/search integration.

- Starts one server.
- Starts a first client with `movie-alpha.txt` and exits after registration.
- Starts a second client with `notes-beta.txt`.
- Runs `find -s movie` and `find -s notes`.
- Expects the second client to see both its own file and the first peer's file.
- Extracts the `S H` identity for `movie-alpha.txt`.
- Runs `find -s <S> <H>` to verify Student 1's identity lookup path.

Expected result: the server registers both peers, filename search works, and
identity search returns the peer that owns the requested file identity.

## `test_central_request_transfer.sh`

Verifies that a central filename search can feed Student 2's transfer request.

- Starts one server.
- Keeps client 1 alive with `hello-source.txt` in its share folder.
- Starts client 2 with an empty share folder.
- Runs `find -s hello`.
- Runs `request 5 0xa430d84680aabd0b`.
- Checks that client 2 reports a download.
- Compares the downloaded file against client 1's source file.

Expected result: the central `find -s` output is cached by the REPL, `request`
refreshes peers through identity lookup, and the transfer layer writes an
identical downloaded `.txt` file into client 2's share folder.

## `test_identity_request_transfer.sh`

Verifies that Student 1's central identity lookup can also feed Student 2's
transfer request.

- Starts one server.
- Keeps client 1 alive with `identity-source.txt`.
- Starts client 2 with an empty share folder.
- Runs `find -s 5 11831194018420276491`.
- Runs `request 5 11831194018420276491`.
- Checks that the identity search returns client 1.
- Compares the downloaded file against client 1's source file.

Expected result: `FIND` by `(S,H)` returns the owning peer, and the subsequent
request downloads the exact file.
The downloaded name keeps the original `.txt` extension.

## `test_distributed_search.sh`

Verifies Student 3's distributed `find -d` path.

- Starts one server so clients can receive neighbor lists during registration.
- Keeps client 1 alive with `archivo-p2p.txt`.
- Starts client 2 with an empty share folder.
- Runs `find -d archivo-p2p`.
- Checks that the REPL announces distributed search.
- Checks that the result includes `archivo-p2p.txt` from client 1.

Expected result: client 2 floods the query to its known neighbor, client 1 scans
its local share folder, and client 2 prints the returned metadata.

## `test_distributed_request_transfer.sh`

Verifies that a distributed search result can feed Student 2's transfer request.

- Starts one server.
- Keeps client 1 alive with `archivo-p2p.txt`.
- Starts client 2 with an empty share folder.
- Runs `find -d archivo-p2p`.
- Runs `request 29 7594717707387257083`.
- Checks that client 2 reports a download.
- Compares the downloaded file against client 1's source file.

Expected result: distributed search populates the REPL result cache with the
owning peer. The request path still attempts a server identity refresh first,
then downloads the file through the existing transfer path.
The downloaded name keeps the original `.txt` extension.

## `test_request_identity_refresh.sh`

Verifies that `request <S> <H>` no longer requires a previously displayed
search result.

- Starts one server.
- Keeps client 1 alive with `identity-refresh.txt`.
- Starts client 2 with an empty share folder.
- Runs `request 5 11831194018420276491` without running `find` first.
- Checks that client 2 refreshed one peer through server identity lookup.
- Compares the downloaded file against client 1's source file.

Expected result: `request` sends an identity `FIND` to the server, receives the
owning peer, and downloads the exact file.
The downloaded name keeps the original `.txt` extension.

## `test_plain_find_fallback.sh`

Verifies the spec behavior for plain `find <name>`.

- Starts one server.
- Keeps client 1 alive with `fallback-p2p.txt`.
- Starts client 2 and lets it receive client 1 as a neighbor during
  registration.
- Stops the server before issuing the REPL command.
- Runs `find fallback-p2p`.
- Checks that client 2 reports server fallback, starts distributed search, and
  prints the peer result.

Expected result: when the server is unavailable, plain `find` falls back to the
distributed search path and displays results in the normal result format.

## `test_hot_unplug_request.sh`

Verifies the local hot-unplug requirement for Student 2's request path.

- Starts one server.
- Keeps client 1 alive with `hot-source.txt`.
- Starts client 2 with an empty share folder.
- Removes client 2's share folder while the client process is still running.
- Runs `request 5 11831194018420276491`, then `help`, then `quit`.
- Checks that the request refreshed a peer, the missing share folder was logged
  as a warning, the help command still ran, and the server process stayed alive.

Expected result: removing the share folder causes the request to fail cleanly
with a warning, but neither the client REPL nor the server crashes.

## `test_large_multi_peer_transfer.sh`

Verifies the 3-client large-file split-transfer path with a real video file.

- Requires `RUN_LARGE_INTEGRATION=1`.
- Uses the first `.mkv` under `./share/`, or `LARGE_TEST_FILE` if provided.
- Requires the source file to be at least 500 MiB.
- Hard-links the same `.mkv` into two source peer folders so both peers share
  the same `(S,H)` identity without duplicating the source file.
- Starts one server, two source clients, and one requesting client.
- Runs `find -s <video-name>` and `request <S> <H>` from the requester.
- Checks that request identity refresh returns two peers.
- Checks that the receiver splits the download into two segments.
- Compares the downloaded `.mkv` against the original video.

Expected result: the requester rebuilds the large video from two peer ranges,
keeps the `.mkv` extension in the downloaded filename, and the final bytes match
the source exactly.

## Remaining Manual Scenarios

The automated scripts are local two-client smoke tests. Before final delivery,
the team should still run a three-client scenario on Linux or a LAN and verify:

- multi-hop distributed forwarding,
- TTL limiting,
- duplicate query suppression,
- hot-unplug behavior on the final Linux target environment.
