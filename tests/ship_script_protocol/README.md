# Ship script protocol

```sh
python3 tools/ci/run_host_tests.py --only ship_script_protocol
```

Players run the vanilla "To Each His Own" 1.7.3 scripts from their own game files. This
ASan/UBSan check builds and reads the ship's script messages and events with the real
`MESSAGE` (`src/libs/core/src/message.cpp`) through the helpers in
`src/libs/ship/src/ship_script_protocol.hpp` that `SHIP` uses:

- `MSG_SHIP_LIGHTSRESET`: the vanilla form `"l"` (no light flag; the ship then takes the state
  from the character's `ship.lights`) and the later `"ll"` form; any other layout stays a
  `MessageFormatError`.
- `SHIP_HULL_DAMAGE`: the event is read in the order of vanilla `Ship_HullDamage`
  (`sea_ai/AIShip.c`): damage type, hull node, x/y/z, damage, the hit character, the ball owner
  character and the hull node name (the rudder is the node named `baller*`).
