#include "t_splash.h"
#include "ball_splash_defines.h"
#include <memory>
#include <iostream>
#include "production.inc"

void cohort(VDX9RENDER &renderer)
{
    const int before=renderer.created;
    auto first=std::make_unique<TSplash>();
    auto last=std::make_unique<TSplash>();
    first->Initialize(nullptr,nullptr,nullptr,&renderer);
    last->Initialize(nullptr,nullptr,nullptr,&renderer);
    assert(renderer.created==before+6); // one shared allocation, not per splash
    first.reset();
    assert(renderer.released==before); // another splash still uses the buffers
    last.reset();
    assert(renderer.released==renderer.created);
}

int main()
{
    auto first=std::make_unique<VDX9RENDER>();
    // Allocate the replacement before retiring the old service, ensuring the
    // addresses differ. The old production code dereferences freed first here.
    auto second=std::make_unique<VDX9RENDER>();
    cohort(*first);
    cohort(*first); // scene reload while the renderer remains alive
    first.reset();
    cohort(*second); // second SDL_main creates a different renderer service
    cohort(*second);
    second.reset();
    auto third=std::make_unique<VDX9RENDER>();
    cohort(*third);
    std::cout<<"PASS production TSplash shared ownership, scene reload and renderer restart\n";
}
