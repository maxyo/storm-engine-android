#!/usr/bin/env python3
"""The touch HUD's land command refresh policy and its wiring, with sanitizers."""
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


land = (root / 'src/libs/battle_interface/src/land/battle_land.cpp').read_text()
main = (root / 'src/apps/engine/src/main.cpp').read_text()
# The main loop polls; it never asks the scripts unconditionally per iteration.
loop = main[main.index('STORM_PROFILE_ZONE("HUD adapter: land commands")'):]
loop = loop[:loop.index('STORM_PROFILE_ZONE("HUD adapter: gameplay')]
assert 'pollLandCommands(' in loop, 'the main loop must poll the land commands'
assert loop.index('pollLandCommands(') < loop.index('landCommands.take('), 'poll before taking a tap'
# A tap is checked against a fresh read of the scripts, right before it acts.
execute = member(land, 'bool executeLandCommand(')
assert execute.index('refreshLandCommands(true)') < execute.index('ExecuteAndroidLand('), execute
# The poll goes through the policy; closing natively forces the next ask.
poll = member(land, 'void pollLandCommands(')
assert re.search(r'landRefresh\.due\(tick,\s*landRefreshSignal\(ready,', poll), poll
assert 'landRefresh.force()' in member(land, 'void closeLandCommands()')
# The signal stays cheap: no script event in it.
assert 'Event(' not in member(land, 'LandRefreshSignal landRefreshSignal(')

with tempfile.TemporaryDirectory(prefix='storm-hud-refresh-') as directory:
    binary = Path(directory) / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I' + str(root / 'src/libs/util/include'), str(here / 'check.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
