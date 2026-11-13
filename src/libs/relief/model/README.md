# The PBRify models of the relief maps

`pbrify-normal-int8.onnx` (501 397 bytes, sha256 `fc91ce30e48deaf5847ed19342bd1fc7a8ffc11a2ec94f26c4d6bcbe63df4b33`)
and `pbrify-height-int8.onnx` (501 394 bytes, sha256
`01f251dc40e79fa7ef5348b51f538de5a14bc6113c8ea3173ba78ce0b648c409`) and `pbrify-roughness-int8.onnx` (501 397 bytes,
sha256 `860845c92142b7d1bd89ac514ae58206d431fd2c899e955dd492348389c5f00b`) are made by `make_models.py` from
Kim2091/PBRify_Remix's SPAN models 1x-PBRify_NormalV3, 1x-PBRify_Height and 1x-PBRify_RoughnessV2 (CC0-1.0), as ONNX exports by
fernandotonon/QtMeshEditor-models (revision b35b7f4c, CC0-1.0; their sha256 pinned in the script): static int8
quantisation (QDQ, per-channel weights) calibrated on procedural textures with the game's 48 px wrap padding (no game
data), unused nodes dropped. They stay CC0-1.0 (`third_party/licenses/PBRify-CC0-1.0.txt`; provenance in
`PBRify-PROVENANCE.txt`). Input and output 1x3xHxW, 0..1, any size.

Regenerate with `make_models.py` (pinned versions in its docstring; deterministic) when the source or the tools
change, then update `fx::kReliefNormalModelDigest` / `fx::kReliefHeightModelDigest` / `fx::kReliefRoughnessModelDigest` (the build checks the files
against them; they are part of the maps' method, so the maps are made again).

Against the float models on 10 TEHO textures at 512 px, the correlation of the int8 output: at least 0.9985 for the
normals and 0.9917 for the height; the roughness on 8 of them at 256 px at least 0.991 (0.9913..0.9995; a
calibration on game textures was worse, 0.967..0.997, and would put game data into the file). One 512 texture on an x86-64 host with 4 threads: about 0.19 s per model (the float
model 0.52 s).
