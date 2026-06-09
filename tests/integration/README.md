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
uses that cached peer, and the transfer layer writes an identical downloaded
file into client 2's share folder.

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
owning peer, and `request` downloads the file through the existing transfer
path.

## Remaining Manual Scenarios

The automated scripts are local two-client smoke tests. Before final delivery,
the team should still run a three-client scenario on Linux or a LAN and verify:

- multi-hop distributed forwarding,
- TTL limiting,
- duplicate query suppression,
- plain `find <name>` fallback once implemented,
- hot-unplug behavior when a share folder disappears while clients keep running.
