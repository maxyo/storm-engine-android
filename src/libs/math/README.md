# Math

The engine uses [GLM](https://github.com/g-truc/glm) 1.0.3 (pinned in `cmake/glm.cmake`) directly:
`glm::vec3`, `glm::dvec3`, `glm::vec4`, `glm::mat4`, `glm::quat`. There are no engine vector or matrix
classes. `include/storm_glm.h` is the only header that includes GLM and the only place that holds the
engine's conventions (`namespace storm::math`). Include it instead of GLM headers.

## Conventions

| Topic | Rule |
| --- | --- |
| Handedness | Left-handed, Y up, as in Direct3D 9. Depth range 0..1. |
| Matrix storage | `glm::mat4` holds the same 16 floats, in the same order, as `D3DMATRIX`: `m[i][j]` is D3D row `i`, column `j`. `m[0]`, `m[1]`, `m[2]` are the X, Y, Z axes and `m[3]` is the translation. |
| Multiplication | The game's formulas are written for D3D row vectors (`p' = p * World * View * Proj`). GLM multiplies column vectors, so the same transform is `Proj * View * World * p`. "Apply `a`, then `b`" is `b * a`. |
| Points | `storm::math::transformPoint(m, p)` (translation included, no perspective divide). `glm::mat3(m) * n` rotates a direction. |
| Euler angles | `storm::math::eulerMatrix(ax, ay, az)` rotates about Z, then X, then Y. |
| Projection | `storm::math::projectionMatrix`: the vertical field of view is the horizontal angle scaled linearly by the aspect ratio. Content and cameras depend on this; `glm::perspective` gives a different picture. |
| Default values | `GLM_FORCE_CTOR_INIT` is defined for the whole engine: `glm::mat4 m;` is the identity, vectors are zero, `glm::quat q;` is the identity rotation. |
| Normalizing | `storm::math::normalizeOrZero(v)` returns zero for a zero vector. `glm::normalize` returns NaN there; use it only when the input cannot be zero. |
| Alignment | GLM types are aligned like `float` (4 bytes). `glm::mat4` is not 16-byte aligned: SIMD code must use unaligned loads. Do not put GLM members into `#pragma pack(1)` structs at odd offsets. |

## Boundaries

- **Direct3D-shaped renderer API**: `storm::math::d3dMatrix(m)` views a `glm::mat4` as `D3DMATRIX *` without
  copying. Nothing is transposed: the bytes already are the D3D matrix.
- **Shader constants and raw floats**: `storm::math::data(m)` is the 16 floats in D3D order. Transpose only
  where the shader expects it (`glm::transpose`).
- **Vertex buffers and files**: `glm::vec3` is 12 bytes `x, y, z`; `glm::vec4` 16 bytes; `glm::mat4` 64 bytes.
  The layouts are asserted in `tests/glm_math`.
- **Variadic messages** (`core/src/message.cpp`): a `glm::vec3` passed through `...` is read back as a plain
  struct of three floats.

## Tests

`tests/glm_math/run.py` builds the contract and randomized checks. With `--baseline <revision>` it builds the
same harness against the math headers of an older revision (for example `c6788d72`, the last one with the
original hand-written classes) and reports bit-exactness and ULP distance for every operation.
Known, intentional differences from the original classes are below one ULP of the result's magnitude:
`glm::length` and `v / f` work in `float` where the old code took a `double` detour, `eulerMatrix` multiplies
the three sines and cosines in GLM's order, and `-v` of a zero component yields `+0`.
