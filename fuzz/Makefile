# AFL Fuzzing Makefile for cyaml
#
# Usage:
#   make                  # Build all fuzzers with afl-clang-fast
#   make CC=afl-gcc       # Build with afl-gcc instead
#   make run-parse        # Run parser fuzzer
#   make run-ypath        # Run ypath fuzzer
#   make run-roundtrip    # Run roundtrip fuzzer
#   make clean            # Clean build artifacts

SRCDIR := ../src
BUILDDIR := build

# Default to afl-clang-fast (faster), fall back to afl-gcc
AFL_CC ?= $(shell command -v afl-clang-fast 2>/dev/null || echo afl-gcc)
CC ?= $(AFL_CC)

CFLAGS := -O2 -g -Wall -Wextra -I$(SRCDIR)
LDFLAGS := -lm

# Source files for cyaml library
CYAML_SRCS := \
	$(SRCDIR)/cyaml.c \
	$(SRCDIR)/cyaml_utf8.c \
	$(SRCDIR)/cyaml_parser.c \
	$(SRCDIR)/cyaml_emitter.c \
	$(SRCDIR)/cyaml_events.c \
	$(SRCDIR)/cyaml_json.c \
	$(SRCDIR)/cyaml_path.c \
	$(SRCDIR)/cyaml_modify.c

# Fuzzer targets
FUZZERS := $(BUILDDIR)/fuzz_parse $(BUILDDIR)/fuzz_ypath $(BUILDDIR)/fuzz_roundtrip

.PHONY: all clean run-parse run-ypath run-roundtrip corpus

all: $(BUILDDIR) $(FUZZERS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Build fuzzers
$(BUILDDIR)/fuzz_parse: fuzz_parse.c $(CYAML_SRCS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(CYAML_SRCS) $(LDFLAGS)

$(BUILDDIR)/fuzz_ypath: fuzz_ypath.c $(CYAML_SRCS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(CYAML_SRCS) $(LDFLAGS)

$(BUILDDIR)/fuzz_roundtrip: fuzz_roundtrip.c $(CYAML_SRCS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(CYAML_SRCS) $(LDFLAGS)

# Create corpus directories
corpus:
	mkdir -p corpus/yaml corpus/ypath $(BUILDDIR)/findings

# Run fuzzers (requires corpus to exist)
run-parse: $(BUILDDIR)/fuzz_parse corpus
	@echo "Starting parser fuzzer..."
	@echo "Press Ctrl-C to stop"
	afl-fuzz -i corpus/yaml -o $(BUILDDIR)/findings/parse -m none -- $(BUILDDIR)/fuzz_parse @@

run-ypath: $(BUILDDIR)/fuzz_ypath corpus
	@echo "Starting ypath fuzzer..."
	@echo "Press Ctrl-C to stop"
	afl-fuzz -i corpus/ypath -o $(BUILDDIR)/findings/ypath -m none -- $(BUILDDIR)/fuzz_ypath @@

run-roundtrip: $(BUILDDIR)/fuzz_roundtrip corpus
	@echo "Starting roundtrip fuzzer..."
	@echo "Press Ctrl-C to stop"
	afl-fuzz -i corpus/yaml -o $(BUILDDIR)/findings/roundtrip -m none -- $(BUILDDIR)/fuzz_roundtrip @@

# Build with sanitizers for crash analysis
ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

$(BUILDDIR)/fuzz_parse_asan: fuzz_parse.c $(CYAML_SRCS) | $(BUILDDIR)
	clang $(CFLAGS) $(ASAN_FLAGS) -o $@ $< $(CYAML_SRCS) $(LDFLAGS)

$(BUILDDIR)/fuzz_ypath_asan: fuzz_ypath.c $(CYAML_SRCS) | $(BUILDDIR)
	clang $(CFLAGS) $(ASAN_FLAGS) -o $@ $< $(CYAML_SRCS) $(LDFLAGS)

$(BUILDDIR)/fuzz_roundtrip_asan: fuzz_roundtrip.c $(CYAML_SRCS) | $(BUILDDIR)
	clang $(CFLAGS) $(ASAN_FLAGS) -o $@ $< $(CYAML_SRCS) $(LDFLAGS)

asan: $(BUILDDIR)/fuzz_parse_asan $(BUILDDIR)/fuzz_ypath_asan $(BUILDDIR)/fuzz_roundtrip_asan

clean:
	rm -rf $(BUILDDIR)
