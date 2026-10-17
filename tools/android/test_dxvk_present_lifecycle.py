#!/usr/bin/env python3
"""Compile actual candidate PresentImage/SynchronizePresent against bounded WSI mocks."""
from pathlib import Path
import subprocess,tempfile,argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dxvk-source',type=Path,required=True)
parser.add_argument('--cxx',default='c++')
args=parser.parse_args()
r=args.dxvk_source.resolve()
s=(r/'src/d3d9/d3d9_swapchain.cpp').read_text()
a=s.index('  void D3D9SwapChainEx::PresentImage(');b=s.index('  void D3D9SwapChainEx::SubmitPresent(',a)
c=s.index('  void D3D9SwapChainEx::SynchronizePresent()');d=s.index('  void D3D9SwapChainEx::RecreateSwapChain(',c)
code=r'''
#include <cassert>
#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <vector>
#include <string>
#include <iostream>
#include "d3d9_present_extent.h"
using UINT=unsigned;using VkResult=int;
constexpr int VK_SUCCESS=0,VK_SUBOPTIMAL_KHR=1,VK_ERROR_OUT_OF_DATE_KHR=-1,VK_ERROR_SURFACE_LOST_KHR=-2,VK_ERROR_DEVICE_LOST=-3,VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT=-4;
struct VkExtent2D{uint32_t width=1280,height=720;};
struct VkRect2D{struct {int32_t x,y;} offset; VkExtent2D extent;};
namespace vk {struct PresenterInfo{int format=0;VkExtent2D imageExtent;};struct PresenterSync{};}
namespace str{template<class...T>std::string format(T...){return "WSI failure";}}
using DxvkError=std::runtime_error;
enum class D3D9DeviceFlag{DirtyFramebuffer};
struct Flags{void set(D3D9DeviceFlag){}};
struct Parent{Flags m_flags;void Flush(){}};
struct Image{~Image(){}};
struct Buffer{Buffer* GetCommonTexture(){return this;}Image GetImage(){return {};}int GetImageView(bool){return 0;}void Swap(Buffer*){}};
struct Device{std::deque<int> results;int waitForSubmission(int*){if(results.empty())return 0;int s=results.front();results.pop_front();return s;}int createCommandList(){return 0;}};
struct Context{std::vector<uint64_t> signals;void beginRecording(int){};void signal(int,uint64_t f){signals.push_back(f);}Context* ptr(){return this;}};
struct Blitter{void presentImage(Context*,int,VkRect2D,int,VkRect2D){}};
struct Hud{void render(Context*,int,VkExtent2D){}};
struct Presenter{bool alive=true;int acquires=0;std::deque<int> results;
 bool hasSwapChain(){return alive;}vk::PresenterInfo info(){return {};}
 int acquireNextImage(vk::PresenterSync&,uint32_t& i){if(!alive)throw std::out_of_range("empty semaphores");++acquires;i=0;if(results.empty())return 0;int s=results.front();results.pop_front();return s;}};
namespace dxvk {
class D3D9SwapChainEx {
public:
 Parent parent;Parent* m_parent=&parent;
 Presenter presenter;Presenter* m_presenter=&presenter;
 Device device;Device* m_device=&device;
 Context context;Context* m_context=&context;
 Blitter blitter;Blitter* m_blitter=&blitter;
 Hud* m_hud=nullptr;
 std::vector<std::shared_ptr<Buffer>> m_backBuffers{std::make_shared<Buffer>()};
 std::vector<int> m_imageViews{0};
 struct Rect{int left=0,top=0,right=1280,bottom=720;}m_srcRect,m_dstRect;
 uint64_t m_frameId=10;int m_frameLatencySignal=0,m_presentStatus=0,m_vsync=0;
 bool m_dirty=false,m_presentToSurface=true;int recreates=0,submits=0,latencyWaits=0;
 std::deque<bool> recreatedAlive;
 void RecreateSwapChain(int){++recreates;assert(!recreatedAlive.empty());presenter.alive=recreatedAlive.front();recreatedAlive.pop_front();m_presentStatus=0;}
 void SubmitPresent(vk::PresenterSync&,uint32_t){++submits;}
 void SyncFrameLatency(){++latencyWaits;assert(!context.signals.empty());assert(context.signals.back()==m_frameId);}
 void PresentImage(UINT);void SynchronizePresent();
};
'''+s[a:b]+s[c:d]+r'''
}
int main(){
 using Chain=dxvk::D3D9SwapChainEx;
 {Chain c;c.device.results={VK_ERROR_OUT_OF_DATE_KHR};c.recreatedAlive={false};c.PresentImage(1);
 assert(c.presenter.acquires==0&&c.submits==0&&c.latencyWaits==0&&c.m_frameId==10&&c.m_dirty);
 // Next public Present's !hasSwapChain path recreates after the view returns.
 c.recreatedAlive={true};c.RecreateSwapChain(0);c.PresentImage(1);
 assert(c.presenter.acquires==1&&c.m_frameId==11&&c.context.signals.back()==11);}
 {Chain c;c.presenter.results={VK_ERROR_OUT_OF_DATE_KHR};c.recreatedAlive={false};c.PresentImage(1);
 assert(c.presenter.acquires==1&&c.submits==0&&c.m_dirty&&c.m_frameId==10);}
 {Chain c;c.device.results={VK_SUCCESS,VK_ERROR_OUT_OF_DATE_KHR};c.recreatedAlive={false};c.PresentImage(3);
 assert(c.submits==1&&c.latencyWaits==0&&c.m_frameId==10&&c.context.signals.empty());
 c.recreatedAlive={true};c.RecreateSwapChain(0);c.PresentImage(3);
 assert(c.submits==4&&c.m_frameId==11&&c.context.signals.back()==11);}
 {Chain c;for(int i=0;i<32;++i){c.presenter.alive=true;c.presenter.results={VK_ERROR_OUT_OF_DATE_KHR};c.recreatedAlive={false};c.PresentImage(1);}
 assert(c.m_frameId==10&&c.latencyWaits==0&&c.context.signals.empty());}
 for(int failure:{VK_ERROR_DEVICE_LOST,-99}){
  Chain c;c.presenter.results={failure};bool failed=false;try{c.PresentImage(1);}catch(const DxvkError&){failed=true;}
  assert(failed&&c.recreates==0&&c.submits==0);
  Chain d;d.device.results={failure};failed=false;try{d.PresentImage(1);}catch(const DxvkError&){failed=true;}
  assert(failed&&d.recreates==0&&d.presenter.acquires==0);
 }
 for(int recoverable:{VK_ERROR_OUT_OF_DATE_KHR,VK_ERROR_SURFACE_LOST_KHR,VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT}){
  Chain c;c.presenter.results={recoverable,VK_SUCCESS};c.recreatedAlive={true};c.PresentImage(1);
  assert(c.presenter.acquires==2&&c.recreates==1&&c.submits==1&&c.m_frameId==11);
  Chain d;d.device.results={recoverable};d.recreatedAlive={true};d.PresentImage(1);
  assert(d.recreates==1&&d.submits==1&&d.m_frameId==11);
 }
 {Chain c;c.presenter.results={VK_SUBOPTIMAL_KHR};c.PresentImage(0);assert(c.submits==1&&c.m_frameId==11);}
 std::cout<<"actual PresentImage/SynchronizePresent: suspended acquire/partial interval/retry/fatal status PASS\n";
}
'''
# The actual body calls context.ptr(), so retain the pointer API via this local
# syntactic adapter only; no production control-flow or conditions are copied.
code=code.replace('m_context.ptr()', 'm_context->ptr()').replace('m_backBuffers[i - 1].ptr()', 'm_backBuffers[i - 1].get()')
with tempfile.TemporaryDirectory(prefix='storm-present-lifecycle-') as t:
 p=Path(t)/'check.cpp';p.write_text(code)
 for android in (False,True):
  exe=Path(t)/('android' if android else 'desktop')
  command=[args.cxx,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(r/'src/d3d9')]
  if android: command.append('-D__ANDROID__')
  subprocess.run(command+[str(p),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
