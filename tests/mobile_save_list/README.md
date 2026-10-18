Run `python3 tests/mobile_save_list/run.py` with a C++20 compiler supporting ASan/UBSan.

The harness exercises production full-list capability and selection functions against a small model of the original save script event contract. It covers rows beyond ten, backwards selection, a new save slot, identity/revision/profile changes, deletion reindexing and restoring a target after the script's delayed initial-selection callback. It does not replace an Android integration test of the original game scripts.

The Android adapter publishes `ANDROID_SAVE_LIST` save rows with absolute index, opaque `rowId`, filename label and enabled `activate`/`delete` actions. A new-save row has `unavailableReason: "new"`. Requests use `verb@screenRevision:rowId`. Native confirmation and custom-save actions retain the original game flow while revalidating the target; no save files are deleted directly by the adapter. Only the already-current profile may be automatically selected.
