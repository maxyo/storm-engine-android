Run `python3 tests/mobile_shipyard/run.py` with a C++17 compiler. The harness includes the production collector and action dispatcher, using bounded attribute/script/node service doubles. ASAN/UBSAN check owner versus offer identity, empty and no-ship slots, frame readiness, native click/right-click/activate routing, window modes and action invalidation. It also checks that native table focus is established before the selection callback.

This is source-contract verification. It does not simulate game pricing, repair or purchase effects, and is not device/UI acceptance evidence.
