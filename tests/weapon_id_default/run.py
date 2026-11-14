#!/usr/bin/env python3
"""Compile the production eGetWeaponID result branch; exercise no handler, blade, axe and owned lifetime."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-ref', help='read the production source from a git ref (baseline control)')
a = p.parse_args()
path = 'src/libs/location/src/character.cpp'
source = subprocess.check_output(['git', '-C', str(root), 'show', f'{a.source_ref}:{path}'], text=True) if a.source_ref else (root/path).read_text()
end = source.index('                VDATA *vd = nullptr;', source.index('core.Event("eGetWeaponID"'))
start = source.rfind('\n', 0, source.index('core.Event("eGetWeaponID"'))
start = source.rfind('\n', 0, start)
branch = source[start:end]
name = 'weapon_id' if 'std::string weapon_id' in branch else 'pWeaponID'
conditions = re.findall(r'storm::iEquals\(' + name + r', "topor"\)', source)
assert len(conditions) == 3, 'force/block/block-hit must all use the weapon selected by this branch'
code = r'''
#include <cassert>
#include <string>
#include <cstdio>
#include <cctype>
struct VDATA { std::string text; const char *GetString() { return text.c_str(); } };
struct Core {
    VDATA data;
    bool present = false;
    VDATA *Event(const char *event, const char *format, const char *id) {
        assert(std::string(event) == "eGetWeaponID" && std::string(format) == "s" && std::string(id) == "hero");
        return present ? &data : nullptr;
    }
} core;
namespace storm {
bool iEquals(const std::string &a, const char *b) {
    std::string low = a; for (char &c : low) c = std::tolower(static_cast<unsigned char>(c)); return low == b;
}}
std::string weapon(bool present, const char *text, bool invalidate = false) {
    core.present = present; core.data.text = text;
    const char *characterID = "hero";
BRANCH
    const bool axe = CONDITION;
    if (invalidate) core.data.text.assign(4096, 'x');
    std::string result = NAME;
    assert(axe == (result == "topor"));
    return result;
}
int main() {
    assert(weapon(false, "unused") == "blade");
    assert(weapon(true, "blade") == "blade");
    assert(weapon(true, "topor") == "topor");
    assert(weapon(true, "topor", true) == "topor");
    puts("PASS production eGetWeaponID: absent handler defaults to blade, blade/axe results and owned event string");
}
'''.replace('BRANCH', branch).replace('CONDITION', conditions[0]).replace('NAME', name)
with tempfile.TemporaryDirectory(prefix='weapon-id-default-') as d:
    out = Path(d); (out/'check.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-Wall', '-Werror=uninitialized',
                    '-Werror=maybe-uninitialized', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(out/'check.cpp'), '-o', str(out/'check')], check=True)
    subprocess.run([str(out/'check')], check=True)
