# bgfx migration: first world pass

This branch does **not** replace the complete game renderer yet. The stable game
continues to use DX9/DXVK, including its existing Android graphics preferences.
`STORM_ENABLE_BGFX` is OFF by default and does not fetch dependencies when OFF.
No extra APK library or game resources are shipped by this change.

The implemented migration slice is the `skyfog` world pass from
`src/techniques/weather/Sky.fx`, rather than an unrelated triangle demo:

* `weather/include/fog_sphere_mesh.hpp` generates the same 513 vertices and 3048
  indices used by `SKY::UpdateFogSphere`. Legacy weather still obtains each ARGB
  vertex color through `WEATHER_CALC_FOG_COLOR` and uploads this shared geometry.
* `storm_bgfx_world` owns a real bgfx shader program, dynamic vertex buffer and
  index buffer. `FogPass::update` accepts that same script-colored mesh; `submit`
  draws it with the supplied world matrix and configured view.
* The shader replaces this pass's fixed-function diffuse color and alpha test.
  ARGB is explicitly converted to RGBA bytes, independent of host endianness.
  Depth test/write and culling are disabled; source-alpha blending is explicit.
  View/projection and the sky camera-relative translation remain the owner's
  responsibility. An explicit `enabled` parameter disables submission.
* The old fog buffer allocation used the larger sky vertex stride. It now uses
  the actual 16-byte fog vertex size; drawing stride and vertex data are unchanged.

The bgfx pass currently runs in a headless Vulkan validation host. It is not yet
connected to the live game's swapchain. Calling it inside the existing DXVK
frame would not magically share either its Vulkan device or render targets.
That integration must not be represented as complete, and the graphics screen
must not offer a complete “bgfx game renderer” until the remaining passes work.

## Pinned dependency and shaders

Upstream [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) commit
`8d950d6dc3df1d763e7d3655987c89009767a07e` pins these git submodules:

| Dependency | Commit | Upstream license |
| --- | --- | --- |
| bgfx | `fd541f05865f6e7fbfc302f81f5a71d2b05794eb` | BSD-2-Clause |
| bx | `d4e6ed3cf4f75c355bb04088bc1c13cd919aafa6` | BSD-2-Clause |
| bimg | `8e665781eff8e963a04924f1acc1d74324149bcb` | BSD-2-Clause |

bgfx.cmake is CC0; bundled third-party shader/image compiler code retains its
own licenses in the fetched source tree. Any future distributed binary must
include those notices. Sources are fetched into the selected build directory;
no moving branch, system package installation, or shared dependency cache is
required by the CMake target. FetchContent's explicit source override is useful
offline; the caller is responsible for using the pinned checkout in that case.

The fog shader sources are ours and are compiled by the pinned host `shaderc`
to bgfx SPIR-V containers. The old `.vso`/`.pso` files are D3D9 bytecode and are
**not** compatible bgfx programs. Nothing silently treats them as SPIR-V.
Host shader tools must be built for the host, not executed from an ARM64 Android
cross-build. `FogPass` accepts the compiled shader memory from its caller.

## Reproduce the standalone checks

Host prerequisites: CMake >=3.21, Ninja, a C++20 toolchain, X11/OpenGL development
headers/libraries required by upstream bgfx.cmake, and a Vulkan ICD. The pixel
test needs a genuine Vulkan implementation with texture readback; Noop fallback
is explicitly disabled. A software Vulkan ICD is suitable for correctness.

```sh
cmake -S src/libs/renderer_bgfx -B build/bgfx-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/bgfx-host --target storm_bgfx_fog_check -j4
ctest --test-dir build/bgfx-host --output-on-failure
```

The test renders the real fog sphere into an RGBA8 target and reads pixels back.
It checks non-symmetric RGB channels (to catch ARGB/RGBA swaps), 50% alpha blending,
zero-alpha rejection, disabling the pass, and updating to an opaque night color.
It checks topology bounds and script-color callback counts as well. These are
rendering correctness checks, not game scene or performance benchmarks.

Android compile/link check (no device/APK modification):

```sh
cmake -S src/libs/renderer_bgfx -B build/bgfx-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/android-arm64.cmake" \
  -DSTORM_ANDROID_NDK=/absolute/path/to/android-ndk-r30 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/bgfx-android --target storm_bgfx_link_check -j2
```

The final shared link requires `--no-undefined`, ARM64/API24 and `c++_shared`,
with Android/log/EGL/GLESv3 dependencies propagated to consumers. This detects
missing platform symbols that a static archive-only check cannot detect. The
probe is never installed or packaged. Cross-compilation is not runtime Android
validation; Vulkan and GLES shader/runtime coverage remain separate requirements.

Validation on 2026-10-17: headless Mesa lavapipe Vulkan passed all four pixel
scenarios (center RGB respectively `72,48,40`, `16,32,48`, `16,32,48`,
`16,40,64`). No X server or Android device was used for that test. NDK r30
ARM64/API24 final link passed with `--no-undefined`; ELF reports AArch64 and
`libc++_shared.so`. The changed legacy `sky.cpp` passed an Android syntax check.
A host harness using the original generator from `d5cbf60` compared all vertex
and index bytes with the shared generator and passed. These results do not
constitute full-engine or physical Android runtime validation.

## Remaining integration gates

1. Give an opt-in bgfx backend exclusive frame/surface ownership, including
   Android surface loss/recreation, shutdown and presentation. Keep the legacy
   backend selectable and retain resolution/vsync/quality preferences.
2. Connect `SKY::UpdateFogSphere` data and its real camera/world transform to the
   new frame owner. Port textured sky/cloud blending next, with real day/night,
   rain and horizon captures. Preserve ordered views and alpha/depth semantics.
3. Separate renderer-neutral resources and draw descriptors from `VDX9RENDER`.
   Its COM pointers, D3D declarations/FVF, lock/unlock access, texture stages and
   render-state save/restore cannot be replaced by a name-only adapter. Audit
   callers in geometry/model/animation/water/particles/shadows and the interface.
4. Port material/shader families explicitly, including skinning, sea reflection
   and refraction targets, DXT texture uploads, fog/lighting, and postprocessing.
   Resolve matrix handedness, clip depth, texture origin and D3D half-texel rules
   per pass. Do not assume bgfx draw sorting preserves D3D submission order.
5. Validate the same saved port, interior, shore/day/night and sea scenes on both
   backends and physical Android hardware. Only then compare frame times under
   controlled load and consider a default switch or removal of DXVK.

New postprocessing or quality improvements must remain configurable. This stage
adds no forced artistic effect and makes no FPS or mobile GPU cost claim.
