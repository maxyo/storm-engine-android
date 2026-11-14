#!/usr/bin/env python3
"""Portable production mailbox, axis/pace and ordinary-motion guard regression; the stick steers the swim."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True  # keep the source tree clean
sys.path.insert(0, str(root / "tests/glm_math"))
from glm_source import ENVIRONMENT, glm_source, math_include_flags
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-root", type=Path, default=root, help="Production checkout for baseline replay")
parser.add_argument("--glm-source", type=Path, help="Offline extracted pinned GLM source")
args = parser.parse_args()
source_root = args.source_root
# The production fragments compute with glm::vec3; camera.py inherits the location.
os.environ[ENVIRONMENT] = str(glm_source(args.glm_source))
with tempfile.TemporaryDirectory(prefix="storm-movement-") as d:
    binary = Path(d)/"check"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I"+str(source_root/"src/libs/util/include"), str(Path(__file__).with_name("check.cpp")),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    character = (source_root/"src/libs/location/src/character.cpp").read_text()
    start = character.index("    const bool touchStop =")
    (Path(d)/"motion.inc").write_text(character[start:character.index("#else",start)])
    start = character.index("        const float spd =", start)
    end = character.index("        curPos += impulse * dltTime;",start)+len("        curPos += impulse * dltTime;")
    (Path(d)/"displacement.inc").write_text(character[start:end])
    start = character.index("#ifdef __ANDROID__\n        if (!touchMotion)",end)
    end = character.index("        // Rotation",start)
    (Path(d)/"strafe.inc").write_text(character[start:end])
    camera_header = (source_root/"src/libs/location/src/location_camera.h").read_text()
    camera_start = camera_header.index("    // Use the actual camera")
    (Path(d)/"camera_interface.inc").write_text(camera_header[camera_start:camera_header.index("#endif", camera_start)])
    camera_source = (source_root/"src/libs/location/src/location_camera.cpp").read_text()
    camera_start = camera_source.index("void LocationCamera::ExecuteTopos(")
    camera_end = camera_source.index("// Free flying camera execution", camera_start)
    (Path(d)/"camera_topos.inc").write_text(camera_source[camera_start:camera_end].replace("LocationCamera::", "Camera::"))
    player = (source_root/"src/libs/location/src/player.cpp").read_text()
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
    main = (source_root/"src/apps/engine/src/main.cpp").read_text()
    start = main.index("        storm::android::MovementPermission permissions;")
    end = main.index("        return permissions;",start)+len("        return permissions;")
    (Path(d)/"permissions.inc").write_text(main[start:end])
    native = Path(d)/"native-motion"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g", "-D__ANDROID__",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I"+d,
                    "-I"+str(source_root/"src/libs/util/include"), *math_include_flags(),
                    str(Path(__file__).with_name("native_motion.cpp")),
                    "-o", str(native)], check=True)
    subprocess.run([str(native)], check=True)
subprocess.run([os.environ.get("PYTHON", "python3"), str(Path(__file__).with_name("camera.py")), "--source-root", str(source_root)], check=True)
print("PASS: analog axis/pace, locks, specials, owner/generation/cancel, stale/replay/overflow, lease/rollover")
