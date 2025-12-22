CC = afl-clang-fast
CFLAGS = -O2 -g -I../src
BUILD = build

all: $(BUILD)/fuzz_parse $(BUILD)/fuzz_emit $(BUILD)/fuzz_dump $(BUILD)/fuzz_json $(BUILD)/fuzz_json_parse $(BUILD)/fuzz_ypath

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/out:
	mkdir -p $(BUILD)/out

$(BUILD)/fuzz_parse: fuzz_parse.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/fuzz_emit: fuzz_emit.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/fuzz_dump: fuzz_dump.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/fuzz_json: fuzz_json.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/fuzz_json_parse: fuzz_json_parse.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/fuzz_ypath: fuzz_ypath.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

corpus:
	mkdir -p corpus/yaml corpus/json corpus/ypath

run-parse: $(BUILD)/fuzz_parse corpus | $(BUILD)/out
	afl-fuzz -i corpus/yaml -o $(BUILD)/out/parse -- $(BUILD)/fuzz_parse

run-emit: $(BUILD)/fuzz_emit corpus | $(BUILD)/out
	afl-fuzz -i corpus/yaml -o $(BUILD)/out/emit -- $(BUILD)/fuzz_emit

run-dump: $(BUILD)/fuzz_dump corpus | $(BUILD)/out
	afl-fuzz -i corpus/yaml -o $(BUILD)/out/dump -- $(BUILD)/fuzz_dump

run-json: $(BUILD)/fuzz_json corpus | $(BUILD)/out
	afl-fuzz -i corpus/yaml -o $(BUILD)/out/json -- $(BUILD)/fuzz_json

run-json-parse: $(BUILD)/fuzz_json_parse corpus | $(BUILD)/out
	afl-fuzz -i corpus/json -o $(BUILD)/out/json_parse -- $(BUILD)/fuzz_json_parse

run-ypath: $(BUILD)/fuzz_ypath corpus | $(BUILD)/out
	afl-fuzz -i corpus/ypath -o $(BUILD)/out/ypath -- $(BUILD)/fuzz_ypath

clean:
	rm -rf $(BUILD)
