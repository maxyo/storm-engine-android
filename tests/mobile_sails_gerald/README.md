Run `python3 tests/mobile_sails_gerald/run.py`. Compiles `mobile_sails_gerald.hpp` (the export of the
sail emblem screen, sailsgerald: emblem rows with their texture paths, the guarded `select`, the
price / money / applied marker) with sanitizers. The glue in `nodes/xi_scroll_image.cpp` and
`mobile_sails_gerald.inc` reads the script attributes and is Android-only.
