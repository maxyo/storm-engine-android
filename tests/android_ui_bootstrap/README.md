Run `python3 tests/android_ui_bootstrap/run.py` with a C++20 compiler and ASan/UBSan (`CXX` can override the compiler).

The runner extracts the production COMPILER Main/OnLoad hooks, optional-adapter loader, and source-reset method. Interpreter/table/filesystem boundaries are doubled. Tests cover post-Main and post-OnLoad ordering, a base-only save restoring after Release, repeated same/other saves, new game within the same program and after whole-program reset, static ClearEvents versus Release, optional entrypoint absence, failed helper compilation without retries, reset recovery, already-loaded adapters, and revocation of old V13 action/draw leases using the production reducer. Source assertions also verify the hooks are wired into Release/LoadState and not frame polling.

This test does not compile commercial game scripts or execute their real interpreter, render an Android frame, or prove device readiness. The application supplies `PROGRAM/interface/android_ui_bootstrap.c`; the engine loads it once after successful Main or restored OnLoad, and never saves it as a base game segment. Missing support leaves native controls available.

With `log.tag.StormInput=DEBUG`, bounded `land-adapter` transition diagnostics report bootstrap, readiness, source, fresh helper response, native owner flags, and selection. They neither relax predicates nor dispatch actions.
