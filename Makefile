CC ?= cc
UNAME_S := $(shell uname -s)

CPPFLAGS += -I. -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64
ifeq ($(UNAME_S),Darwin)
CPPFLAGS += -D_DARWIN_C_SOURCE
endif

CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -g
LDFLAGS ?=
LDLIBS += -pthread

BUILD_DIR := build
SERVER_BIN := $(BUILD_DIR)/server/p2p-server
CLIENT_BIN := $(BUILD_DIR)/client/p2p-client

COMMON_OBJS := \
	$(BUILD_DIR)/common/hash.o \
	$(BUILD_DIR)/common/net.o

SERVER_OBJS := \
	$(COMMON_OBJS) \
	$(BUILD_DIR)/server/main.o \
	$(BUILD_DIR)/server/registry.o \
	$(BUILD_DIR)/server/query_handler.o

CLIENT_OBJS := \
	$(COMMON_OBJS) \
	$(BUILD_DIR)/client/main.o \
	$(BUILD_DIR)/client/repl.o \
	$(BUILD_DIR)/client/server_api.o \
	$(BUILD_DIR)/client/scanner.o \
	$(BUILD_DIR)/transfer/sender.o \
	$(BUILD_DIR)/transfer/listener.o \
	$(BUILD_DIR)/transfer/receiver.o \
	$(BUILD_DIR)/search/neighbors.o \
	$(BUILD_DIR)/search/flood.o \
	$(BUILD_DIR)/search/aggregator.o

TEST_BINS := \
	$(BUILD_DIR)/tests/unit/test_hash \
	$(BUILD_DIR)/tests/unit/test_net \
	$(BUILD_DIR)/tests/unit/test_protocol_roundtrip \
	$(BUILD_DIR)/tests/unit/test_registry \
	$(BUILD_DIR)/tests/unit/test_query_handler \
	$(BUILD_DIR)/tests/unit/test_transfer_sender \
	$(BUILD_DIR)/tests/unit/test_transfer_receiver

INTEGRATION_TESTS := \
	tests/integration/test_central_server_client.sh \
	tests/integration/test_central_request_transfer.sh \
	tests/integration/test_identity_request_transfer.sh \
	tests/integration/test_distributed_search.sh \
	tests/integration/test_distributed_request_transfer.sh \
	tests/integration/test_request_identity_refresh.sh \
	tests/integration/test_plain_find_fallback.sh \
	tests/integration/test_hot_unplug_request.sh

.PHONY: all server client test unit-test integration-test docs clean

all: server client

server: $(SERVER_BIN)

client: $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(CLIENT_BIN): $(CLIENT_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/tests/unit/test_hash: $(BUILD_DIR)/tests/unit/test_hash.o $(BUILD_DIR)/common/hash.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/tests/unit/test_net: $(BUILD_DIR)/tests/unit/test_net.o $(BUILD_DIR)/common/net.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/tests/unit/test_protocol_roundtrip: $(BUILD_DIR)/tests/unit/test_protocol_roundtrip.o $(BUILD_DIR)/common/net.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/tests/unit/test_registry: $(BUILD_DIR)/tests/unit/test_registry.o $(BUILD_DIR)/server/registry.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/tests/unit/test_query_handler: \
	$(BUILD_DIR)/tests/unit/test_query_handler.o \
	$(BUILD_DIR)/client/server_api.o \
	$(BUILD_DIR)/common/net.o \
	$(BUILD_DIR)/server/query_handler.o \
	$(BUILD_DIR)/server/registry.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/tests/unit/test_transfer_sender: $(BUILD_DIR)/tests/unit/test_transfer_sender.o $(BUILD_DIR)/transfer/sender.o $(BUILD_DIR)/common/hash.o $(BUILD_DIR)/common/net.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/tests/unit/test_transfer_receiver: $(BUILD_DIR)/tests/unit/test_transfer_receiver.o $(BUILD_DIR)/transfer/receiver.o $(BUILD_DIR)/transfer/sender.o $(BUILD_DIR)/common/hash.o $(BUILD_DIR)/common/net.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/tests/get_local_ip: $(BUILD_DIR)/tests/get_local_ip.o $(BUILD_DIR)/common/net.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: unit-test integration-test

unit-test: $(TEST_BINS)
	@set -e; \
	for test_bin in $(TEST_BINS); do \
		echo "==> $$test_bin"; \
		$$test_bin; \
	done

integration-test: all $(BUILD_DIR)/tests/get_local_ip
	@set -e; \
	for test_script in $(INTEGRATION_TESTS); do \
		echo "==> $$test_script"; \
		sh $$test_script; \
	done

docs:
	@if command -v pdflatex >/dev/null 2>&1; then \
		cd docs && pdflatex -interaction=nonstopmode main.tex; \
	else \
		echo "pdflatex not found; install TeX Live/MacTeX to build docs."; \
	fi

clean:
	rm -rf $(BUILD_DIR) docs/*.aux docs/*.log docs/*.out docs/*.pdf
