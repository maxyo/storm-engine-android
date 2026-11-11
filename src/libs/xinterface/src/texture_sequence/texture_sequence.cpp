#include "texture_sequence.h"
#include "vma.hpp"

#include "core.h"

#include "v_file_service.h"

#define FILE_PATH "TextureSequence\\%s.tga"
static const char *INI_FILENAME = "resource\\ini\\TextureSequence.ini";

#define TS_VERTEX_FRMT (D3DFVF_XYZRHW | D3DFVF_TEX2 | D3DFVF_TEXTUREFORMAT2)

struct TS_VERTEX
{
    glm::vec3 pos;
    float w;
    float tu1, tv1;
    float tu2, tv2;
};

void GetRectFromNum(FRECT &texRect, int xq, int yq, int curNum, bool bHorzFlip, bool bVertFlip)
{
    auto y = curNum / xq;
    if (y >= yq)
        y = 0;
    auto x = curNum - y * xq;
    if (x >= xq)
        x = 0;

    const auto width = 1.f / xq;
    const auto height = 1.f / yq;

    if (bHorzFlip)
        texRect.left = width + (texRect.right = x * width);
    else
        texRect.right = width + (texRect.left = x * width);
    if (bVertFlip)
        texRect.top = height + (texRect.bottom = y * height);
    else
        texRect.bottom = height + (texRect.top = y * height);
}

TextureSequence::TextureSequence()
{
    m_AllTex = -1;
    m_bHorzFlip = false;
    m_bVertFlip = false;
}

TextureSequence::~TextureSequence()
{
    Release();
}

int32_t TextureSequence::Initialize(VDX9RENDER *pRS, const char *cTSfileName, bool bCicled)
{
    m_bCicled = bCicled;
    if (pRS == nullptr || cTSfileName == nullptr)
        return -1;
    m_pRS = pRS;

    // open ini file
    auto ini = fio->OpenIniFile(INI_FILENAME);
    if (!ini)
    {
        core.Trace("ini file %s not found!", INI_FILENAME);
        return -1;
    }
    m_dwDeltaTime = ini->GetInt((char *)cTSfileName, "timeDelay", 128);
    if (m_dwDeltaTime == 0)
    {
        return -1;
    }
    m_texWidth = ini->GetInt((char *)cTSfileName, "width", 128);
    m_texHeight = ini->GetInt((char *)cTSfileName, "height", 128);
    m_xQuantity = ini->GetInt((char *)cTSfileName, "horzQ", 1);
    m_yQuantity = ini->GetInt((char *)cTSfileName, "vertQ", 1);
    m_maxCurNum = m_xQuantity * m_yQuantity;
    if (m_maxCurNum == 0)
    {
        return -1;
    }

    // load sequence texture
    char fullName[256];
    if (!ini->ReadString((char *)cTSfileName, "TextureFile", fullName, sizeof(fullName) - 1, ""))
        sprintf_s(fullName, FILE_PATH, cTSfileName);
    m_AllTex = m_pRS->TextureCreate(fullName);
    if (m_AllTex == -1)
    {
        return -1;
    }

    m_bHorzFlip = ini->GetInt((char *)cTSfileName, "flipH", 0) != 0;
    m_bVertFlip = ini->GetInt((char *)cTSfileName, "flipV", 0) != 0;

    // create output texture
    m_texture = m_pRS->TextureCreate(m_texWidth, m_texHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT);
    if (m_texture < 0)
        return -1;

    // first render
    m_curNum = 0;
    m_dwCurDeltaTime = 0;
    // ToTextureRender(0.f);

    return m_texture;
}

//-----------------------------------------------------------------------------
// Name: FrameUpdate
// Desc: Performs per-frame updates
//-----------------------------------------------------------------------------
bool TextureSequence::FrameUpdate()
{
    m_dwCurDeltaTime += core.GetRDeltaTime();
    while (m_dwCurDeltaTime > m_dwDeltaTime)
    {
        m_dwCurDeltaTime -= m_dwDeltaTime;
        m_curNum++;
        if (m_curNum >= m_maxCurNum)
            m_curNum = 0;
    }

    ToTextureRender(static_cast<float>(m_dwCurDeltaTime) / static_cast<float>(m_dwDeltaTime));
    return true;
}

void TextureSequence::ToTextureRender(float blendValue) const
{
    const auto newTFactor = ARGB(static_cast<int32_t>(255.f * blendValue), static_cast<int32_t>(255.f * blendValue),
                                 static_cast<int32_t>(255.f * blendValue), static_cast<int32_t>(255.f * blendValue));

    // set texture as render target
    m_pRS->PushRenderTarget();
    if (m_pRS->SetRenderTarget(m_texture, 0, 0, {}))
    {
        glm::mat4 matw;
        m_pRS->SetTransform(D3DTS_WORLD, (D3DMATRIX *)&matw);
        m_pRS->TextureSet(0, m_AllTex);
        m_pRS->TextureSet(1, m_AllTex);

        FRECT m_rectTex;
        TS_VERTEX v[4];
        for (auto i = 0; i < 4; i++)
        {
            v[i].w = 0.5f;
            v[i].pos.z = 1.f;
        }
        v[0].pos.x = 0;
        v[0].pos.y = 0;
        v[1].pos.x = 0;
        v[1].pos.y = static_cast<float>(m_texHeight);
        v[2].pos.x = static_cast<float>(m_texWidth);
        v[2].pos.y = 0;
        v[3].pos.x = static_cast<float>(m_texWidth);
        v[3].pos.y = static_cast<float>(m_texHeight);

        GetRectFromNum(m_rectTex, m_xQuantity, m_yQuantity, m_curNum, m_bHorzFlip, m_bVertFlip);
        v[0].tu1 = m_rectTex.left;
        v[0].tv1 = m_rectTex.bottom;
        v[1].tu1 = m_rectTex.left;
        v[1].tv1 = m_rectTex.top;
        v[2].tu1 = m_rectTex.right;
        v[2].tv1 = m_rectTex.bottom;
        v[3].tu1 = m_rectTex.right;
        v[3].tv1 = m_rectTex.top;

        GetRectFromNum(m_rectTex, m_xQuantity, m_yQuantity, m_curNum + 1, m_bHorzFlip, m_bVertFlip);
        v[0].tu2 = m_rectTex.left;
        v[0].tv2 = m_rectTex.bottom;
        v[1].tu2 = m_rectTex.left;
        v[1].tv2 = m_rectTex.top;
        v[2].tu2 = m_rectTex.right;
        v[2].tv2 = m_rectTex.bottom;
        v[3].tu2 = m_rectTex.right;
        v[3].tv2 = m_rectTex.top;

        // draw rectangle
        m_pRS->SetRenderState(D3DRS_TEXTUREFACTOR, newTFactor);
        m_pRS->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, TS_VERTEX_FRMT, 2, v, sizeof(TS_VERTEX), "TextureSequence");
    }
    m_pRS->PopRenderTarget();
}

void TextureSequence::Release()
{
    if (m_texture >= 0 && m_pRS != nullptr)
    {
        m_pRS->TextureRelease(m_texture);
        m_texture = -1;
    }
    if (m_pRS != nullptr && m_AllTex != -1)
    {
        m_pRS->TextureRelease(m_AllTex);
        m_AllTex = -1;
    }
}

void TextureSequence::LostRender()
{
    TEXTURE_RELEASE(m_pRS, m_texture);
}

void TextureSequence::RestoreRender()
{
    m_texture = m_pRS->TextureCreate(m_texWidth, m_texHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT);
}
