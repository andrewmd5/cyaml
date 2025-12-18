# Test Tools

## Suite Runner

```bash
cyaml_suite [suite_path] [filter] [--no-save] [--1.3]
```

Runs the YAML test suite against cyaml. Results are saved to `.cyaml_suite_results` for regression tracking.

- `suite_path`: Path to yaml-test-suite/data directory (default: `../refs/yaml-test-suite/data`)
- `filter`: Only run tests matching this substring
- `--no-save`: Skip saving results
- `--1.3`: Parse in YAML 1.3 mode

```bash
./cyaml_suite                           # Run all tests
./cyaml_suite --1.3                     # Run in 1.3 mode
./cyaml_suite TS54                      # Run only TS54 test
./cyaml_suite --1.3 --no-save TS54      # Debug specific 1.3 test
```

## Results Query

```bash
results_query [file] <command> [args]
```

Query saved test results. Default file is `.cyaml_suite_results`.

**Commands:**

| Command | Description |
|---------|-------------|
| (none) | Show summary |
| `list [filter]` | List tests (filter: pass, fail, skip, tree, emit, json, dump) |
| `show <test_id>` | Show details for a test (displays case N of M for multi-case tests) |
| `fails` | List all failing tests with details |
| `ids [filter]` | List just test IDs (filter: fail, tree, emit, json, dump) |
| `regressions` | List regressions from this run |
| `improvements` | List improvements from this run |
| `broken` | List all failures with regression dates |

```bash
./results_query                    # Show summary
./results_query list dump          # List dump failures
./results_query show 9MQT:1        # Show case 1 of multi-case test
./results_query fails              # Show all failures with details
./results_query regressions        # List regressions since last run
```

## Debug Tools

All debug tools output diagnostic YAML for analysis.

### parse_debug

```bash
parse_debug <suite_path> <test_id> [case_num] [--1.3]
```

Debug parse results. Shows input bytes, parse success/error with location, document structure, and event stream comparison.

```bash
./parse_debug ../refs/yaml-test-suite/data TS54
./parse_debug ../refs/yaml-test-suite/data JEF9 3      # Case 3 of multi-case test
```

### emit_debug

```bash
emit_debug <suite_path> <test_id> [case_num]
```

Debug emit output. Shows input bytes, expected vs actual emit output, and diff analysis.

### json_debug

```bash
json_debug <suite_path> <test_id> [case_num]
```

Debug JSON output. Shows input bytes, expected vs actual JSON output, and semantic comparison.

### dump_debug

```bash
dump_debug <suite_path> <test_id> [case_num] [--1.3]
```

Debug dump output. Shows input bytes, document structure, expected vs actual dump output, and byte-level diff.

## YAML 1.3 Mode

The `--1.3` flag enables YAML 1.3 parsing mode with stricter rules:

- Block scalars at document root with trailing whitespace on blank lines are errors
- Tests tagged `1.3-err` should produce parse errors
- Tests tagged `1.3-mod` should pass with potentially modified semantics
