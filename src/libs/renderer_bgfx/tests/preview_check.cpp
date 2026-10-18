#include "fog_preview.hpp"
#include <SDL2/SDL.h>
#include <bx/math.h>
#include <filesystem>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2 || SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0) return 2;
    const auto path=(std::filesystem::temp_directory_path()/"storm-fog-preview.bin").string();
    storm::render::FogSnapshot snapshot{};
    snapshot.mesh=storm::weather::makeFogSphere([](float,float,float){return 0x80804020u;});
    bx::mtxIdentity(snapshot.world.data());
    bx::mtxLookAt(snapshot.view.data(),bx::Vec3{0,0,0},bx::Vec3{0,1200,2000});
    bx::mtxProj(snapshot.projection.data(),40.f,1.f,0.1f,8000.f,false);
    storm::render::writeFogSnapshot(path,snapshot);
    storm::render::FogPreviewOptions options;
    options.snapshotPath=path; options.shaderDirectory=argv[1]; options.width=320; options.height=320; options.frameLimit=8;
    int result=0;
    for(int run=0;run<2 && !result;++run) {
        SDL_Event event{};
        event.type=SDL_APP_WILLENTERBACKGROUND; SDL_PushEvent(&event);
        event.type=SDL_APP_DIDENTERFOREGROUND; SDL_PushEvent(&event);
        event.type=SDL_WINDOWEVENT; event.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
        event.window.data1=400; event.window.data2=240; SDL_PushEvent(&event);
        result=storm::render::runFogPreview(options);
        std::cout<<"Exclusive SDL/bgfx owner run "<<run<<": "<<result<<'\n';
    }
    std::filesystem::remove(path); SDL_Quit(); return result;
}
