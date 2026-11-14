#!/usr/bin/env python3
"""The scene sweep's picture and renderer checks (tools/scene_sweep/run.py frame_reasons, check_renderer) on
synthetic frames and log lines: a scene with its lower part not drawn or without its HUD fails, every variant is
judged, dark interiors, dialogues and the boot scenes pass; a sweep on the GPU refuses OpenGL ES, lavapipe or a
missing renderer line, and --llvmpipe refuses the GPU."""
import importlib.util
from pathlib import Path
import random
import sys
import threading
import types

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('scene_sweep', root / 'tools/scene_sweep/run.py')
sweep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sweep)
from PIL import Image, ImageDraw  # noqa: E402  (run.py has checked that Pillow is there)

W, H = 1280, 720


def frame(upper=(120, 170, 220), lower=(30, 80, 100), portrait=True, compass=True, dialogue=False):
    """A noisy two-part scene with the game's gold HUD elements where the sweep expects them."""
    rng = random.Random(1)
    image = Image.new('RGB', (W, H))
    draw = ImageDraw.Draw(image)
    for y in range(0, H, 8):
        base = upper if y < H // 2 else lower
        for x in range(0, W, 8):
            n = rng.randint(-12, 12) if base != (0, 0, 0) else 0
            draw.rectangle((x, y, x + 7, y + 7), fill=tuple(max(0, min(255, c + n)) for c in base))
    if portrait:
        draw.ellipse((8, 8, 92, 92), outline=(220, 170, 60), width=8)
    if compass:
        draw.ellipse((1130, 10, 1250, 130), outline=(220, 170, 60), width=10)
    if dialogue:
        draw.rectangle((70, 615, 1210, 690), fill=(170, 130, 70))
    return image


def reasons(image, kind):
    return sweep.frame_reasons(image, None, kind)[0]


def expect(name, found, wanted):
    ok = all(any(w in r for r in found) for w in wanted) if wanted else not found
    if not ok:
        raise AssertionError(f'{name}: expected {wanted or "no reason"}, got {found}')
    print(f'PASS {name}: {found or "no reason"}')


expect('a sea scene with its HUD', reasons(frame(), 'sea'), [])
expect('the sea not drawn (black under the sky)', reasons(frame(lower=(0, 0, 0)), 'sea'), ['black lower part'])
expect('the sea drawn but the HUD not', reasons(frame(portrait=False, compass=False), 'sea'),
       ['HUD not drawn (portrait and compass missing)'])
expect('at sea the compass alone missing', reasons(frame(compass=False), 'ship'), ['compass missing'])
expect('a location with its portrait', reasons(frame(compass=False), 'loc'), [])
expect('a location without portrait', reasons(frame(portrait=False, compass=False), 'loc'), ['portrait missing'])
expect('a dialogue in a location (no portrait)', reasons(frame(portrait=False, compass=False, dialogue=True), 'loc'), [])
expect('a dark interior (dark everywhere)', reasons(frame(upper=(4, 4, 4), lower=(0, 0, 0), compass=False), 'loc'), [])
expect('the main menu has no HUD', reasons(frame(portrait=False, compass=False), 'menu'), [])
expect('a black frame', reasons(frame(upper=(0, 0, 0), lower=(0, 0, 0)), 'sea'), ['black frame'])
same = frame()
digest = sweep.frame_reasons(same, None, 'sea')[1]
expect('the same frame as the previous scene', sweep.frame_reasons(same, digest, 'sea')[0], ['identical'])


def shard(llvmpipe):
    return types.SimpleNamespace(index=0, runs=1, work=Path('/work'), arguments=types.SimpleNamespace(llvmpipe=llvmpipe),
                                 context={'lock': threading.Lock(), 'render': set()})


def log(backend, device):
    return [('system.log', f'[x] [system] [info] [storm-bgfx] program cache: /c/1, 0 binaries (0.0 MB of 64 MB) for {device}'),
            ('system.log', f'[x] [system] [info] [storm-bgfx] bgfx renderer={backend} 1280x720 originBottomLeft=0')]


def renderer(name, llvmpipe, lines, refused):
    target = shard(llvmpipe)
    try:
        sweep.Shard.check_renderer(target, lines)
    except RuntimeError as error:
        if not refused:
            raise
        print(f'PASS {name}: refused ({str(error)[:90]})')
        return
    if refused:
        raise AssertionError(f'{name}: accepted {target.context["render"]}')
    print(f'PASS {name}: {", ".join(target.context["render"])}')


nvidia = 'NVIDIA GeForce RTX 5070 Ti, Vulkan 1.4.329, driver 0x94d8c080, vendor 0x10de device 0x2c05 | pipeline cache 3f3d'
mesa = 'Mesa | llvmpipe (LLVM 21.1.8, 256 bits) | OpenGL ES 3.2 Mesa 26.0.8 | bgfx API 168'
renderer('the GPU sweep on Vulkan on a GPU', False, log('Vulkan', nvidia), False)
renderer('the GPU sweep fallen back to OpenGL ES', False, log('OpenGL ES 3.0', mesa), True)
renderer('the GPU sweep on lavapipe', False, log('Vulkan', 'llvmpipe (LLVM 21.1.8, 256 bits), Vulkan 1.4.335'), True)
renderer('no renderer line before the first capture', False, log('Vulkan', nvidia)[1:], True)
renderer('--llvmpipe on llvmpipe', True, log('OpenGL ES 3.0', mesa), False)
renderer('--llvmpipe on the GPU', True, log('Vulkan', nvidia), True)
print('PASS scene sweep picture and renderer checks')
