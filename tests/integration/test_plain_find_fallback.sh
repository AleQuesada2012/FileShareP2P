#!/bin/sh
set -eu

PORT=39263
SERVER_IP="${TEST_SERVER_IP:-127.0.0.1}"
LOCAL_IP=$(./build/tests/get_local_ip "${SERVER_IP}" "${PORT}")
ROOT_DIR=$(mktemp -d)
SERVER_PID=
CLIENT1_PID=
CLIENT2_PID=
CLIENT2_IN=

cleanup() {
    exec 3>&- 2>/dev/null || true
    if [ -n "${CLIENT2_PID}" ]; then
        kill "${CLIENT2_PID}" 2>/dev/null || true
        wait "${CLIENT2_PID}" 2>/dev/null || true
    fi
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
printf 'hello' > "${ROOT_DIR}/peer1/fallback-p2p.txt"

./build/server/p2p-server "${PORT}" > "${ROOT_DIR}/server.log" 2>&1 &
SERVER_PID=$!
sleep 1

# Client 1 registers the searchable file and stays online as a distributed
# neighbor after the central server is stopped.
(sleep 10; printf 'quit\n') | ./build/client/p2p-client "${SERVER_IP}" "${PORT}" 7711 "${ROOT_DIR}/peer1" \
    > "${ROOT_DIR}/client1.log" 2>&1 &
CLIENT1_PID=$!

wait_for_log "Registered 1 file(s) with server ${SERVER_IP}:${PORT}; received 0 neighbor(s)." \
    "${ROOT_DIR}/client1.log"

CLIENT2_IN="${ROOT_DIR}/client2.in"
mkfifo "${CLIENT2_IN}"
./build/client/p2p-client "${SERVER_IP}" "${PORT}" 7712 "${ROOT_DIR}/peer2" \
    < "${CLIENT2_IN}" > "${ROOT_DIR}/client2.log" 2>&1 &
CLIENT2_PID=$!
exec 3>"${CLIENT2_IN}"

wait_for_log "Registered 0 file(s) with server ${SERVER_IP}:${PORT}; received 1 neighbor(s)." \
    "${ROOT_DIR}/client2.log"

# Force plain find to exercise the server-failure fallback path.
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true
SERVER_PID=

printf 'find fallback-p2p\nquit\n' >&3
exec 3>&-

if ! wait "${CLIENT2_PID}"; then
    CLIENT2_PID=
    cat "${ROOT_DIR}/client2.log" >&2
    exit 1
fi
CLIENT2_PID=

require_log "Server search unavailable for 'fallback-p2p'; falling back to distributed search" \
    "${ROOT_DIR}/client2.log"
require_log "Iniciando búsqueda P2P en la red distribuida..." "${ROOT_DIR}/client2.log"
require_log "fallback-p2p.txt" "${ROOT_DIR}/client2.log"
require_log "${LOCAL_IP}:7711" "${ROOT_DIR}/client2.log"

echo "test_plain_find_fallback: ok"
