#!/bin/sh
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TMP_DIR=$(mktemp -d /tmp/p2p-three-client.XXXXXX)
SERVER_PID=
PEER1_PID=
PEER2_PID=
PEER3_PID=

cleanup() {
    [ -n "${PEER1_PID:-}" ] && printf 'quit\n' >&3 2>/dev/null || true
    [ -n "${PEER2_PID:-}" ] && printf 'quit\n' >&4 2>/dev/null || true
    [ -n "${PEER3_PID:-}" ] && printf 'quit\n' >&5 2>/dev/null || true
    exec 3>&- 2>/dev/null || true
    exec 4>&- 2>/dev/null || true
    exec 5>&- 2>/dev/null || true
    [ -n "${PEER1_PID:-}" ] && wait "$PEER1_PID" 2>/dev/null || true
    [ -n "${PEER2_PID:-}" ] && wait "$PEER2_PID" 2>/dev/null || true
    [ -n "${PEER3_PID:-}" ] && wait "$PEER3_PID" 2>/dev/null || true
    [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null || true
    [ -n "${SERVER_PID:-}" ] && wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

wait_for_count() {
    pattern=$1
    file=$2
    min_count=$3
    deadline=$((SECONDS + 12))

    while [ "$SECONDS" -lt "$deadline" ]; do
        count=$(grep -c "$pattern" "$file" 2>/dev/null || true)
        if [ "$count" -ge "$min_count" ]; then
            return 0
        fi
        sleep 0.2
    done

    echo "Timed out waiting for '$pattern' count $min_count in $file" >&2
    return 1
}

extract_size_hash() {
    pattern=$1
    file=$2
    grep "$pattern" "$file" |
        tail -1 |
        sed -n 's/.*S=\([0-9][0-9]*\) H=\([0-9][0-9]*\).*/\1 \2/p'
}

cd "$ROOT_DIR"
make >/dev/null

mkdir -p "$TMP_DIR/peer1" "$TMP_DIR/peer2" "$TMP_DIR/peer3"
printf 'same alpha payload for segmented transfer\n' > "$TMP_DIR/peer1/alpha-video.txt"
cp "$TMP_DIR/peer1/alpha-video.txt" "$TMP_DIR/peer2/alpha-video.txt"
mkfifo "$TMP_DIR/peer1.in" "$TMP_DIR/peer2.in" "$TMP_DIR/peer3.in"

build/server/p2p-server 19092 > "$TMP_DIR/server.log" 2>&1 &
SERVER_PID=$!
sleep 0.3

build/client/p2p-client 127.0.0.1 19092 19121 "$TMP_DIR/peer1" --search-timeout 700 \
    < "$TMP_DIR/peer1.in" > "$TMP_DIR/peer1.log" 2>&1 &
PEER1_PID=$!
exec 3>"$TMP_DIR/peer1.in"
sleep 0.5

build/client/p2p-client 127.0.0.1 19092 19122 "$TMP_DIR/peer2" --search-timeout 700 \
    < "$TMP_DIR/peer2.in" > "$TMP_DIR/peer2.log" 2>&1 &
PEER2_PID=$!
exec 4>"$TMP_DIR/peer2.in"
sleep 1

printf 'late fallback payload for segmented transfer\n' > "$TMP_DIR/peer1/late-only.txt"
cp "$TMP_DIR/peer1/late-only.txt" "$TMP_DIR/peer2/late-only.txt"

build/client/p2p-client 127.0.0.1 19092 19123 "$TMP_DIR/peer3" --search-timeout 900 \
    < "$TMP_DIR/peer3.in" > "$TMP_DIR/peer3.log" 2>&1 &
PEER3_PID=$!
exec 5>"$TMP_DIR/peer3.in"
sleep 1

printf 'find -s alpha\n' >&5
wait_for_count 'alpha-video.txt' "$TMP_DIR/peer3.log" 2
set -- $(extract_size_hash 'alpha-video.txt' "$TMP_DIR/peer3.log")
ALPHA_SIZE=$1
ALPHA_HASH=$2
printf 'request %s %s\n' "$ALPHA_SIZE" "$ALPHA_HASH" >&5
wait_for_count 'Downloaded file' "$TMP_DIR/peer3.log" 1
cmp "$TMP_DIR/peer1/alpha-video.txt" "$TMP_DIR/peer3/download_${ALPHA_SIZE}_${ALPHA_HASH}.bin"

printf 'find -d alpha\n' >&5
wait_for_count 'alpha-video.txt' "$TMP_DIR/peer3.log" 4
set -- $(extract_size_hash 'alpha-video.txt' "$TMP_DIR/peer3.log")
printf 'request %s %s\n' "$1" "$2" >&5
wait_for_count 'Downloaded file' "$TMP_DIR/peer3.log" 2
cmp "$TMP_DIR/peer1/alpha-video.txt" "$TMP_DIR/peer3/download_${ALPHA_SIZE}_${ALPHA_HASH}.bin"

printf 'find late\n' >&5
wait_for_count 'late-only.txt' "$TMP_DIR/peer3.log" 2
set -- $(extract_size_hash 'late-only.txt' "$TMP_DIR/peer3.log")
LATE_SIZE=$1
LATE_HASH=$2
printf 'request %s %s\n' "$LATE_SIZE" "$LATE_HASH" >&5
wait_for_count 'Downloaded file' "$TMP_DIR/peer3.log" 3
cmp "$TMP_DIR/peer2/late-only.txt" "$TMP_DIR/peer3/download_${LATE_SIZE}_${LATE_HASH}.bin"

rm -rf "$TMP_DIR/peer1"
printf 'find -d alpha\n' >&5
wait_for_count 'alpha-video.txt' "$TMP_DIR/peer3.log" 5
sleep 0.5
kill -0 "$PEER1_PID"
kill -0 "$PEER2_PID"
kill -0 "$PEER3_PID"
kill -0 "$SERVER_PID"

echo "three_client_smoke: ok"
