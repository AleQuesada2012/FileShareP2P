#!/bin/sh
set -eu

PORT=39203
ROOT_DIR=$(mktemp -d)
SERVER_PID=

cleanup() {
    if [ -n "${SERVER_PID}" ]; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    rm -rf "${ROOT_DIR}"
}
trap cleanup EXIT INT TERM

require_log() {
    pattern=$1
    file=$2
    if ! grep -F "$pattern" "$file" >/dev/null 2>&1; then
        echo "Missing expected output: $pattern" >&2
        echo "--- $file ---" >&2
        cat "$file" >&2
        exit 1
    fi
}

mkdir -p "${ROOT_DIR}/peer1" "${ROOT_DIR}/peer2"
printf 'movie bytes from peer one\n' > "${ROOT_DIR}/peer1/movie-alpha.txt"
printf 'notes bytes from peer two\n' > "${ROOT_DIR}/peer2/notes-beta.txt"

./build/server/p2p-server "${PORT}" > "${ROOT_DIR}/server.log" 2>&1 &
SERVER_PID=$!
sleep 1

printf 'quit\n' | ./build/client/p2p-client 127.0.0.1 "${PORT}" 7101 "${ROOT_DIR}/peer1" \
    > "${ROOT_DIR}/client1.log" 2>&1

require_log "Registered 1 file(s) with server 127.0.0.1:${PORT}; received 0 neighbor(s)." \
    "${ROOT_DIR}/client1.log"

printf 'find -s movie\nfind -s notes\nquit\n' | ./build/client/p2p-client 127.0.0.1 "${PORT}" 7102 "${ROOT_DIR}/peer2" \
    > "${ROOT_DIR}/client2.log" 2>&1

require_log "Registered 1 file(s) with server 127.0.0.1:${PORT}; received 1 neighbor(s)." \
    "${ROOT_DIR}/client2.log"
require_log "movie-alpha.txt" "${ROOT_DIR}/client2.log"
require_log "127.0.0.1:7101" "${ROOT_DIR}/client2.log"
require_log "notes-beta.txt" "${ROOT_DIR}/client2.log"
require_log "127.0.0.1:7102" "${ROOT_DIR}/client2.log"

IDENTITY=$(sed -n 's/.*S=\([0-9][0-9]*\) H=\([0-9][0-9]*\).*movie-alpha.txt.*/\1 \2/p' \
    "${ROOT_DIR}/client2.log" | head -n 1)
if [ -z "${IDENTITY}" ]; then
    echo "Could not extract S/H identity for movie-alpha.txt" >&2
    cat "${ROOT_DIR}/client2.log" >&2
    exit 1
fi

printf 'find -s %s\nquit\n' "${IDENTITY}" | ./build/client/p2p-client 127.0.0.1 "${PORT}" 7102 "${ROOT_DIR}/peer2" \
    > "${ROOT_DIR}/identity.log" 2>&1

require_log "movie-alpha.txt" "${ROOT_DIR}/identity.log"
require_log "127.0.0.1:7101" "${ROOT_DIR}/identity.log"

echo "test_central_server_client: ok"
