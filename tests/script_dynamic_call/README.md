# Script dynamic call status

```sh
python3 tools/ci/run_host_tests.py --only script_dynamic_call
```

After `call name()` (TEHO uses `call sFunction(qname)` in `quests_reaction.c`), the compiler
stores 1 or 0 in its expression register. The callee may have left a reference there
(`ref sld = CharacterFromID("Blaze")`), and `DATA::Set` writes through references, so storing
the status used to overwrite the referenced character (storm-devs/storm-engine#511).

The runner checks that the `CALL` branch of `BC_Execute` stores the status through
`COMPILER::StoreDynamicCallStatus`, extracts that method verbatim from `compiler.cpp` and runs
it under ASan/UBSan with the real `DATA` and `ATTRIBUTES`: a reference and an attribute
reference left in the register must leave the object, its type and its attributes intact.
