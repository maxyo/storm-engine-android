#!/usr/bin/env python3
"""The coverage rule's layer table: behaviour, and every call site classified."""
import os
import re
import subprocess
import tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='hud-layers-') as d:
    binary = Path(d)/'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(root/'src/libs/util/include'), str(Path(__file__).with_name('check.cpp')),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
header = (root/'src/libs/util/include/platform/android_hud_presenter.hpp').read_text()
table = set(re.findall(r'\{"([^"]+)", LegacyLayerKind::', header))
used = {}
for path in (root/'src/libs').rglob('*.cpp'):
    for name in re.findall(r'legacyDrawRefused\("([^"]+)"', path.read_text(errors='replace')):
        used.setdefault(name, path.relative_to(root))
assert set(used) <= table, f'unclassified legacy layers: {sorted(set(used) - table)}'
assert table <= set(used), f'classified layers nobody draws: {sorted(table - set(used))}'
# The sea interface draws the bars over the ships under their own world-space switch.
battle = (root/'src/libs/battle_interface/src/sea/i_battle.cpp').read_text()
assert re.search(r'if \(shipBars && m_bShowBattleNavigator && m_pShipInfoImages\)\s*m_pShipInfoImages->Draw\(\);', battle), \
    'i_battle.cpp: the ship bars must draw under the "ship bars" switch, not the sea interface\'s'
print(f'PASS: {len(table)} legacy layers classified, all drawn; ship bars drawn at sea')
