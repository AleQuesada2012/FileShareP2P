#include "client/server_api.h"
#include "common/net.h"
#include "server/query_handler.h"
#include "server/registry.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST_SERVER_PORT "39191"

typedef struct {
    p2p_msg_header_t header;
    find_req_t payload;
} find_req_frame_t;

typedef struct {
    p2p_msg_header_t header;
    error_msg_t payload;
} error_frame_t;

static pid_t server_pid = -1;

static void stop_server(void)
{
    int status;

    if (server_pid > 0) {
        (void)kill(server_pid, SIGTERM);
        (void)waitpid(server_pid, &status, 0);
        server_pid = -1;
    }
}

static void require(int condition)
{
    if (!condition) {
        stop_server();
        assert(condition);
    }
}

static int wait_for_server(void)
{
    int i;

    for (i = 0; i < 50; ++i) {
        struct timespec delay;
        int fd = net_connect("127.0.0.1", TEST_SERVER_PORT);
        if (fd >= 0) {
            net_close(fd);
            return 0;
        }

        delay.tv_sec = 0;
        delay.tv_nsec = 20000000L;
        (void)nanosleep(&delay, NULL);
    }

    return -1;
}

static file_meta_t make_file(const char *name, uint64_t hash, uint64_t size)
{
    file_meta_t file;

    memset(&file, 0, sizeof(file));
    (void)snprintf(file.name, sizeof(file.name), "%s", name);
    file.hash = hash;
    file.size_bytes = size;
    return file;
}

static void send_header_only(uint16_t opcode, uint16_t version, uint32_t payload_len)
{
    p2p_msg_header_t header;
    int fd;

    memset(&header, 0, sizeof(header));
    header.opcode = htons(opcode);
    header.version = htons(version);
    header.payload_len = htonl(payload_len);

    fd = net_connect("127.0.0.1", TEST_SERVER_PORT);
    require(fd >= 0);
    require(net_send_msg(fd, &header, (uint32_t)sizeof(header)) == 0);
    net_close(fd);
}

static uint32_t send_frame_and_recv_error(const void *frame, uint32_t frame_len)
{
    error_frame_t error_frame;
    uint32_t received_len = 0u;
    int fd;
    int rc;

    fd = net_connect("127.0.0.1", TEST_SERVER_PORT);
    require(fd >= 0);
    require(net_send_msg(fd, frame, frame_len) == 0);

    memset(&error_frame, 0, sizeof(error_frame));
    rc = net_recv_msg(fd, &error_frame, sizeof(error_frame), &received_len);
    net_close(fd);

    require(rc == 1);
    require(received_len == sizeof(error_frame));
    require(ntohs(error_frame.header.opcode) == (uint16_t)P2P_MSG_ERROR);
    require(ntohs(error_frame.header.version) == (uint16_t)P2P_PROTOCOL_VERSION);
    require(ntohl(error_frame.header.payload_len) == sizeof(error_frame.payload));

    return ntohl(error_frame.payload.code);
}

static void expect_header_error(uint16_t opcode,
                                uint16_t version,
                                uint32_t payload_len,
                                uint32_t expected_code)
{
    p2p_msg_header_t header;

    memset(&header, 0, sizeof(header));
    header.opcode = htons(opcode);
    header.version = htons(version);
    header.payload_len = htonl(payload_len);

    require(send_frame_and_recv_error(&header, (uint32_t)sizeof(header)) == expected_code);
}

static void expect_empty_find_error(void)
{
    find_req_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.header.opcode = htons((uint16_t)P2P_MSG_FIND_REQ);
    frame.header.version = htons((uint16_t)P2P_PROTOCOL_VERSION);
    frame.header.payload_len = htonl((uint32_t)sizeof(frame.payload));

    require(send_frame_and_recv_error(&frame, (uint32_t)sizeof(frame)) == P2P_ERROR_BAD_REQUEST);
}

int main(void)
{
    registry_t registry;
    scan_result_t scan;
    scan_result_t second_scan;
    scan_result_t third_scan;
    register_resp_t register_response;
    search_results_t results;

    server_pid = fork();
    require(server_pid >= 0);

    if (server_pid == 0) {
        if (net_ignore_sigpipe() != 0) {
            return 1;
        }
        if (registry_init(&registry) != 0) {
            return 1;
        }
        (void)query_server_run(TEST_SERVER_PORT, &registry);
        registry_destroy(&registry);
        return 0;
    }

    require(wait_for_server() == 0);

    memset(&scan, 0, sizeof(scan));
    scan.count = 1u;
    scan.files[0] = make_file("movie.mp4", UINT64_C(0x1234), UINT64_C(4096));

    memset(&register_response, 0, sizeof(register_response));
    require(server_register_files("127.0.0.1", TEST_SERVER_PORT, 7001u, &scan, &register_response) == 0);
    require(register_response.neighbor_count == 0u);

    memset(&second_scan, 0, sizeof(second_scan));
    second_scan.count = 1u;
    second_scan.files[0] = make_file("notes.txt", UINT64_C(0x5678), UINT64_C(128));

    memset(&register_response, 0, sizeof(register_response));
    require(server_register_files("127.0.0.1", TEST_SERVER_PORT, 7002u, &second_scan, &register_response) == 0);
    require(register_response.neighbor_count == 1u);
    require(register_response.neighbors[0].data_port == 7001u);

    memset(&third_scan, 0, sizeof(third_scan));
    third_scan.count = 1u;
    third_scan.files[0] = make_file("movie-copy.mp4", UINT64_C(0x1234), UINT64_C(4096));

    memset(&register_response, 0, sizeof(register_response));
    require(server_register_files("127.0.0.1", TEST_SERVER_PORT, 7003u, &third_scan, &register_response) == 0);
    require(register_response.neighbor_count == 2u);

    memset(&results, 0, sizeof(results));
    require(server_find_files("127.0.0.1", TEST_SERVER_PORT, "movie", &results) == 0);
    require(results.count == 2u);
    require(strcmp(results.items[0].name, "movie.mp4") == 0);
    require(results.items[0].hash == UINT64_C(0x1234));
    require(results.items[0].size_bytes == UINT64_C(4096));
    require(results.items[0].owner_port == 7001u);
    require(strcmp(results.items[1].name, "movie-copy.mp4") == 0);
    require(results.items[1].owner_port == 7003u);

    memset(&results, 0, sizeof(results));
    require(server_find_files("127.0.0.1", TEST_SERVER_PORT, "4096 4660", &results) == 0);
    require(results.count == 2u);
    require(results.items[0].owner_port == 7001u);
    require(results.items[1].owner_port == 7003u);

    memset(&results, 0, sizeof(results));
    require(server_find_files("127.0.0.1", TEST_SERVER_PORT, "S=4096 H=4660", &results) == 0);
    require(results.count == 2u);

    memset(&results, 0, sizeof(results));
    require(server_find_files("127.0.0.1", TEST_SERVER_PORT, "H=4660 S=4096", &results) == 0);
    require(results.count == 2u);

    expect_header_error(999u,
                        (uint16_t)P2P_PROTOCOL_VERSION,
                        0u,
                        P2P_ERROR_UNKNOWN_OPCODE);
    expect_header_error((uint16_t)P2P_MSG_FIND_REQ,
                        99u,
                        (uint32_t)sizeof(find_req_t),
                        P2P_ERROR_UNSUPPORTED_VERSION);
    expect_header_error((uint16_t)P2P_MSG_FIND_REQ,
                        (uint16_t)P2P_PROTOCOL_VERSION,
                        (uint32_t)sizeof(find_req_t),
                        P2P_ERROR_BAD_REQUEST);
    expect_empty_find_error();

    send_header_only((uint16_t)P2P_MSG_FIND_REQ, (uint16_t)P2P_PROTOCOL_VERSION, 0u);

    stop_server();

    puts("test_query_handler: ok");
    return 0;
}
