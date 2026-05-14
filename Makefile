APP := dnslab

SRC_DIR := src
BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj
DIST_DIR := dist

CC ?= cc

CPPFLAGS ?=
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -g
LDFLAGS ?=
LDLIBS ?=

SRCS := $(SRC_DIR)/main.c
OBJS := $(OBJ_DIR)/main.o
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

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BIN_DIR) $(OBJ_DIR) $(DIST_DIR):
	mkdir -p $@

run: build
	$(BIN)

test: build
	@output="$$( $(BIN) )"; \
	expected="hello from dnslab"; \
	if [ "$$output" = "$$expected" ]; then \
		printf "ok: output matches: %s\n" "$$output"; \
	else \
		printf "fail: expected '%s', got '%s'\n" "$$expected" "$$output"; \
		exit 1; \
	fi

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
