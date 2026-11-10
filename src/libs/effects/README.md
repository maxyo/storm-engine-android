# Effects (Caribbean Legend Effekseer effects)

The `Effects` entity and the `EFFECTS_LIBRARY` script library that Caribbean Legend's
`Program/particles.c` declares. Effects are played and drawn through
`storm/render/effekseer.hpp` (renderer_bgfx).

## Effect ids

`Resource/Effects/effects.json` maps an id to `{file, speed}` (parsed by `scene_data`, loaded on
first use through the file service, ids case-insensitive). An entry whose file is neither
`.efkefc` nor `.xps` is logged and skipped.

## Natives (EFFECTS_LIBRARY)

| Native | Behaviour |
| --- | --- |
| `int CheckAndCreateEffect(id, x, y, z, rx, ry, rz, lifetime, isDirect)` | `.efkefc` id: plays it on the `Effects` entity with the table speed, returns the handle (-1 if it cannot play). `.xps` id: `PS_CREATE` (or `PS_CREATEX` when `isDirect`) to the `particles` entity with the file name, returns -1 (the scripts' contract: particle systems have no handle). An id outside the table is a legacy particle system name (`CreateParticleSystem("torch", ...)`), created as `<id>.xps` and logged once per name. `isDirect`: the angles are a direction vector, turned into angles as `PS_CREATEX` does. `lifetime` (ms, 0 = none): the effect stops spawning (`StopRoot`) after it. |
| `int ReloadEffect(id)` | re-reads a loaded effect (loads it if it is not loaded): 1 / 0 |
| `void ReloadAllEffects()` | re-reads `effects.json` and every loaded effect of it |
| `int UnloadEffect(id)` | releases a loaded effect: 1, or 0 when it was not loaded |
| `void UnloadAllEffects()` | releases every effect |

## Entity messages (`MSG_EFFECT_*`, `shared/messages.h`, CL values 80001..)

`PLAY "lsfff[fff[fff[l]]]"` (id, position, angles, scale, start frame) and `PLAY_FILE "lsfff"`
(game path) return the handle or -1; `DELETE`, `DELETE_ALL`, `STOP_ROOT`, `SET_PARAMETER` /
`GET_PARAMETER` (Effekseer dynamic inputs), `SEND_TRIGGER`, `IS_PLAYING`, `PAUSE`,
`UPDATE_POS` / `ADD_SHIFT` / `GET_POS`, `UPDATE_ROT`, `SET_TARGET_POS`, `SET_SCALE`,
`SET_SPEED` / `GET_SPEED`, `SET_COLOR` (ARGB). Other codes are logged as not handled
(`PAUSE_ALL`, `SET_CAPTURE`, `STOP_CAPTURED`: no CL 2.0 script sends them).

The scripts create the entity without layers: it adds itself to `EXECUTE`/`SEA_EXECUTE`
(`Update`) and to `REALIZE`/`SEA_REALIZE` at 65536 (drawn after the opaque scene, like the
particles). Land and sea layer pairs are never active together.

Limitations (logged): a non-zero start frame of `MSG_EFFECT_PLAY` is not supported (no CL 2.0
script passes one). Angles go to Effekseer's `SetRotation` (radians, its XYZ order).

## Check

```sh
ninja -C <build> effects_check
<build>/effects_check                # synthetic table: ids, particle path, failure paths
<build>/effects_check <CL game dir>  # real effects on bgfx Noop (read-only), exit 77 if missing
```
