#!/usr/bin/env python3
"""The touch HUD asks the scripts only on the original game's invalidation, never on a timer."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]


def member(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def read(path):
    return (root / path).read_text()


land = read('src/libs/battle_interface/src/land/battle_land.cpp')
sign = read('src/libs/battle_interface/src/land/battle_man_sign.cpp')
sign_h = read('src/libs/battle_interface/src/land/battle_man_sign.h')
main = read('src/apps/engine/src/main.cpp')
abilities = read('src/apps/engine/src/android_ship_abilities.hpp')

# The original's update messages reach the touch HUD's flag through SetUpdate.
process = member(land, 'uint64_t BATTLE_LAND_INTERFACE::ProcessMessage(')
for message in ('MSG_BATTLE_LAND_START', 'BI_MSG_CARE_COMMANDLIST_UPDATE'):
    case = process[process.index('case ' + message):]
    assert 'SetUpdate()' in case[:case.index('break;')], message
assert 'm_bAndroidChanged = true;' in member(sign_h, 'void SetUpdate()')
assert 'SetUpdate();' in member(sign, 'void BIManSign::SetActive(')
assert 'm_bAndroidChanged = true;' in member(sign, 'void BIManSign::ExecuteCommand(')
assert 'UpdateCooldowns()' in member(sign, 'void BIManSign::Draw(')

# The main loop polls the gate; no clock reaches it.
loop = main[main.index('STORM_PROFILE_ZONE("HUD adapter: land commands")'):]
loop = loop[:loop.index('STORM_PROFILE_ZONE("HUD adapter: gameplay')]
assert re.search(r'pollLandCommands\(storm::android::landCommandsReady\(gameplayReady\)\);', loop), loop
assert loop.index('pollLandCommands(') < loop.index('landCommands.take(')
poll = member(land, 'void pollLandCommands(')
assert 'TakeAndroidLandChanged()' in poll and 'landRefresh.due(' in poll and 'Tick' not in poll, poll
# A tap is checked against a fresh read right before it acts; a native close asks again.
execute = member(land, 'bool executeLandCommand(')
assert execute.index('refreshLandCommands(true)') < execute.index('ExecuteAndroidLand('), execute
assert 'landRefresh.force()' in member(land, 'void closeLandCommands()')
# The targets of the open menu are not part of the closed menu's identity.
key = member(land, 'std::string landOwnerKey(')
assert key.index('if (!active) return key;') < key.index('"dialogready"') < key.index('"AndroidLand"'), key

# Ship abilities: asked on a change of procSetUsingAbility's inputs, not on a period.
refresh = member(abilities, 'inline void refreshShipAbilities(')
assert 'shipAbilityInputs(frame)' in refresh and 'inputs == last' in refresh, refresh
assert 'tick' not in refresh and '250' not in refresh, refresh
for removed in ('android_hud_refresh.hpp', 'HudRefreshPolicy'):
    for path in ('src/libs/util/include/platform/android_land_commands.hpp', 'src/apps/engine/src/main.cpp'):
        assert removed not in read(path), (removed, path)

with tempfile.TemporaryDirectory(prefix='storm-hud-invalidate-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I' + str(root / 'src/libs/util/include'), str(here / 'check.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
