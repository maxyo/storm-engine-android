# script_check

Host tool that compiles a game's script tree without starting the engine and reports every
compile error with file and line. It links only `core` (no renderer, no engine modules).

```sh
cmake --build build/host --target script_check
cd <game dir>                          # e.g. the TEHO or Caribbean Legend directory
<build>/script_check check . [--dump <dir>] [--warnings] [--language <name>]
<build>/script_check test [--cache] tests/script_language/cases/*.c
```

`check` compiles the main segment (`seadogs.c`) and then every `.c` file that no script includes,
each as its own segment that is unloaded again, as `LoadSegment` does. A segment that fails on names
another segment defines is compiled again with that segment loaded, since the game loads such
segments together. The compiler resumes after an error, so one run lists everything.
- Natives the scripts declare but no engine library implements are listed as `MISSING NATIVE`
  lines and in a summary line. They are not errors here, because the tool links no library.
- The game profile and the `%language%` of include paths are resolved as the engine resolves them.
- `--dump` writes each segment's bytecode (`.b`), plus its attribute and function names (`.aw`) in
  instruction order. Use it to show that a compiler change leaves a game's bytecode unchanged; the
  codes depend on compilation order, so compare through the names.

`test` runs each script's `int test_*()` functions; each must return 1 without a runtime error.
A script whose first line is `// expect-error: <text>` must instead fail to compile with that
error. With `--cache`, a first compiler stores each script in a fresh script cache, and the tests
run on a second compiler that loads it from there.

`tests/script_language/run.py` runs the script language cases both ways.
