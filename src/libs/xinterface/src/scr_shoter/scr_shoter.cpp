#include "scr_shoter.h"
#include "../xdefines.h"

#include "core.h"
#include "string_compare.hpp"

#define SS_TEXTURE_WIDTH 128
#define SS_TEXTURE_HEIGHT 128
#define SS_TEXTURE_FONECOLOR 0xFF000000

uint32_t GetA8R8G8B8_FromFMT(void *p, uint32_t fmt)
{
    uint32_t retVal;

    if (fmt == D3DFMT_R5G6B5)
    {
        retVal = 0xFF000000 | (static_cast<uint32_t>(*static_cast<uint16_t *>(p) & 0xF800) << 8) |
                 (static_cast<uint32_t>(*static_cast<uint16_t *>(p) & 0x7E0) << 5) |
                 (static_cast<uint32_t>(*static_cast<uint16_t *>(p) & 0x1F) << 3);
    }
    else
    {
        retVal = 0xFF000000 | (*static_cast<uint32_t *>(p));
    }

    return retVal;
}

SCRSHOTER::~SCRSHOTER()
{
    if (textureIndex_ != -1 && rs != nullptr)
        rs->TextureRelease(textureIndex_);
    SAVETEXTURES *pst;
    while (m_list)
    {
        pst = m_list;
        m_list = pst->next;
        delete pst->fileName;
        if (pst->textureId != -1 && rs != nullptr)
            rs->TextureRelease(pst->textureId);
        delete pst;
    }
}

bool SCRSHOTER::Init()
{
    // GUARD(SCRSHOTER::Init())
    SetDevice();
    // UNGUARD
    return true;
}

void SCRSHOTER::SetDevice()
{
    // get render service
    rs = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
    if (!rs)
        throw std::runtime_error("No service: dx9render");
}

void SCRSHOTER::Execute(uint32_t Delta_Time)
{
}

void SCRSHOTER::Realize(uint32_t Delta_Time)
{
    if (textureIndex_ < 0)
        if (!MakeScreenShot())
        {
            core.Trace("ERROR!!! screen shot create error");
            core.Event("makescrshot");
        }
        else
            core.Event("makescrshot");
}

bool SCRSHOTER::MakeScreenShot()
{
    D3DLOCKED_RECT inRect, outRect;

    auto hr = D3D_OK;

    // make the video card draw all unfinished tasks
    hr = rs->EndScene();
    if (hr != D3D_OK)
    {
        core.Trace("ERROR!!! Can`t EndScene");
        return false;
    }
    hr = rs->BeginScene();
    if (hr != D3D_OK)
    {
        core.Trace("ERROR!!! Can`t BeginScene");
        return false;
    }

    // delete the old screen shot
    if (textureIndex_ != -1 && rs != nullptr)
    {
        rs->TextureRelease(textureIndex_);
    }
    textureIndex_ = -1;

    // a copy of the frame, scaled down into the shot
    const int32_t frame = rs->GetRenderTargetAsTexture();
    if (frame < 0)
        return false;
    D3DSURFACE_DESC desc{};
    textureIndex_ = rs->TextureCreate(SS_TEXTURE_WIDTH, SS_TEXTURE_HEIGHT, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
    bool ok = textureIndex_ >= 0 && rs->TextureLevelDesc(frame, 0, desc) &&
              rs->TextureLock(frame, 0, inRect, D3DLOCK_READONLY);
    if (ok)
    {
        ok = rs->TextureLock(textureIndex_, 0, outRect, 0);
        if (ok)
        {
            for (int vi = 0; vi < SS_TEXTURE_HEIGHT; vi++)
            {
                const size_t vertical_offset = vi * desc.Height / SS_TEXTURE_HEIGHT;
                auto *const pInPxl = static_cast<uint8_t *>(inRect.pBits) + inRect.Pitch * vertical_offset;
                auto *pOutPxl = (uint32_t *)(static_cast<uint8_t *>(outRect.pBits) + outRect.Pitch * vi);
                for (int hi = 0; hi < SS_TEXTURE_WIDTH; hi++)
                {
                    const size_t horizontal_offset = (hi * desc.Width / SS_TEXTURE_WIDTH) * (inRect.Pitch / desc.Width);
                    pOutPxl[hi] = GetA8R8G8B8_FromFMT(&pInPxl[horizontal_offset], desc.Format);
                }
            }
            rs->TextureUnlock(textureIndex_, 0);
        }
        rs->TextureUnlock(frame, 0);
    }
    rs->TextureRelease(frame);
    if (!ok)
    {
        TEXTURE_RELEASE(rs, textureIndex_);
        return false;
    }

    // Add a texture with a frame to the shot
    const int nTextureID = rs->TextureCreate("interfaces\\EmptyBorder.tga");
    if (nTextureID >= 0)
    {
        const int32_t framed = rs->TextureCreate(SS_TEXTURE_WIDTH, SS_TEXTURE_HEIGHT, 1, D3DUSAGE_RENDERTARGET,
                                                 D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
        if (framed >= 0)
        {
            const uint32_t BI_SCRSHOTER_VERTEX_FORMAT = (D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXTUREFORMAT2);
            struct BI_SCRSHOTER_VERTEX
            {
                glm::vec3 pos;
                float w;
                float tu, tv;
            } vert[4];
            for (auto i = 0; i < 4; i++)
            {
                vert[i].pos.z = 1.f;
                vert[i].w = 0.5f;
            }
            vert[1].pos.x = vert[0].pos.x = 0;
            vert[2].pos.y = vert[0].pos.y = 0;
            vert[3].pos.x = vert[2].pos.x = SS_TEXTURE_WIDTH;
            vert[3].pos.y = vert[1].pos.y = SS_TEXTURE_HEIGHT;
            vert[1].tu = vert[0].tu = 0;
            vert[2].tv = vert[0].tv = 0;
            vert[3].tu = vert[2].tu = 1.f;
            vert[3].tv = vert[1].tv = 1.f;

            rs->PushRenderTarget();
            if (rs->SetRenderTarget(framed, 0, 0, {}))
            {
                rs->TextureSet(0, textureIndex_);
                rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, BI_SCRSHOTER_VERTEX_FORMAT, 2, vert,
                                    sizeof(BI_SCRSHOTER_VERTEX), "battle_icons");
                rs->TextureSet(0, nTextureID);
                rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, BI_SCRSHOTER_VERTEX_FORMAT, 2, vert,
                                    sizeof(BI_SCRSHOTER_VERTEX), "battle_icons");
                // the framed shot replaces the plain one (a readable copy of the render target)
                if (const int32_t shot = rs->GetRenderTargetAsTexture(); shot >= 0)
                {
                    rs->TextureRelease(textureIndex_);
                    textureIndex_ = shot;
                }
            }
            rs->PopRenderTarget();
            rs->TextureRelease(framed);
        }

        rs->TextureRelease(nTextureID);
    }

    return true;
}

uint64_t SCRSHOTER::ProcessMessage(MESSAGE &message)
{
    switch (message.Long())
    {
    case MSG_SCRSHOT_MAKE:
        // Normally Realize has taken the shot already. A save made outside the
        // frame loop (autosave when the app goes to background) asks before
        // any Realize ran: take it now from the last rendered back buffer.
        if (textureIndex_ < 0 && !MakeScreenShot())
        {
            core.Trace("ERROR!!! screen shot create error");
        }
        return textureIndex_;
    case MSG_SCRSHOT_READ: {
        VDATA *pvdat;

        const std::string &param = message.String();
        const std::string &param2 = message.String();
        pvdat = message.ScriptVariablePointer();

        int32_t pRetTex = AddSaveTexture(param.c_str(), param2.c_str());
        auto *const strDat = FindSaveData(param2.c_str());
        if (pvdat)
            if (!strDat)
            {
                pvdat->Set("\0");
            }
            else
            {
                pvdat->Set(strDat);
            }
        return pRetTex;
    }
    break;
    case MSG_SCRSHOT_RELEASE: {
        const std::string &param = message.String();
        DelSaveTexture(param.c_str());
    }
    break;
    }
    return 0;
}

int32_t SCRSHOTER::FindSaveTexture(const char *fileName) const
{
    if (!fileName)
        return -1;
    auto *ps = m_list;
    while (ps)
    {
        if (ps->fileName && storm::iEquals(fileName, ps->fileName))
            return ps->textureId;
        ps = ps->next;
    }
    return -1;
}

char *SCRSHOTER::FindSaveData(const char *fileName) const
{
    if (!fileName)
        return nullptr;
    SAVETEXTURES *ps = m_list;
    while (ps)
    {
        if (ps->fileName && storm::iEquals(fileName, ps->fileName))
            return ps->dataString;
        ps = ps->next;
    }
    return nullptr;
}

int32_t SCRSHOTER::AddSaveTexture(const char *dirName, const char *fileName)
{
    if (fileName == nullptr)
        return -1;
    int32_t rval = FindSaveTexture(fileName);
    if (rval != -1)
        return rval;
    if (storm::iEquals(fileName, "newsave"))
        return textureIndex_;
    auto *ps = new SAVETEXTURES;
    if (ps == nullptr)
    {
        throw std::runtime_error("Allocate memory error");
    }
    ps->dataString = nullptr;
    ps->next = m_list;
    m_list = ps;
    const auto len = strlen(fileName) + 1;
    m_list->fileName = new char[len];
    if (m_list->fileName == nullptr)
    {
        throw std::runtime_error("Allocate memory error");
    }
    memcpy(m_list->fileName, fileName, len);
    char param[1024];
    if (dirName == nullptr || dirName[0] == 0)
        sprintf_s(param, "%s", fileName);
    else
        sprintf_s(param, "%s\\%s", dirName, fileName);
    m_list->textureId = GetTexFromSave(param, &ps->dataString);
    return m_list->textureId;
}

void SCRSHOTER::DelSaveTexture(const char *fileName)
{
    if (!fileName)
        return;
    SAVETEXTURES *oldps = nullptr;
    SAVETEXTURES *ps = m_list;
    while (ps)
    {
        if (ps->fileName && storm::iEquals(fileName, ps->fileName))
        {
            if (oldps)
                oldps->next = ps->next;
            else
                m_list = ps->next;
            delete ps->fileName;
            delete ps->dataString;
            if (ps->textureId != -1 && rs != nullptr)
                rs->TextureRelease(ps->textureId);
            delete ps;
            return;
        }
        oldps = ps;
        ps = ps->next;
    }
}

int32_t SCRSHOTER::GetTexFromSave(char *fileName, char **pDatStr) const
{
    bool ok = true;
    D3DLOCKED_RECT outRect;
    int32_t textureId = -1;
    *pDatStr = nullptr; //~!~

    int32_t datSize = 0;
    char *pdat = nullptr;
    pdat = static_cast<char *>(core.GetSaveData(fileName, datSize));
    int32_t startIdx = 0;
    int32_t texSize = 0;
    if (pdat != nullptr && datSize > sizeof(SAVE_DATA_HANDLE))
    {
        startIdx = ((SAVE_DATA_HANDLE *)pdat)->StringDataSize + sizeof(SAVE_DATA_HANDLE);
        texSize = ((SAVE_DATA_HANDLE *)pdat)->SurfaceDataSize;
    }
    if (datSize > sizeof(SAVE_DATA_HANDLE) && datSize == startIdx + texSize &&
        texSize == 4 * SS_TEXTURE_WIDTH * SS_TEXTURE_HEIGHT)
    {
        textureId = rs->TextureCreate(SS_TEXTURE_WIDTH, SS_TEXTURE_HEIGHT, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
        ok = textureId >= 0 && rs->TextureLock(textureId, 0, outRect, 0);
        if (ok)
        {
            memcpy(outRect.pBits, &pdat[startIdx], texSize);
            rs->TextureUnlock(textureId, 0);
        }
        char *stringData = &pdat[sizeof(SAVE_DATA_HANDLE)];
        if (!utf8::IsValidUtf8(stringData))
        {
            utf8::FixInvalidUtf8(stringData);
        }
        const int strLen = ((SAVE_DATA_HANDLE *)pdat)->StringDataSize;
        *pDatStr = new char[strLen + 1];
        if (!*pDatStr)
        {
            throw std::runtime_error("allocate memory error");
        }
        strncpy_s(*pDatStr, strLen + 1, stringData, strLen);
        (*pDatStr)[strLen] = 0;
    }
    /*    else
      {
        int idx=0;
        for(int i=0; i<SS_TEXTURE_HEIGHT; i++)
          for(int j=0; j<SS_TEXTURE_WIDTH; j++)
          {
            ((DWORD*)outRect.pBits)[idx] = 0xFF000000;
            idx++;
          }
      }*/
    delete pdat;

    if (ok)
    {
        return textureId;
    }
    if (*pDatStr)
        delete (*pDatStr);
    *pDatStr = nullptr;
    TEXTURE_RELEASE(rs, textureId);
    return -1;
}
