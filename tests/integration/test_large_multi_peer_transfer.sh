#!/bin/sh
set -eu

if [ "${RUN_LARGE_INTEGRATION:-0}" != "1" ]; then
    echo "test_large_multi_peer_transfer: skipped (set RUN_LARGE_INTEGRATION=1)"
    exit 0
fi

PORT=39283
SERVER_IP="${TEST_SERVER_IP:-127.0.0.1}"
MIN_BYTES=524288000
SOURCE_FILE="${LARGE_TEST_FILE:-}"
ROOT_DIR=
SERVER_PID=
CLIENT1_PID=
CLIENT2_PID=
CLIENT1_IN=
CLIENT2_IN=

cleanup() {
    exec 3>&- 2>/dev/null || true
    exec 4>&- 2>/dev/null || true
    if [ -n "${CLIENT1_PID}" ]; then
        kill "${CLIENT1_PID}" 2>/dev/null || true
        wait "${CLIENT1_PID}" 2>/dev/null || true
    fi
    if [ -n "${CLIENT2_PID}" ]; then
        kill "${CLIENT2_PID}" 2>/dev/null || true
        wait "${CLIENT2_PID}" 2>/dev/null || true
    fi
    if [ -n "${SERVER_PID}" ]; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    if [ -n "${ROOT_DIR}" ]; then
        rm -rf "${ROOT_DIR}"
    fi
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

    while [ "${i}" -lt 900 ]; do
        if grep -F "$pattern" "$file" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 1
    done

    require_log "$pattern" "$file"
}

if [ -z "${SOURCE_FILE}" ]; then
    SOURCE_FILE=$(find "${LARGE_SHARE_DIR:-share}" -type f -name '*.mkv' -print 2>/dev/null | head -n 1)
fi

if [ -z "${SOURCE_FILE}" ] || [ ! -f "${SOURCE_FILE}" ]; then
    echo "Large .mkv source not found; set LARGE_TEST_FILE or place one under ./share" >&2
    exit 1
fi

SOURCE_BYTES=$(wc -c < "${SOURCE_FILE}" | tr -d ' ')
if [ "${SOURCE_BYTES}" -lt "${MIN_BYTES}" ]; then
    echo "Large source must be at least ${MIN_BYTES} bytes: ${SOURCE_FILE}" >&2
    exit 1
fi

mkdir -p build
ROOT_DIR=$(mktemp -d "./build/large-transfer.XXXXXX")
mkdir -p "${ROOT_DIR}/peer1" "${ROOT_DIR}/peer2" "${ROOT_DIR}/peer3"

BASENAME=$(basename "${SOURCE_FILE}")
ln "${SOURCE_FILE}" "${ROOT_DIR}/peer1/${BASENAME}"
ln "${SOURCE_FILE}" "${ROOT_DIR}/peer2/${BASENAME}"

./build/server/p2p-server "${PORT}" > "${ROOT_DIR}/server.log" 2>&1 &
SERVER_PID=$!
sleep 1

CLIENT1_IN="${ROOT_DIR}/client1.in"
mkfifo "${CLIENT1_IN}"
./build/client/p2p-client "${SERVER_IP}" "${PORT}" 7811 "${ROOT_DIR}/peer1" \
    < "${CLIENT1_IN}" > "${ROOT_DIR}/client1.log" 2>&1 &
CLIENT1_PID=$!
exec 3>"${CLIENT1_IN}"

wait_for_log "Registered 1 file(s) with server ${SERVER_IP}:${PORT}; received 0 neighbor(s)." \
    "${ROOT_DIR}/client1.log"

IDENTITY=$(sed -n 's/.* size=\([0-9][0-9]*\) hash=\([0-9][0-9]*\).*/\1 \2/p' \
    "${ROOT_DIR}/client1.log" | head -n 1)
if [ -z "${IDENTITY}" ]; then
    echo "Could not extract large file identity from client 1 log" >&2
    cat "${ROOT_DIR}/client1.log" >&2
    exit 1
fi
SIZE=$(printf '%s\n' "${IDENTITY}" | awk '{print $1}')
HASH=$(printf '%s\n' "${IDENTITY}" | awk '{print $2}')

CLIENT2_IN="${ROOT_DIR}/client2.in"
mkfifo "${CLIENT2_IN}"
./build/client/p2p-client "${SERVER_IP}" "${PORT}" 7812 "${ROOT_DIR}/peer2" \
    < "${CLIENT2_IN}" > "${ROOT_DIR}/client2.log" 2>&1 &
CLIENT2_PID=$!
exec 4>"${CLIENT2_IN}"

wait_for_log "Registered 1 file(s) with server ${SERVER_IP}:${PORT}; received 1 neighbor(s)." \
    "${ROOT_DIR}/client2.log"

printf 'find -s %s\nrequest %s %s\nquit\n' "${BASENAME}" "${SIZE}" "${HASH}" | \
    ./build/client/p2p-client "${SERVER_IP}" "${PORT}" 7813 "${ROOT_DIR}/peer3" \
    > "${ROOT_DIR}/client3.log" 2>&1

require_log "${BASENAME}" "${ROOT_DIR}/client3.log"
require_log "Refreshed 2 peer(s) through server identity lookup." "${ROOT_DIR}/client3.log"
require_log "as 2 segment(s)." "${ROOT_DIR}/client3.log"
require_log "segment 1: bytes" "${ROOT_DIR}/client3.log"
require_log "segment 2: bytes" "${ROOT_DIR}/client3.log"
require_log "Downloaded file to" "${ROOT_DIR}/client3.log"

DOWNLOADED="${ROOT_DIR}/peer3/${BASENAME}"
if [ ! -f "${DOWNLOADED}" ]; then
    echo "Downloaded .mkv kept no original name" >&2
    cat "${ROOT_DIR}/client3.log" >&2
    exit 1
fi

cmp "${SOURCE_FILE}" "${DOWNLOADED}" >/dev/null

printf 'quit\n' >&3
exec 3>&-
wait "${CLIENT1_PID}"
CLIENT1_PID=

printf 'quit\n' >&4
exec 4>&-
wait "${CLIENT2_PID}"
CLIENT2_PID=

echo "test_large_multi_peer_transfer: ok"
