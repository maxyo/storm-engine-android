Run `python3 tests/android_land_refresh/run.py` with a C++20 compiler and ASan/UBSan (`CXX` override supported).

The test extracts the production `DATA::Convert` implementation and `BIManCommandList::RefreshAndroidSource`, with small attribute/interpreter boundaries. It reproduces the failing SendMessage VAR_PTR→VAR_STRING conversion and verifies the stock VAR_PTR→VAR_INTEGER→VAR_STRING path, including the -1 sentinel. It exercises missing/partial/malformed metadata, stale fields from a previous refresh, nonfatal interpreter errors both before and after a completion marker, recovery, original Forbidden versus Missing semantics, and rejection of old draw/action leases after failed refresh.

The native admission reads the monotonically increasing script error serial before/after the synchronous event. A marker and all fields are still rejected if any logged interpreter error occurred. This host test is not actual game-script compilation or device validation.
