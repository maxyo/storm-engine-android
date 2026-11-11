# Rendering buffer regression checks

Run `python3 tests/render_buffer_state/run.py` with a C++17 compiler supporting ASan/UBSan.
The checks compile the actual production sea-drop drawing block and the complete `DX9RENDER::DrawRects` method with small renderer stubs. They verify balanced buffer locks, batch bounds, expiry, failed/repeated locks, successful locks with null data, and restoration of camera transforms. These CPU checks do not replace Android/D3D/Vulkan gameplay testing.

`buffer_handles.py` compiles `DX9RENDER`'s buffer handle block (`LiveBuffer` … `ReleaseIndexBuffer`) and checks that the game's "no buffer" handle -1 and out-of-range handles never index the handle table (UBSan bounds), with a member in front of the table that ends in 1.0f like libc++'s `unordered_map` (the Android crash at 0x3f800000).
