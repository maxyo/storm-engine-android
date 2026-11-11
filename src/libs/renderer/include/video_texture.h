#pragma once
#include "dx9render.h"

//-----------------------------------------------------------------------------
// Name: class VideoToTexture
// Desc: play video into texture
//-----------------------------------------------------------------------------
class CVideoTexture : public Entity
{
  public:
    // Returns the renderer texture id it draws into, -1 on failure.
    virtual int32_t Initialize(VRenderer *pRS, const char *sFileName, bool bCicled) = 0;
    virtual bool FrameUpdate() = 0;
    virtual void Release() = 0;
    int32_t m_texture = -1;
};
