CC ?= cc
UNAME_S := $(shell uname -s)

CPPFLAGS += -I. -D_POSIX_C_SOURCE=200809L
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
	$(BUILD_DIR)/transfer/receiver.o \
	$(BUILD_DIR)/search/neighbors.o \
	$(BUILD_DIR)/search/flood.o \
	$(BUILD_DIR)/search/aggregator.o

TEST_BINS := \
	$(BUILD_DIR)/tests/unit/test_hash \
	$(BUILD_DIR)/tests/unit/test_net \
	$(BUILD_DIR)/tests/unit/test_protocol_roundtrip

.PHONY: all server client test docs clean

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

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TEST_BINS)
	@set -e; \
	for test_bin in $(TEST_BINS); do \
		echo "==> $$test_bin"; \
		$$test_bin; \
	done

docs:
	@if command -v pdflatex >/dev/null 2>&1; then \
		cd docs && pdflatex -interaction=nonstopmode main.tex; \
	else \
		echo "pdflatex not found; install TeX Live/MacTeX to build docs."; \
	fi

clean:
	rm -rf $(BUILD_DIR) docs/*.aux docs/*.log docs/*.out docs/*.pdf
