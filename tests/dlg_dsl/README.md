# Caribbean Legend template language

```sh
python3 tests/dlg_dsl/run.py                        # unit checks, ASan/UBSan
python3 tests/dlg_dsl/run.py --corpus <CL game dir> [--language english]
python3 tools/ci/run_host_tests.py --only dlg_dsl
```

`src/libs/core/src/dlg_dsl.{hpp,cpp}` expands the templates in Caribbean Legend text values
(`$name`, `$key(...)`, `$rand(a|b)`, `\` escapes, the `↴` lower-case marker) for the natives
DLGO, DLG_Convert, DLG_ConvertE, StringFromKey and DLG_ReplaceAllMatches, and parses the
`key {value}` text files. It has no engine dependency: the script VM implements `storm::dlg::Host`.

`check.cpp` drives every built-in, the script-function fallback (`name`, `DLG_Name`, `RT_Name`,
`Name`), the reporting of unknown or broken constructs (they stay visible in the output), the
text-file parser and its cache through a fake host with invented strings.

`--corpus` builds an optimised binary and expands every value of every text file of one
language through a fake host that knows the game's script function names (extracted from
`Program/` into a temporary file). It prints parse problems, expansion issues per name with the
first location, and timings. Game text is read only from the given directory; nothing is copied
into the repository.
