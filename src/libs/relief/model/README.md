# The DeepBump model of the relief maps

`deepbump256-int8.onnx` (7 084 998 bytes, sha256
`24b0975701c94500a7ecbc1e0927fc0d07803c19310437ddc4b4ca86fe239603`) is derived from DeepBump's
`deepbump256.onnx` (github.com/HugoTini/DeepBump, commit fad19ba8, sha256 `3a2aba…6d67`) by `make_model.py`:
constant-scale upsamplings and a free input size (the model runs once per whole texture), then static int8
quantisation (QDQ, per-channel weights) calibrated on procedural textures. It is a modified version of GPL-3.0 weights
and stays GPL-3.0 (`third_party/licenses/DeepBump-GPL-3.0.txt`; provenance in `DeepBump-PROVENANCE.txt`).
Regenerate with `make_model.py` (pinned versions in its docstring; deterministic) when the source or the tools change,
and bump `fx::kReliefMethodVersion`.

Against the float model as DeepBump runs it (256 tiles, overlap 64) on 16 TEHO textures at 512 px, the RMS of the
normals' tilt difference relative to their tilt: this model 36 %; the float model on the whole image 27 % (the whole
image alone changes the normals as much as the int8 weights do); float 512 tiles 18 %. One inference of a 512 texture
on an x86-64 core: 213 ms against 579 ms for the float model's nine 256 tiles.
