#!/bin/sh
set -eu

PORT=39253
SERVER_IP="${TEST_SERVER_IP:-127.0.0.1}"
ROOT_DIR=$(mktemp -d)
SERVER_PID=
CLIENT1_PID=

cleanup() {
    if [ -n "${CLIENT1_PID}" ]; then
        kill "${CLIENT1_PID}" 2>/dev/null || true
        wait "${CLIENT1_PID}" 2>/dev/null || true
    fi
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

wait_for_log() {
    pattern=$1
    file=$2
    i=0

    while [ "${i}" -lt 50 ]; do
        if grep -F "$pattern" "$file" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.1
    done

    require_log "$pattern" "$file"
}

mkdir -p "${ROOT_DIR}/peer1" "${ROOT_DIR}/peer2"
printf 'hello' > "${ROOT_DIR}/peer1/identity-refresh.txt"

./build/server/p2p-server "${PORT}" > "${ROOT_DIR}/server.log" 2>&1 &
SERVER_PID=$!
sleep 1

# Keep the source peer online so the requester can refresh peers by identity
# and then open a transfer connection without a preceding find command.
(sleep 8; printf 'quit\n') | ./build/client/p2p-client ${SERVER_IP} "${PORT}" 7411 "${ROOT_DIR}/peer1" \
    > "${ROOT_DIR}/client1.log" 2>&1 &
CLIENT1_PID=$!

wait_for_log "Registered 1 file(s) with server ${SERVER_IP}:${PORT}; received 0 neighbor(s)." \
    "${ROOT_DIR}/client1.log"

printf 'request 5 11831194018420276491\nquit\n' | \
    ./build/client/p2p-client ${SERVER_IP} "${PORT}" 7412 "${ROOT_DIR}/peer2" \
    > "${ROOT_DIR}/client2.log" 2>&1

require_log "Refreshed 1 peer(s) through server identity lookup." "${ROOT_DIR}/client2.log"
require_log "Downloaded file to" "${ROOT_DIR}/client2.log"

DOWNLOADED="${ROOT_DIR}/peer2/identity-refresh.txt"
if [ ! -f "${DOWNLOADED}" ]; then
    echo "Downloaded file kept no original name" >&2
    cat "${ROOT_DIR}/client2.log" >&2
    exit 1
fi

cmp "${ROOT_DIR}/peer1/identity-refresh.txt" "${DOWNLOADED}" >/dev/null

echo "test_request_identity_refresh: ok"
