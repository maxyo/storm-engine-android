# Render parity tools

Screenshot comparison for the D3D9 -> bgfx renderer migration.

```sh
python3 -m venv .venv && .venv/bin/pip install -r tools/render-parity/requirements.txt
.venv/bin/python tools/render-parity/ssim.py a.png b.png --mask 960,20,320,150 --heatmap diff.png
.venv/bin/python tools/render-parity/compare_runs.py BASELINE CANDIDATE --masks masks.json --heatmaps heat/
# noise floor of one renderer: compare two independent runs of the same tree
.venv/bin/python tools/render-parity/compare_runs.py BASE BASE --runs run1 run2
```

Capture trees are `<scene>/<run>/shot<N>.png`. SSIM is grayscale with an 8x8 uniform
window; masked rectangles (`[x, y, w, h]` per scene or `"*"` in masks.json) are excluded,
e.g. Android overlay widgets and the clock caption. The heatmap shows 1-SSIM in red over
a dimmed candidate frame.

Draw-state manifest: setting `ff_key_log = 1` in engine.ini makes the D3D9 renderer write
`Logs/ff_keys.json` (draw counts per fixed-function state key, per scene tag, plus per-frame
counters). Writing a tag into `Logs/ff_tag.txt` starts a new scene group.
Screenshots and manifests are evidence: keep them out of Git.
