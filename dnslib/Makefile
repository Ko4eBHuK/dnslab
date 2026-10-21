APP := dnslab

SRC_DIR := src
BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj
DIST_DIR := dist

CC ?= cc

# -I$(SRC_DIR): lets every .c include headers from the src/ root,
# e.g. #include "dns.h" or #include "command/command.h" regardless of
# which subdirectory the source file itself lives in.
CPPFLAGS ?= -I$(SRC_DIR)
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -g
LDFLAGS ?=
LDLIBS ?=

# Application sources (excluding the test binary).
# Grouped by role so it is obvious where to add a new command.
MAIN_SRC := $(SRC_DIR)/main.c
CLI_SRC  := $(SRC_DIR)/cli.c
LIB_SRC  := $(SRC_DIR)/engine/dns.c \
            $(SRC_DIR)/decor/print.c \
            $(SRC_DIR)/net/net.c
CMD_SRC  := $(SRC_DIR)/command/compose.c \
            $(SRC_DIR)/command/lookup.c \
            $(SRC_DIR)/command/details.c \
            $(SRC_DIR)/command/resolv.c

SRCS := $(MAIN_SRC) $(CLI_SRC) $(LIB_SRC) $(CMD_SRC)
# % matches across '/', so src/command/foo.c -> build/obj/command/foo.o
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
BIN := $(BIN_DIR)/$(APP)

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
OS := macos
else ifeq ($(UNAME_S),Linux)
OS := linux
else
OS := unknown
endif

ifeq ($(UNAME_M),arm64)
ARCH := arm64
else ifeq ($(UNAME_M),aarch64)
ARCH := arm64
else ifeq ($(UNAME_M),x86_64)
ARCH := x86_64
else
ARCH := $(UNAME_M)
endif

PACKAGE := $(DIST_DIR)/$(APP)-$(OS)-$(ARCH).tar.gz

.PHONY: all build run test package clean print-config

all: build

build: $(BIN)

$(BIN): $(OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Create the matching obj subdirectory (e.g. build/obj/command) on demand.
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BIN_DIR) $(OBJ_DIR) $(DIST_DIR):
	mkdir -p $@

run: build
	$(BIN)

TEST_BIN := $(BIN_DIR)/test_dns
TEST_OBJS := $(OBJ_DIR)/test/test_dns.o $(OBJ_DIR)/engine/dns.o

$(TEST_BIN): $(TEST_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(TEST_BIN)
	$(TEST_BIN)

package: build | $(DIST_DIR)
	tar -czf $(PACKAGE) -C $(BIN_DIR) $(APP)
	@printf "created %s\n" "$(PACKAGE)"

print-config:
	@printf "APP=%s\n" "$(APP)"
	@printf "CC=%s\n" "$(CC)"
	@printf "CFLAGS=%s\n" "$(CFLAGS)"
	@printf "OS=%s\n" "$(OS)"
	@printf "ARCH=%s\n" "$(ARCH)"
	@printf "BIN=%s\n" "$(BIN)"
	@printf "PACKAGE=%s\n" "$(PACKAGE)"

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
