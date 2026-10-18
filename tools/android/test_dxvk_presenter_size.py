#!/usr/bin/env python3
"""Compile actual candidate header and pickImageExtent body; no GPU/device/build cache."""
from pathlib import Path
import subprocess
import tempfile
import argparse
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dxvk-source', type=Path, required=True)
parser.add_argument('--cxx', default='c++')
args = parser.parse_args()
root = args.dxvk_source.resolve()
source = (root/'src/vulkan/vulkan_presenter.cpp').read_text()
a = source.index('  VkExtent2D Presenter::pickImageExtent(')
b = source.index('  uint32_t Presenter::pickImageCount(', a)
function = source[a:b].replace('Presenter::pickImageExtent', 'pickImageExtent')
code = r'''
#include <algorithm>
#include <limits>
#include <cassert>
#include <cstdint>
#include <iostream>
#include "vulkan_android_extent.h"
struct VkExtent2D {uint32_t width,height;};
struct VkSurfaceCapabilitiesKHR {
 VkExtent2D currentExtent,minImageExtent,maxImageExtent;
 uint32_t currentTransform,supportedTransforms;
};
constexpr uint32_t VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR=1;
namespace dxvk::vk { using std::clamp;
''' + function + r'''
}
int main() {
 using dxvk::vk::pickImageExtent;
 VkSurfaceCapabilitiesKHR caps{{960,540},{1,1},{4096,4096},1,1};
 auto eq=[](VkExtent2D a,uint32_t w,uint32_t h){assert(a.width==w&&a.height==h);};
 // Desktop and explicit destination rectangles retain legacy behavior.
 eq(pickImageExtent(caps,{1280,720},false),960,540);
#ifdef __ANDROID__
 eq(pickImageExtent(caps,{1280,720},true),1280,720); // restore, stale small preferred size
 caps.currentExtent={1280,720};
 eq(pickImageExtent(caps,{960,540},true),960,540);   // shrink, stale large preferred size
 caps.currentExtent={960,540};
 eq(pickImageExtent(caps,{960,540},true),960,540);   // settled compact
 caps.currentTransform=2;
 eq(pickImageExtent(caps,{1280,720},true),960,540); // rotation falls back, not silently swapped
 caps.currentTransform=1;caps.supportedTransforms=2;
 eq(pickImageExtent(caps,{1280,720},true),960,540); // unsupported identity falls back
 caps.supportedTransforms=1;
 eq(pickImageExtent(caps,{8192,2048},true),4096,2048);
 caps.minImageExtent={320,240};
 eq(pickImageExtent(caps,{160,120},true),320,240);
 eq(pickImageExtent(caps,{0,720},true),0,0);
 caps.currentExtent={0,0};
 eq(pickImageExtent(caps,{1280,720},true),0,0); // don't create zero-extent swapchain
 caps.currentExtent={960,540};caps.maxImageExtent={0,0};
 eq(pickImageExtent(caps,{1280,720},true),0,0);
 caps.maxImageExtent={100,100};
 eq(pickImageExtent(caps,{1280,720},true),0,0); // invalid min/max guarded before clamp
 caps.maxImageExtent={4096,4096};caps.minImageExtent={1,1};
 caps.currentExtent={UINT32_MAX,UINT32_MAX};
 eq(pickImageExtent(caps,{1280,720},true),1280,720);
#else
 eq(pickImageExtent(caps,{1280,720},true),960,540); // flag cannot change desktop path
 caps.currentExtent={UINT32_MAX,UINT32_MAX};
 eq(pickImageExtent(caps,{1280,720},true),1280,720);
#endif
 std::cout<<"candidate actual pickImageExtent + production header PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='storm-presenter-size-') as d:
    cpp = Path(d)/'test.cpp'; cpp.write_text(code)
    for android in (False, True):
        binary = Path(d)/('android' if android else 'desktop')
        command = [args.cxx,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(root/'src/vulkan')]
        if android: command.append('-D__ANDROID__')
        subprocess.run(command+[str(cpp),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
