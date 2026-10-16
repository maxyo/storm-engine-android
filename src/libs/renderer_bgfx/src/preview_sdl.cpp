#include "fog_preview.hpp"
#include "fog_frame_owner.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace storm::render {
namespace {
std::vector<uint8_t> shaderBytes(const std::string& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    const auto size=file.tellg();
    if(!file||size<=0||size>16*1024*1024) throw std::runtime_error("Missing/invalid bgfx preview shader: "+path);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),size)) throw std::runtime_error("Cannot read bgfx preview shader");
    return bytes;
}
FogSurface surfaceFor(SDL_Window* window,const FogPreviewOptions& options) {
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window,&info)) throw std::runtime_error(SDL_GetError());
    FogSurface surface;
    surface.vsync=options.vsync;
    int width,height; SDL_GetWindowSize(window,&width,&height);
    surface.width=uint32_t(std::max(1,width)); surface.height=uint32_t(std::max(1,height));
    switch(info.subsystem) {
#if defined(SDL_VIDEO_DRIVER_ANDROID)
    case SDL_SYSWM_ANDROID: surface.nativeWindow=info.info.android.window; break;
#endif
#if defined(SDL_VIDEO_DRIVER_X11)
    case SDL_SYSWM_X11:
        surface.nativeDisplay=info.info.x11.display;
        surface.nativeWindow=reinterpret_cast<void*>(uintptr_t(info.info.x11.window)); break;
#endif
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    case SDL_SYSWM_WINDOWS: surface.nativeWindow=info.info.win.window; break;
#endif
    default: throw std::runtime_error("bgfx fog preview supports Android, X11 and Windows surfaces only");
    }
    return surface;
}
}
int runFogPreview(const FogPreviewOptions& options) {
    try {
        auto snapshot=readFogSnapshot(options.snapshotPath);
        auto vs=shaderBytes(options.shaderDirectory+"/vs_fog.bin");
        auto fs=shaderBytes(options.shaderDirectory+"/fs_fog.bin");
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Storm bgfx fog preview - not full game",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                int(std::clamp(options.width,320u,8192u)),int(std::clamp(options.height,180u,8192u)),
                SDL_WINDOW_VULKAN|SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);
        if(!window) throw std::runtime_error(SDL_GetError());
        // Declared after window: renderer is always destroyed before its surface.
        auto owner=std::make_unique<FogFrameOwner>(surfaceFor(window.get(),options),snapshot,vs,fs);
        bool running=true; uint32_t frames=0;
        while(running) {
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                if(event.type==SDL_QUIT||event.type==SDL_APP_TERMINATING) running=false;
                if(event.type==SDL_KEYUP && (event.key.keysym.sym==SDLK_ESCAPE||event.key.keysym.sym==SDLK_AC_BACK)) running=false;
                if(event.type==SDL_APP_WILLENTERBACKGROUND) {
                    owner.reset();
                    std::cout<<"bgfx preview: released surface renderer for background\n";
                }
                if(event.type==SDL_APP_DIDENTERFOREGROUND && !owner) {
                    owner=std::make_unique<FogFrameOwner>(surfaceFor(window.get(),options),snapshot,vs,fs);
                    std::cout<<"bgfx preview: recreated surface renderer for foreground\n";
                }
                if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED && owner)
                    owner->resize(uint32_t(std::max(0,event.window.data1)),uint32_t(std::max(0,event.window.data2)));
            }
            if(!running) break;
            if(owner) {
                owner->frame(options.fogEnabled);
                if(options.frameLimit && ++frames>=options.frameLimit) break;
            } else SDL_Delay(25);
        }
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"bgfx fog preview: "<<error.what()<<'\n'; return 1;
    }
}
}
