Run `python3 tests/mobile_inventory/run.py` from the engine repository. The
sanitizer harness exercises the production inventory identity and publication
capability code with the engine's real ATTRIBUTES implementation. It writes
only to a temporary directory. The platform shim supplies the fixed-buffer
formatting overload required by attributes.cpp; no UI or attribute behavior is
mocked.

Inventory rows without UserData.ID use an opaque item/character/role identity.
EQUIP_BUTTON publishes that selected rowId and a press action only with an
enabled selected row. Clients send `press@<screenRevision>:<rowId>` after that
publication. Selection/filter commands invalidate the capability and an equip
consumes it, so clients must await another native publication before pressing
again. Dispatch also validates current selection, character, and node locks.
Other forms retain their existing identities and commands. Plain press remains
available to old apps; new inventory clients must use guarded press.

The same runner compiles the production map-close collection method and checks hidden, disabled, command and screen boundaries with renderer-independent node stubs. Runtime rendering, touch dispatch and full map readability still need Android verification.
