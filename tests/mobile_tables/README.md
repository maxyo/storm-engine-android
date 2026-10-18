Run `python3 tests/mobile_tables/run.py`. The sanitizer harness compiles the
production CXI_TABLE collection and apply methods with real ATTRIBUTES. Only
engine service, renderer and event queue boundaries are stubs.

Store, storage, contraband and cannons use TABLE_LIST.index plus
SHIPS_SCROLL.current/picN.companionIndex. Item trade and item boxes use the
corresponding CHARACTERS_SCROLL.current/picN.character. The resulting opaque
row token is carried with the screen revision in select/activate/dispatch.
A missing or malformed item/owner disables the exported action. These commerce
tables reject unversioned selection. Other tables retain their existing behavior.

Tests cover reordered goods, changed owners, missing selector entries, locks,
old epochs, deferred activation after selection, invalid indices, native column
headers and preservation of the existing non-commerce row contract. These are
contract tests, not evidence of an actual purchase or device rendering.
