#!/usr/bin/env python3
"""A script segment unloaded in this frame is not loaded: loading it again in the same frame
loads it afresh (interface.c LoadGameOptions twice in one frame used to answer "Segment
already loaded" with its functions invalidated: "Invalid function call"), and the next unload
finds the live copy. Runs the compiler's LiveSegment against a segment table played through
load / unload / load / unload / ProcessFrame; no engine build."""
from pathlib import Path
import os, re, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/libs/core/src/compiler.cpp').read_text()
start = source.index('static bool LiveSegment(')
live = source[start:source.index('\n}', start) + 2]
# Every lookup by name goes through it.
for name in ('void COMPILER::UnloadSegment(', 'bool COMPILER::BC_SegmentIsLoaded(', 'bool COMPILER::BC_LoadSegment('):
    body = source[source.index(name):]
    body = body[:body.index('\n}\n')]
    assert 'LiveSegment(SegmentTable[n], ' in body, f'{name} looks segments up without LiveSegment'
    assert not re.search(r'strcmp\(SegmentTable\[n\]\.name', body), f'{name} still compares names directly'
code = r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
struct SEGMENT_DESC { std::string name; unsigned id = 0; bool bUnload = false; bool functions = true; };
''' + live + r'''
std::vector<SEGMENT_DESC> table;
unsigned next_id = 0;
// The compiler's three operations on the table, as compiler.cpp does them.
bool Load(const char *name) {
    for (auto &s : table) if (LiveSegment(s, name)) return s.functions; // "already loaded"
    table.push_back({name, next_id++, false, true});
    return true;
}
void Unload(const char *name) {
    for (auto &s : table) if (LiveSegment(s, name)) { s.bUnload = true; s.functions = false; break; }
}
bool Loaded(const char *name) { for (auto &s : table) if (LiveSegment(s, name)) return true; return false; }
void ProcessFrame() { std::erase_if(table, [](const SEGMENT_DESC &s) { return s.bUnload; }); }
int main() {
    const char *options = "interface\\option_sl.c";
    // LoadGameOptions twice in one frame.
    assert(Load(options) && Loaded(options));
    Unload(options);
    assert(!Loaded(options));
    assert(Load(options) && Loaded(options)); // its functions are there: no "Invalid function call"
    Unload(options);                           // the live copy, not the stale one
    assert(!Loaded(options) && table.size() == 2);
    ProcessFrame();
    assert(table.empty());
    // A segment loaded twice without an unload in between is the same segment.
    assert(Load(options) && Load(options) && table.size() == 1);
    // A segment that unloads itself and is used again next frame.
    Unload(options); ProcessFrame(); assert(!Loaded(options) && Load(options) && Loaded(options));
    std::cout << "PASS a segment unloaded this frame is loaded afresh and unloaded as the live copy\n";
}
'''
with tempfile.TemporaryDirectory(prefix='script-segments-') as d:
    p = Path(d); (p / 'check.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(p / 'check.cpp'), '-o', str(p / 'check')], check=True)
    subprocess.run([str(p / 'check')], check=True)
