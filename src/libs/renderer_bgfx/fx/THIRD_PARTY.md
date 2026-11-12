# Third-party code in the fx package

Only permissively licensed code is used; full texts are in `third_party/licenses/`.
List both on the application's licences screen.

| What | Where | Origin | Licence |
| --- | --- | --- | --- |
| AgX tone curve on grey (log2 range of Blender's AgX by Troy Sobotka, sigmoid polynomial of Benjamin Wrensch's "Minimal AgX") | `shaders/include/fx_tonemap.sh`, mirrored in `src/color_lut.cpp` | three.js `tonemapping_pars_fragment.glsl.js` (r160) | MIT, Copyright (c) 2010-2023 three.js authors — `threejs-MIT.txt` |
| Triangle/box overlap test (Akenine-Möller) of the AO volume bake | `src/voxelizer_impl.c` (voxelizer.h fetched at the pin in `CMakeLists.txt`) | karimnaaji/voxelizer `voxelizer.h` | MIT, Copyright (c) 2016 Karim Naaji — `voxelizer-MIT.txt` |
| FSR 1: EASU upscale and RCAS sharpening (pixel-shader form; constants set up on the CPU with the same header) | `shaders/fs_fx_easu.sc`, `fs_fx_rcas.sc`, `include/fx_fsr.sh`, `src/post_chain.cpp` (`ffx_a.h`, `ffx_fsr1.h` fetched at the pin in `CMakeLists.txt`) | AMD FidelityFX Super Resolution 1, GPUOpen-Effects/FidelityFX-FSR | MIT, Copyright (c) 2021 Advanced Micro Devices, Inc.; `ffx_a.h` also Copyright (c) 2014 Michal Drobot — `FidelityFX-FSR-MIT.txt` |
| SMAA 1x: edge detection, blending weights, neighbourhood blending, and the precomputed area and search tables | `shaders/fs_fx_smaa_*.sc`, `vs_fx_smaa.sc`, `include/fx_smaa.sh`, `src/post_chain.cpp` (`SMAA.hlsl`, `Textures/AreaTex.h`, `SearchTex.h` fetched at the pin in `CMakeLists.txt`) | Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez, iryoku/smaa | MIT, Copyright (C) 2013 the authors — `SMAA-MIT.txt` |
| Optimised percentage-closer filter (3x3 in four and 5x5 in nine hardware-filtered compare taps, after The Witness) of the lamp shadows | `../shaders/include/ff_fragment.sh` (`FF_LAMP_PCF3`, `FF_LAMP_PCF5`, rewritten for the lamp atlases) | Matt Pettineo, TheRealMJP/Shadows (`SampleShadowMapOptimizedPCF`) | MIT, Copyright (c) 2016 MJP — `MJP-Shadows-MIT.txt` |

Everything else is original work under the engine's licence, including:

- Bloom: the dual-filter (Kawase) down/up kernels are a published technique (Marius Bjørge, ARM, SIGGRAPH 2015);
  own code.
- Normal mapping without mesh tangents: the screen-space cotangent frame is a published technique
  (Christian Schüler, 2013); own code.
- Sky state (the grade of the place and hour, the hemisphere ambient): single scattering with the usual Rayleigh /
  Mie coefficients; own code.
- All textures (grading look-up tables, the sky panorama the sky state averages) are generated
  procedurally at run time. The only bundled tables are SMAA's area and search textures (above), compiled in
  from the fetched headers.
