#!/usr/bin/env python3
"""Exercise the real map mailbox and snapshot with recording script boundaries."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='storm-worldmap-') as temp:
    out=Path(temp)
    commands=(root/'src/libs/battle_interface/src/world_map_interface/ship_command.cpp').read_text()
    (out/'commands.inc').write_text(commands[commands.index('std::vector<storm::android::MapCommand> WMShipCommandList::AndroidMapCommands'):commands.index('bool WMShipCommandList::SelectAndroidMapCommand')])
    (out/'select.inc').write_text(commands[commands.index('bool WMShipCommandList::SelectAndroidMapCommand'):commands.rindex('#endif')])
    interface=(root/'src/libs/battle_interface/src/world_map_interface/interface.cpp').read_text()
    (out/'activate.inc').write_text(interface[interface.index('bool WM_INTERFACE::ActivateAndroidMapCommand'):interface.index('void storm::android::readWorldMapCommands')])
    common=[os.environ.get('CXX','c++'),'-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'src/libs/util/include'),'-I'+temp]
    subprocess.run(common+[str(here/'check.cpp'),'-o',str(out/'check')],check=True)
    subprocess.run([str(out/'check')],check=True)
    source=(root/'src/apps/engine/src/android_worldmap_state.hpp').read_text()
    (out/'snapshot.inc').write_text(source[source.index('inline std::string mapText'):source.rindex('\n}\n#endif')])
    source=(root/'src/apps/engine/src/android_ship_state.hpp').read_text()
    (out/'helpers.inc').write_text(source[source.index('inline ATTRIBUTES *shipMainCharacter'):source.index('inline SHIP_BASE *shipForCharacter')]+source[source.index('inline std::optional<double> shipNumber'):source.index('inline ShipTelemetryEntry shipEntry')])
    (out/'entry.inc').write_text(source[source.index('inline ShipTelemetryEntry shipEntry'):source.index('inline bool shipSailControlAllowed(')])
    subprocess.run(common[:1]+['-I'+str(root/'tests/mobile_inventory/stubs')]+common[1:]+['-DEX_OFF','-Wno-deprecated-declarations','-include','memory','-I'+str(root/'tests/mobile_inventory/stubs'),'-I'+str(root/'src/libs/core/include'),'-I'+temp,str(here/'snapshot.cpp'),str(root/'src/libs/core/src/attributes.cpp'),'-o',str(out/'snapshot')],check=True)
    subprocess.run([str(out/'snapshot')],check=True)
