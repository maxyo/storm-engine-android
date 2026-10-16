# GLM math migration: first compatibility layer

This branch replaces working arithmetic behind the engine's existing `CVECTOR`, `DVECTOR`, and central `CMatrix` operations, plus the separate packed `Vector`/`Matrix`/`Quaternion` arithmetic, with GLM. It does not rename serialized types or claim to finish every custom math implementation.

## Inventory at d5cbf60

| Family | Existing representation / use | This change |
| --- | --- | --- |
| `CVECTOR` | Three floats, 12 bytes, native alignment 4 (or the caller's legacy pack alignment); public x/y/z and v[3]. Word appears in 279 source/header files. Used in vertex buffers, raw animation reads and save files. | Component arithmetic, dot/cross, length and normalization delegate to GLM, retaining legacy precision and zero-vector behavior. Storage is unchanged. |
| `DVECTOR` | Three doubles, 24 bytes; four source/header files. | Arithmetic, dot/cross and normalization delegate to GLM. Zero normalization still produces NaN as before. |
| `CMatrix` | 16 floats, 64 bytes, alignment 16, overlaid axis/position fields. Word appears in 131 files. | Identity, scalar/matrix multiplication, point and inverse-basis transforms, 3x3/4x4 transpose and strided screen projection use GLM. Existing constructors and callers exercise these operations immediately. |
| `math3d::Vector` / `Matrix` / `Quaternion` (global class names) | Separate older family under pack(1); 37 / 30 / 10 files contain the respective words. Different normalization and rich geometry APIs. | Vector arithmetic/normalization/length/reflection/interpolation; matrix products, transforms, transpose and projection; quaternion arithmetic, matrix output, normalization/conjugation and interpolation blending now use GLM temporaries. Packed storage and legacy branch policies remain. |
| `CVECTOR4`, `Vector4`, `types3d.h` PODs | Legacy interop/vertex declarations. | Unchanged. |
| `storm_d3dx9.cpp`, sea SIMD helpers, scalar angle/rounding helpers | D3DX compatibility and specialized kernels. | Unchanged. Existing SSE/scalar portability regressions run with the new dependency. |

Counts are textual inventory, not a complete call graph. Relevant binary boundaries include `core/include/save_load.h::SaveVector` (raw 12-byte write), animation_service_imp.cpp (raw `sizeof(CVECTOR)` reads), weather/stars.cpp (vertex stride and cache I/O), and CMatrix's D3DMATRIX pointer conversion.

## Conventions that must remain explicit

Storm's `m[column][row]` storage has the same flat element ordering as a GLM matrix, while D3D interprets those bytes using row vectors. Therefore **legacy `A * B` means `glm(B) * glm(A)`**: first apply A, then B. This is tested with noncommuting scale and translation and 3,000 arbitrary full 4x4 matrix pairs.

Point transformation uses homogeneous w=1. `MulToInv` and `MulToInvNorm` remain inverse-orthonormal-basis operations; they are not silently changed into a general inverse. `Transposition()` remains the legacy rigid inverse, distinct from `Transposition4x4()`.

View space looks along positive Z. Projection maps near/far depth to 0/1, flips screen Y downward and retains reciprocal W. The historical projection builder scales vertical FOV linearly by height/width; replacing it with a generic `glm::perspective` would change framing. Euler builders, view degeneracy handling, mirror construction and that projection builder remain legacy code intentionally for this stage.

GLM values are temporaries loaded/stored component-wise; no GLM object is reinterpreted as CVECTOR or installed into a packed union. Explicit `packed_highp` template qualifiers avoid accidental layout changes from `GLM_FORCE_DEFAULT_ALIGNED_GENTYPES`. Compile-time layout assertions, literal saved-vector byte fixtures, D3D row-vector checks and strided projection guards test the compatibility boundary. This preserves formats; it does not promise bit-identical results for every floating-point computation. Observed maximum scaled numerical difference from the original implementation was 3.22e-06 across 228,000 samples.

## Dependency

GLM 1.0.3 is pinned in `cmake/glm.cmake` to upstream commit `8d1fd52e5ab5590e2c81768ace50c72bae28f2ed`, archive SHA256 `36eefbae41503e6822e8d9e1b4cc85c6a6621f17676f6258aaf0a64c2de0a09c`. CMake downloads only into the chosen build directory and the `math` target exports `glm::glm-header-only` transitively. The backend checks the expected version. For offline configuration, supply `-DFETCHCONTENT_SOURCE_DIR_STORM_GLM=/absolute/extracted/source` containing that revision.

[Upstream GLM](https://github.com/g-truc/glm/tree/8d1fd52e5ab5590e2c81768ace50c72bae28f2ed) provides the dependency; the MIT alternative is selected and copied into `third_party/licenses/GLM-MIT.txt`. Include this notice in downstream binary distribution notices. No third-party source checkout, game resource, APK, device state or shared dependency cache was committed or modified.

## Validation performed

- `python3 tests/glm_math/run.py --build-dir build/glm-differential --glm-source <pinned-source> --baseline d5cbf60`: PASS. The same public-API harness is compiled against this branch and the original headers extracted from the Git revision. Both must pass fixed mathematical/ABI contracts, then 228,000 scalar samples are compared with a 5e-5 scaled bound. Maximum measured difference: 3.22e-06.
- `python3 tests/glm_math/run.py --build-dir build/glm-sanitized --sanitize --aligned-defaults --baseline d5cbf60`: PASS with ASan/UBSan and forced aligned/intrinsics GLM defaults. This configuration also exercised the actual pinned URL/hash download without the offline override.
- `python3 tests/portability/run.py --glm-source <pinned-source>`: both existing SSE and forced-scalar suites PASS, including their rounding-mode checks. The standalone Android portability target also links the pinned header dependency now.
- NDK r30/API24 ARM64 compilation of `tests/glm_math/check.cpp` against the actual dxvk-native D3D9 headers: PASS; output verified as AArch64 relocatable ELF. This verifies target layout assertions and SDK header compatibility, not execution on Android.
- `tests/android_runtime/compile_engine.py` with the existing read-only NDK/SDL/spdlog/fmt/mimalloc/dxvk source inputs plus new `--glm-source`: real ARM64 `main.cpp` and `file_service.cpp` objects PASS. Objects were produced in an isolated temporary directory. Existing unrelated warnings remain.
- `git diff --check`: PASS.

The host math harness uses a minimal documented D3DMATRIX shape when no SDK is present; the separate ARM64 compilation uses the real SDK headers. No full-engine link, gameplay run, render comparison, save/load gameplay round trip or performance improvement is claimed for this branch.

## Remaining migration and acceptance work

Custom Euler/view/projection/mirror builders, scaled matrix inverse, matrix-to-quaternion branch selection, vector angle/clamp/random helpers, and D3DX/math kernels remain. Migrate those as separately bounded changes, retaining their distinct edge-case policies, formats and calling contracts; do not bulk-replace names with GLM aliases. Before promoting this layer to a released build, perform the full native build and representative render, animation, camera, physics and save/load checks on the target device. Tiny numerical changes may accumulate in simulation even though bytes and conventions are preserved. No gameplay resources are needed for the committed tests.

The GLM boundary resets packing to the platform default for its headers and
calculation types, then restores the caller's packing. CVECTOR/DVECTOR retain
inherited packing, as required by geometry's first include under pack(1).
`packed_vector_include.cpp` separately verifies that first-include case, native
GLM temporary alignment, restored caller packing, and odd-address operations.
Matrix load/store bridges use memcpy so packed serialized buffers are valid.

Packing correction validation: GCC ASan/UBSan with forced aligned GLM defaults
passed the new first-include test and all 228,000 differential samples against
d5cbf60 (maximum scaled error 3.22e-06). Host Clang passed the packing test
without sanitizers (this host's Clang sanitizer runtime is unavailable). NDK r30
compiled the real geometry/collide.cpp and geometry/geom_static.cpp objects,
plus the packing regression, for ARM64. This is compile coverage, not a complete
native link or device execution.

## Packed arithmetic stage

The second stage keeps Vector/Matrix/Quaternion sizes at 12/64/16 bytes and
alignment at 1. GLM values are copied by components; packed matrix buffers pass
through the memcpy boundary. Vector double-precision normalization, double
reciprocal division, zero/NaN policies, and scalar-left subtraction behavior are
preserved. Packed Matrix composition retains reversed GLM operand order. Its
`Transposition()` is a full transpose (unlike CMatrix's rigid inverse method).
Special matrix builders and scaled inverse remain unchanged.

Legacy quaternion multiplication is not Hamilton multiplication: its historical
`(x-w)*(q.y-q.z)` intermediate changes the resulting y component. For example,
identity times (0,0,1,0) produces (0,-2,1,0). The GLM Hamilton product therefore
receives the algebraically equivalent y correction `2*(left.x-left.w)*right.z`.
This deliberately preserves the existing behavior; fixing it requires a separate
compatibility decision. Inverse remains normalized conjugate, not norm-squared
inverse. SLerp keeps its 0.9 threshold, first-endpoint sign flip and scalar
weight calculation; GLM performs the weighted blend. Matrix-to-quaternion
conversion keeps the old trace/diagonal branches. Quaternion GLM conversion uses
named components, independent of GLM's quaternion storage-order macros.

`packed_check.cpp` first includes math3d through plane.h's pack(1), checks literal
vector/quaternion byte fixtures and matrix offsets, and runs odd-address math
with sentinel bytes, strided projection, orientation, noncommuting composition,
normalization edge cases, quaternion quirks and SLerp threshold cases. The same
public-API harness is compiled against archived d5cbf60 headers for differential
comparison. Tests contain no game assets.

Validated second-stage commands/results:

- `python3 tests/glm_math/run.py --build-dir build/glm-packed-default --glm-source build/glm-download/glm-8d1fd52e5ab5590e2c81768ace50c72bae28f2ed --baseline d5cbf60`: PASS.
- The same command with `--build-dir build/glm-packed --sanitize --aligned-defaults`: PASS GCC ASan/UBSan, including genuine odd-address objects. Both configurations compare 810,112 new scalar samples (maximum scaled error 3.53e-05, threshold 5e-05) and the original 228,000 samples (3.22e-06).
- Host Clang `-O2 -ffp-contract=off`, forced aligned/intrinsics defaults and `GLM_FORCE_QUAT_DATA_WXYZ`: contracts and 810,112 samples versus the same Clang-built legacy harness PASS (maximum scaled error 1.78e-05). Clang sanitizer runtimes are unavailable on this host; sanitizer evidence is GCC.
- NDK r30 ARM64 compilation of packed_check.cpp using actual DXVK D3D headers: PASS. No full engine link, device execution or visual/gameplay equivalence claim is made for this stage.

Floating-point evaluation order can change, particularly around cancellation in
screen coordinates. Numerical tolerances are explicit; this stage does not
promise bit-identical simulation output. The random harness compares builds
using the same compiler; constructor argument evaluation can consume the random
sequence in a different order between compilers.
