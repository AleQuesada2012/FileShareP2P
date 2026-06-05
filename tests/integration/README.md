# Integration tests

This folder records repeatable smoke procedures for the running binaries.

## Central server smoke

Purpose: verify startup registration and `find -s` through the real server.

Terminal 1:

```sh
build/server/p2p-server 19090
```

Terminal 2:

```sh
tmpdir=$(mktemp -d /tmp/p2p-m2.XXXXXX)
mkdir -p "$tmpdir/peer1" "$tmpdir/peer2"
printf 'alpha from peer one\n' > "$tmpdir/peer1/alpha-video.txt"
printf 'beta from peer two\n' > "$tmpdir/peer2/beta.txt"

printf 'quit\n' |
  build/client/p2p-client 127.0.0.1 19090 19101 "$tmpdir/peer1"

printf 'find -s alpha\nquit\n' |
  build/client/p2p-client 127.0.0.1 19090 19102 "$tmpdir/peer2"

rm -rf "$tmpdir"
```

Expected result: the first client registers one file; the second client
registers one file, receives one neighbor, and prints one `find -s alpha`
result for `alpha-video.txt`.

## Final three-client smoke

The full smoke can be run after `make` with:

```sh
sh tests/integration/three_client_smoke.sh
```

It starts one server and three clients with separate share folders. Peer 1 and
peer 2 share the same file so peer 3 can validate segmented multi-peer
downloads after `find -s` and `find -d`. The script then creates another file
after registration so plain `find` must fall back to distributed search before
requesting it. Finally, it removes peer 1's share folder while all processes
are running and verifies that the clients and server survive.
