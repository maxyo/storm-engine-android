#pragma once

#include "dx9render.h"
#include "vma.hpp"

class xiBaseVideo : public Entity
{
  public:
    virtual void SetShowVideo(bool bShowVideo)
    {
    }

    // Renderer texture id of the current frame, -1 if there is none.
    virtual int32_t GetCurrentVideoTexture()
    {
        return -1;
    }
};
