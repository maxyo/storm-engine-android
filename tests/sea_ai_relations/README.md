# Sea AI relation numbers

```sh
python3 tools/ci/run_host_tests.py --only sea_ai_relations
```

The sea AI asks the scripts for the relation between two characters (`GET_RELATION_EVENT`)
and hands relations back to them (`SeaAI.Update` attributes, `AI_MESSAGE_GET_RELATION`). The
scripts number relations by the game's own `RESOURCE/shared/sea_ai/Script_defines.h`: To Each
His Own 1.7.3 uses FRIEND 0, NEUTRAL 1, ENEMY 2, UNKNOWN 3, Caribbean Legend the engine's
UNKNOWN 0, FRIEND 1, NEUTRAL 2, ENEMY 3, ALLIANCE 4. Read as engine numbers, a TEHO enemy (2)
was a neutral ship, so enemy ships never fired at the player.

The check runs `storm::sea_ai::ScriptRelations` (`src/libs/sea_ai/src/script_relations.hpp`)
on both numberings and on broken headers (a missing or repeated define, an unnamed value must
throw), runs `COMPILER::FindIntegerDefine` verbatim from `compiler.cpp` against the real
`S_DEFTAB`, and checks that the three crossing points in the sea AI translate.

The game-data check is the `battle:<island>` scene of `tools/scene_sweep`: it fails when the
enemy ship has not fired its cannons within 90 s of game time at sea.
