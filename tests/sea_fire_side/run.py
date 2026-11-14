#!/usr/bin/env python3
"""Check the player's camera fire side selection (fire_side.hpp) and that AI fire keeps the stock zone rules."""
import os
from pathlib import Path
import subprocess
import tempfile

r = Path(__file__).resolve().parents[2]
controller = (r/'src/libs/sea_ai/src/ai_ship_cannon_controller.cpp').read_text()


def body(signature):
    start = controller.index(signature)
    return controller[start:controller.index('\n}\n', start)]


# The camera-aimed paths use the full-circle selection ...
for signature in ('bool AIShipCannonController::Fire(const glm::vec3 &vFireCamPos, const glm::vec3 &vFireDir)',
                  'glm::vec3 AIShipCannonController::GetFirePos(const glm::vec3 &vFireDir, float fDistance)',
                  'glm::vec3 AIShipCannonController::GetFirePos(const glm::vec3 &vFireDir)\n',
                  'bool AIShipCannonController::isCanFire(const glm::vec3 &vCamDir) const'):
    assert 'GetCameraFireBort(' in body(signature), f'{signature.strip()}: not the camera fire side'
# ... AI fire (at a position, at a ship, the fire tests) keeps the stock zone rule.
for signature in ('bool AIShipCannonController::Fire(const glm::vec3 &vFirePos)',
                  'bool AIShipCannonController::Fire(AIShip *pEnemy)',
                  'bool AIShipCannonController::isCanFirePos(const glm::vec3 &vFirePos)',
                  'bool AIShipCannonController::isCanFire(AIShip *pEnemy)',
                  'auto AIShipCannonController::GetFirstFireBort(',
                  'auto AIShipCannonController::GetNextFireBort('):
    assert 'GetCameraFireBort(' not in body(signature), f'{signature}: AI fire must keep the zone rule'
assert 'if (fY0 <= bort.fCosFireZone)' in body('bool AIShipCannonController::isCanFireBort('), 'isCanFireBort lost its zone test'
navigator = (r/'src/libs/battle_interface/src/sea/battle_navigator_android.cpp').read_text()
assert 'facing=storm::fire_side::nearest(' in navigator, 'the minimap lights another side than the one that fires'

with tempfile.TemporaryDirectory(prefix='sea-fire-side-') as tmp:
    binary = Path(tmp)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I'+str(r/'src/libs/util/include'),
                    str(Path(__file__).with_name('check.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
