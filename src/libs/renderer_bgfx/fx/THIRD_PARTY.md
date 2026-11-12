# Third-party code in the fx package

Only permissively licensed code is used; full texts are in `third_party/licenses/`.
List both on the application's licences screen.

| What | Where | Origin | Licence |
| --- | --- | --- | --- |
| ACES RRT+ODT fit (two 3x3 matrices and a rational curve) | `shaders/include/fx_tonemap.sh`, mirrored in `src/color_lut.cpp` | Stephen Hill, in Baking Lab `ACES.hlsl` by MJP and David Neubelt | MIT, Copyright (c) 2016 MJP — `BakingLab-ACES-MIT.txt` |
| Triangle/box overlap test (Akenine-Möller) of the AO volume bake | `src/voxelizer_impl.c` (voxelizer.h fetched at the pin in `CMakeLists.txt`) | karimnaaji/voxelizer `voxelizer.h` | MIT, Copyright (c) 2016 Karim Naaji — `voxelizer-MIT.txt` |
| Contrast adaptive sharpening (weighting formula, reduced to a five-tap cross) | `shaders/fs_fx_cas.sc` | AMD FidelityFX CAS `ffx_cas.h` | MIT, Copyright (c) 2017-2019 Advanced Micro Devices, Inc. — `FidelityFX-CAS-MIT.txt` |

Everything else is original work under the engine's licence, including:

- `shaders/fs_fx_fxaa.sc`: written from the published description of FXAA (Timothy Lottes, NVIDIA); no
  source code from FXAA 3.11 is used.
- Bloom: the dual-filter (Kawase) down/up kernels are a published technique (Marius Bjørge, ARM, SIGGRAPH 2015);
  own code.
- Normal mapping without mesh tangents: the screen-space cotangent frame is a published technique
  (Christian Schüler, 2013); own code.
- Sky: single scattering with the usual Rayleigh / Mie coefficients; own code.
- All textures (water detail normals and foam, cloud noise, grading look-up tables, sky panorama) are generated
  procedurally at run time. No look-up tables, images or other assets are bundled.
