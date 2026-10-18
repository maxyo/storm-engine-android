Run `python3 tests/android_newgame/run.py` to check transient default-off selection,
revision fencing, cancellation, unsupported scripts, wrong hero/intro, and one-shot
post-init redirection under ASan/UBSan. The harness exercises production bridge code
with a minimal script-variable fake; it does not validate quest scripts or gameplay.

Android dispatch occurs after the frame script returns and before the following
posted-event pass. The original game's delayed quest scheduler calls the app-owned
initializer. No loaded-save state or persistent Android setting requests a skip.

A release additionally requires the actual supported game scripts to compile and
fresh-new-game runtime checks for normal and skipped starts, ship/crew/officer,
main-story progression, map travel, and save/reload. Native tests cannot replace them.
