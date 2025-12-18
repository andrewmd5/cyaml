# Fuzzing

## Setup

```bash
# macOS
brew install aflplusplus

# Linux
apt install afl++
```

## Build

```bash
make
```

## Run

```bash
make run-parse      # parser, emitter, JSON, events
make run-ypath      # path queries
make run-roundtrip  # parse -> emit -> parse
```

## Crash Analysis

```bash
make asan
./build/fuzz_parse_asan crash_file
```

## Parallel

```bash
afl-fuzz -i corpus/yaml -o build/findings/parse -M main -- ./build/fuzz_parse @@
afl-fuzz -i corpus/yaml -o build/findings/parse -S sec1 -- ./build/fuzz_parse @@
```

## Corpus

Seeds in `corpus/{yaml,ypath}/`. Add yaml-test-suite samples:

```bash
cp ../refs/yaml-test-suite/data/*/*.yaml corpus/yaml/
```
