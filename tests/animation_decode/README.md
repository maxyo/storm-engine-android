# Packed animation endpoint decoding

`Bone::BlendFrame` may reuse a decoded quaternion when the two packed keys
are identical. It must still run the original slerp: equal endpoints do not
justify replacing floating-point blend operations with a direct return.

Run the synthetic differential test with the same GLM source used by CMake:

```sh
python3 tests/animation_decode/run.py --glm-source /path/to/glm
CXX=g++ python3 tests/animation_decode/run.py --glm-source /path/to/glm --sanitize
```

The fixture extracts production `GetFrame` and `BlendFrame` method bodies,
uses the production `storm::math::slerp` and `glm::mat4_cast`, and compares against the previous
BlendFrame implementation. Core, resource services and the Bone allocator are
not involved. It compares quaternion/local-matrix bytes and FP exception flags
under four rounding modes. Synthetic inputs cover every signed 16-bit packed
component value, equal/unequal keys, empty/terminal tracks, signed zero and
clamped/interior coefficients.

For locally available original `.an` files (not distributed with these tests):

```sh
python3 tests/animation_decode/run.py --glm-source /path/to/glm \
  --animations /path/to/RESOURCE/animation --mode diff
```

The loader reproduces the existing AN layout and SetAngles quantization;
loading uses nearest rounding, then playback decoding is tested in all modes.
`--mode stats` counts identical neighboring keys. `--mode bench` runs an ABBA
microbenchmark with runtime coefficient arguments and non-inlined methods.
It reports BlendFrame cost only: it does not measure the game renderer or FPS,
and sequential traversal is not an actual distribution of active actions.
Validate target ARM64 correctness and game frame cost separately before
accepting a performance claim. The miss path has an added comparison/branch.
