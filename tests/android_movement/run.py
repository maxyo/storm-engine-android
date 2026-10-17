#!/usr/bin/env python3
"""Portable production mailbox, axis/pace and ordinary-motion guard regression."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-movement-") as d:
    binary = Path(d)/"check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I"+str(root/"src/libs/util/include"), str(Path(__file__).with_name("check.cpp")),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    character = (root/"src/libs/location/src/character.cpp").read_text()
    start = character.index("    const bool touchStop =")
    (Path(d)/"motion.inc").write_text(character[start:character.index("#else",start)])
    start = character.index("        const float spd =", start)
    end = character.index("        curPos += impulse * dltTime;",start)+len("        curPos += impulse * dltTime;")
    (Path(d)/"displacement.inc").write_text(character[start:end])
    start = character.index("#ifdef __ANDROID__\n        if (!touchMotion)",end)
    end = character.index("        // Rotation",start)
    (Path(d)/"strafe.inc").write_text(character[start:end])
    player = (root/"src/libs/location/src/player.cpp").read_text()
    start = player.index("    if (task.task != npct_none || command.cmd != aicmd_none || GetId()")
    end = player.index("    androidMovementHeld =",start)
    (Path(d)/"release.inc").write_text(player[start:end])
    start = player.index("                SetRunMode(IsRunMode(dltTime));")
    end = player.index("            }\n        }\n        else\n            lastChange",start)
    (Path(d)/"physical.inc").write_text(player[start:end])
    start = player.index("void Player::StrafeWhenMove(")
    end = player.index("bool Player::IsDoBlock()",start)
    (Path(d)/"strafe_controls.inc").write_text(player[start:end])
    start = player.index("void Player::StopAndroidMovement()")
    end = player.index("void Player::TraceAndroidMovement(",start)
    (Path(d)/"stop.inc").write_text(player[start:end])
    start = player.index("                const auto &axes = *androidAxes;")
    end = player.index("            }\n            else\n#endif",start)
    (Path(d)/"analog_decision.inc").write_text(player[start:end])
    main = (root/"src/apps/engine/src/main.cpp").read_text()
    start = main.index("        storm::android::MovementPermission permissions;")
    end = main.index("        return permissions;",start)+len("        return permissions;")
    (Path(d)/"permissions.inc").write_text(main[start:end])
    native = Path(d)/"native-motion"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g", "-D__ANDROID__",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I"+d,
                    "-I"+str(root/"src/libs/util/include"), str(Path(__file__).with_name("native_motion.cpp")),
                    "-o", str(native)], check=True)
    subprocess.run([str(native)], check=True)
print("PASS: analog axis/pace, locks, specials, owner/generation/cancel, stale/replay/overflow, lease/rollover")
