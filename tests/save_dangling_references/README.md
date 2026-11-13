# Saving script references to freed memory

`aref` and `ref` variables are plain pointers; the script language does not clear them when
their target is deleted (`DeleteAttribute`, a rebuilt branch, a resized array, a reloaded
segment). A save writes every global variable, so it meets such pointers even when no script
would use them again. The owner's crashes (Xiaomi 15 Ultra, build 900ecfa5): `SIGSEGV` in
`COMPILER::SaveVariable`, in the inlined `TraceARoot` walk from the freed target of the global
`aref reload_locator_ref` (reload.c: the last reload locator used) up through its parents.

The check builds a variable table with live, deleted, local and chained references, deletes
the `reload` branch as a script does, and writes the variables with the production
`COMPILER::SaveVariable` under AddressSanitizer: nothing freed is read, each unsaveable
reference is reported by name and written empty, and the save loads with every live reference
intact (`save_references.hpp`).

`--baseline <checkout>` builds the same scenario against a tree from before the fix and
requires AddressSanitizer to stop it with a heap-use-after-free in `TraceARoot`:

```sh
git worktree add /tmp/base 900ecfa5
python3 tests/save_dangling_references/run.py --dependency-sources <sources> --glm-source <glm> --baseline /tmp/base
```
