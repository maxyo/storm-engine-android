#!/usr/bin/env python3
"""Exercise the production service factory with real singleton VMA registration."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
root = args.source_root.resolve()
build = args.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
source = (root / 'src/libs/core/src/core_impl.cpp').read_text()
start = source.index('void *CoreImpl::GetService(')
end = source.index('\nvoid CoreImpl::Trace(', start)
code = r'''
#include "vma.hpp"
#include <cassert>
#include <stdexcept>
#include <iostream>
#include <string_view>
struct SERVICE {
    virtual ~SERVICE() = default;
    virtual bool Init() = 0;
};
struct Probe : SERVICE {
    static inline int mode = 0, attempts = 0, alive = 0;
    Probe() { ++alive; }
    ~Probe() override { --alive; }
    bool Init() override {
        ++attempts;
        if (mode == 1) throw std::runtime_error("graphics device unavailable");
        return mode == 2;
    }
};
CREATE_SERVICE(Probe)
struct CoreImpl {
    struct List {
        int registered = 0;
        void Add(uint32_t, uint32_t, SERVICE*) { ++registered; }
    } Services_List;
    bool autoException = false;
    VMA *FindVMA(const char *name) {
        return std::string_view(name) == "Probe" ? &Probevmaci : nullptr;
    }
    void CheckAutoExceptions(int) {
        if (autoException) throw std::runtime_error("auto exception");
    }
    void *GetService(const char *);
};
''' + source[start:end] + r'''
int main() {
    CoreImpl core;
    Probevmaci.SetHash(MakeHashValue("Probe"));
    assert(core.GetService("missing") == nullptr);
    for (int i = 0; i < 2; ++i) {
        assert(core.GetService("Probe") == nullptr);
        assert(Probe::alive == 0 && Probevmaci.GetReference() == 0);
        assert(core.Services_List.registered == 0);
    }
    assert(Probe::attempts == 2);
    core.autoException = true;
    try { core.GetService("Probe"); assert(false); }
    catch (const std::runtime_error &e) { assert(std::string_view(e.what()) == "auto exception"); }
    assert(Probe::alive == 0 && Probevmaci.GetReference() == 0);
    core.autoException = false;
    Probe::mode = 1;
    for (int i = 0; i < 2; ++i) {
        try { core.GetService("Probe"); assert(false); }
        catch (const std::runtime_error &e) { assert(std::string_view(e.what()) == "graphics device unavailable"); }
        assert(Probe::alive == 0 && Probevmaci.GetReference() == 0);
        assert(core.Services_List.registered == 0);
    }
    assert(Probe::attempts == 5);
    Probe::mode = 2;
    auto *ready = core.GetService("Probe");
    assert(ready && Probe::alive == 1 && Probe::attempts == 6);
    assert(core.GetService("Probe") == ready);
    assert(Probe::attempts == 6 && core.Services_List.registered == 1);
    Probevmaci.Clear();
    assert(Probe::alive == 0);
    std::cout << "PASS: failed/throwing init rolls back singleton; retry and successful reuse\n";
}
'''
cpp = build / 'service_init.cpp'
cpp.write_text(code)
exe = build / 'service_init'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-g', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                '-I' + str(root / 'src/libs/core/include'), str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
