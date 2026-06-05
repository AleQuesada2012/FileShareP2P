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

Planned scenario:

1. Start one server on a known port.
2. Start three clients with separate share folders.
3. Verify `find -s`, `find -d`, and `request S H` against a shared file.
4. Remove one share folder while the client is running and confirm all processes survive.
