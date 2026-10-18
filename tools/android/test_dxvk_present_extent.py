#!/usr/bin/env python3
"""Host-check patched DXVK's Android full-surface presentation coordinate policy.

Use --dxvk-source with an isolated checkout containing dxvk-native-arm64.patch.
No Vulkan device is needed. This is not a hot-resize runtime acceptance test.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dxvk-source", type=Path, required=True)
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    # Compile the production policy header, rather than a copied formula.
    harness = r'''
#include "d3d9_present_extent.h"
#include <cassert>
#include <cstdint>
#include <iostream>
struct Extent { uint32_t width, height; };
struct Offset { int32_t x, y; };
struct Rect { Offset offset; Extent extent; };
void full(Extent window, Extent acquired) {
    const Rect requested{{0,0},window};
    const auto result=dxvk::presentSurfaceRegion(requested,acquired,true);
    assert(result.offset.x==0 && result.offset.y==0);
    assert(result.extent.width==acquired.width && result.extent.height==acquired.height);
    // Android maps acquired buffer dimensions onto the view exactly once.
    // This must fill the view whether the backing buffer was resized or retained.
    assert(uint64_t(result.extent.width)*window.width/acquired.width==window.width);
    assert(uint64_t(result.extent.height)*window.height/acquired.height==window.height);
}
int main() {
    full({1280,720},{1280,720}); // unchanged
    full({960,540},{1280,720});  // observed retained-buffer hot shrink
    full({960,540},{960,540});   // fresh compact startup / recreated backing
    full({1280,720},{960,540});  // reverse resize, retained small backing
    full({720,1280},{1280,720}); // orientation mismatch during surface transition
    // Source crop/render resolution are not inputs to this coordinate policy.
    const Rect explicitRect{{37,19},{400,200}};
    const auto result=dxvk::presentSurfaceRegion(explicitRect,Extent{1280,720},false);
    assert(result.offset.x==37 && result.offset.y==19);
    assert(result.extent.width==400 && result.extent.height==200);
    const Rect empty{{0,0},{0,0}};
    assert(dxvk::presentSurfaceRegion(empty,Extent{1280,720},false).extent.width==0);
    // Old logical-destination policy reproduces exactly the reported 720x405.
    assert(uint64_t(960)*960/1280==720 && uint64_t(540)*540/720==405);
    std::cout<<"DXVK production present region: hot/fresh/reverse/orientation/explicit PASS\n";
}
'''
    with tempfile.TemporaryDirectory(prefix="storm-dxvk-present-") as directory:
        source = Path(directory) / "present.cpp"
        binary = Path(directory) / "present-check"
        source.write_text(harness)
        subprocess.run([args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(args.dxvk_source.resolve() / "src/d3d9"),
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
