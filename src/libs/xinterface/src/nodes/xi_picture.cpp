#include "xi_picture.h"
#include "../mobile_image_bridge.hpp"
#ifdef __ANDROID__
#include "platform/android_ui.hpp"
#endif

#include "storm_assert.h"
#include "string_compare.hpp"
#include "v_file_service.h"

CXI_PICTURE::CXI_PICTURE()
{
    m_rs = nullptr;
    m_idTex = -1;
    m_pTex = nullptr;
    m_nNodeType = NODETYPE_PICTURE;
    m_pcGroupName = nullptr;
    m_bMakeBlind = false;
    m_fCurBlindTime = 0.f;
    m_bBlindUp = true;
    m_fBlindUpSpeed = 0.001f;
    m_fBlindDownSpeed = 0.001f;
    m_dwBlindMin = ARGB(255, 128, 128, 128);
    m_dwBlindMax = ARGB(255, 255, 255, 255);
}

CXI_PICTURE::~CXI_PICTURE()
{
    ReleaseAll();
}

void CXI_PICTURE::Draw(bool bSelected, uint32_t Delta_Time)
{
    if (m_bUse)
    {
        if (m_bMakeBlind)
        {
            if (m_bBlindUp)
            {
                m_fCurBlindTime += m_fBlindUpSpeed * Delta_Time;
                if (m_fCurBlindTime >= 1.f)
                {
                    m_fCurBlindTime = 1.f;
                    m_bBlindUp = false;
                }
            }
            else
            {
                m_fCurBlindTime -= m_fBlindDownSpeed * Delta_Time;
                if (m_fCurBlindTime <= 0.f)
                {
                    m_fCurBlindTime = 0.f;
                    m_bBlindUp = true;
                }
            }
            ChangeColor(ptrOwner->GetBlendColor(m_dwBlindMin, m_dwBlindMax, m_fCurBlindTime));
        }

        if (m_idTex != -1 || m_pTex)
        {
            if (m_idTex != -1)
                m_rs->TextureSet(0, m_idTex);
            else
                m_rs->SetTexture(0, m_pTex ? m_pTex->m_pTexture : nullptr);
            m_rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, XI_ONETEX_FVF, 2, m_v, sizeof(XI_ONETEX_VERTEX), "iVideo");
        }
    }
}

bool CXI_PICTURE::Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VDX9RENDER *rs,
                       XYRECT &hostRect, XYPOINT &ScreenSize)
{
    if (!CINODE::Init(ini1, name1, ini2, name2, rs, hostRect, ScreenSize))
        return false;
    return true;
}

void CXI_PICTURE::LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2)
{
    char param[255];

    m_idTex = -1;
    m_mobileTexturePath.clear();

    auto texRect = FXYRECT(0.f, 0.f, 1.f, 1.f);

    if (ReadIniString(ini1, name1, ini2, name2, "groupName", param, sizeof(param), ""))
    {
        m_idTex = pPictureService->GetTextureID(param);
        const auto len = strlen(param) + 1;
        m_pcGroupName = new char[len];
        Assert(m_pcGroupName);
        memcpy(m_pcGroupName, param, len);

        if (ReadIniString(ini1, name1, ini2, name2, "picName", param, sizeof(param), ""))
        {
            pPictureService->GetTexturePos(m_pcGroupName, param, texRect);
            FXYRECT unused;
            int32_t width = 0, height = 0;
            pPictureService->ResolveImageSource(m_pcGroupName, param, m_mobileTexturePath, unused, width, height);
        }
    }
    else
    {
        if (ReadIniString(ini1, name1, ini2, name2, "textureName", param, sizeof(param), ""))
        {
            m_idTex = m_rs->TextureCreate(param);
            m_mobileTexturePath = param;
        }
        texRect = GetIniFloatRect(ini1, name1, ini2, name2, "textureRect", texRect);
    }

    m_pTex = nullptr;
    if (ReadIniString(ini1, name1, ini2, name2, "videoName", param, sizeof(param), ""))
        m_pTex = m_rs->GetVideoTexture(param);

    const auto color = GetIniARGB(ini1, name1, ini2, name2, "color", ARGB(255, 128, 128, 128));

    // Create rectangle
    ChangePosition(m_rect);
    ChangeUV(texRect);
    for (auto i = 0; i < 4; i++)
    {
        m_v[i].color = color;
        m_v[i].pos.z = 1.f;
    }

    m_bMakeBlind = GetIniBool(ini1, name1, ini2, name2, "blind", false);
    m_fCurBlindTime = 0.f;
    m_bBlindUp = true;
    auto fTmp = GetIniFloat(ini1, name1, ini2, name2, "blindUpTime", 1.f);
    if (fTmp > 0.f)
        m_fBlindUpSpeed = 0.001f / fTmp;
    fTmp = GetIniFloat(ini1, name1, ini2, name2, "blindDownTime", 1.f);
    if (fTmp > 0.f)
        m_fBlindDownSpeed = 0.001f / fTmp;
    m_dwBlindMin = GetIniARGB(ini1, name1, ini2, name2, "blindMinColor", ARGB(255, 128, 128, 128));
    m_dwBlindMax = GetIniARGB(ini1, name1, ini2, name2, "blindMaxColor", ARGB(255, 255, 255, 255));
}

void CXI_PICTURE::ReleaseAll()
{
    ReleasePicture();
}

int CXI_PICTURE::CommandExecute(int wActCode)
{
    return -1;
}

bool CXI_PICTURE::IsClick(int buttonID, int32_t xPos, int32_t yPos)
{
    if (m_bClickable)
    {
        if (xPos >= m_rect.left && xPos <= m_rect.right && yPos >= m_rect.top && yPos <= m_rect.bottom)
            return true;
    }
    return false;
}

void CXI_PICTURE::ChangePosition(XYRECT &rNewPos)
{
    m_rect = rNewPos;
    m_v[0].pos.x = static_cast<float>(m_rect.left);
    m_v[0].pos.y = static_cast<float>(m_rect.top);
    m_v[1].pos.x = static_cast<float>(m_rect.left);
    m_v[1].pos.y = static_cast<float>(m_rect.bottom);
    m_v[2].pos.x = static_cast<float>(m_rect.right);
    m_v[2].pos.y = static_cast<float>(m_rect.top);
    m_v[3].pos.x = static_cast<float>(m_rect.right);
    m_v[3].pos.y = static_cast<float>(m_rect.bottom);
}

void CXI_PICTURE::SaveParametersToIni()
{
    char pcWriteParam[2048];

    auto pIni = fio->OpenIniFile(ptrOwner->m_sDialogFileName.c_str());
    if (!pIni)
    {
        core.Trace("Warning! Can`t open ini file name %s", ptrOwner->m_sDialogFileName.c_str());
        return;
    }

    // save position
    sprintf_s(pcWriteParam, sizeof(pcWriteParam), "%d,%d,%d,%d", m_rect.left, m_rect.top, m_rect.right, m_rect.bottom);
    pIni->WriteString(m_nodeName, "position", pcWriteParam);
}

void CXI_PICTURE::SetNewPicture(bool video, const char *sNewTexName)
{
    ReleasePicture();
    if (video)
        m_pTex = m_rs->GetVideoTexture(sNewTexName);
    else
    {
        m_idTex = m_rs->TextureCreate(sNewTexName);
        m_mobileTexturePath = sNewTexName ? sNewTexName : "";
    }

    FXYRECT uv;
    uv.left = uv.top = 0.f;
    uv.right = uv.bottom = 1.f;
    ChangeUV(uv);
}

void CXI_PICTURE::SetNewPictureFromDir(const char *dirName)
{
    char param[512];
    sprintf(param, "resource\\textures\\%s", dirName);

    const auto vFilenames = fio->_GetPathsOrFilenamesByMask(param, "*.tx", false);
    if (!vFilenames.empty())
    {
        int findQ = rand() % vFilenames.size();
        sprintf(param, "%s\\%s", dirName, vFilenames[findQ].c_str());
        const int paramlen = strlen(param);
        if (paramlen < sizeof(param) && paramlen >= 3)
        {
            param[paramlen - 3] = 0;
        }
        SetNewPicture(false, param);
    }
}

void CXI_PICTURE::SetNewPictureByGroup(const char *groupName, const char *picName)
{
    if (!m_pcGroupName || !storm::iEquals(m_pcGroupName, groupName))
    {
        ReleasePicture();
        if (groupName)
        {
            const auto len = strlen(groupName) + 1;
            m_pcGroupName = new char[strlen(groupName) + 1];
            Assert(m_pcGroupName);
            memcpy(m_pcGroupName, groupName, len);
            m_idTex = pPictureService->GetTextureID(groupName);
        }
    }

    if (m_pcGroupName && picName)
    {
        FXYRECT texRect;
        pPictureService->GetTexturePos(m_pcGroupName, picName, texRect);
        FXYRECT unused;
        int32_t width = 0, height = 0;
        m_mobileTexturePath.clear();
        pPictureService->ResolveImageSource(m_pcGroupName, picName, m_mobileTexturePath, unused, width, height);
        ChangeUV(texRect);
    }
}

uint32_t CXI_PICTURE::MessageProc(int32_t msgcode, MESSAGE &message)
{
    switch (msgcode)
    {
    case 0: // Move the picture to a new position
    {
        m_rect.left = message.Long();
        m_rect.top = message.Long();
        m_rect.right = message.Long();
        m_rect.bottom = message.Long();
        ChangePosition(m_rect);
    }
    break;

    case 1: // Set the texture coordinates of the image
    {
        FXYRECT texRect;
        texRect.left = message.Float();
        texRect.right = message.Float();
        texRect.top = message.Float();
        texRect.bottom = message.Float();
        ChangeUV(texRect);
    }
    break;

    case 2: // Set a new picture or video picture
    {
        const auto bVideo = message.Long() != 0;
        const std::string &param = message.String();
        SetNewPicture(bVideo, param.c_str());
    }
    break;

    case 3: // Get a random picture from the directory
    {
        const std::string &param = message.String();
        SetNewPictureFromDir(param.c_str());
    }
    break;

    case 4: // Set a new color
    {
        const uint32_t color = message.Long();
        for (auto i = 0; i < 4; i++)
            m_v[i].color = color;
    }
    break;

    case 5: // set / remove blinking
    {
        const bool bBlind = message.Long() != 0;
        if (m_bMakeBlind != bBlind)
        {
            m_bMakeBlind = bBlind;
            if (!m_bMakeBlind)
                ChangeColor(m_dwBlindMin);
            else
            {
                m_fCurBlindTime = 0.f;
                m_bBlindUp = true;
            }
        }
    }
    break;

    case 6: // set new picture by group and picture name
    {
        const std::string &groupName = message.String();
        const std::string &picName = message.String();
        SetNewPictureByGroup(groupName.c_str(), picName.c_str());
    }
    break;

    case 7: // set new picture by pointer to IDirect3DTexture9
    {
        int32_t pTex = -1;
        if (message.GetCurrentFormatType() == 'p') {
            // DEPRECATED
            core.Trace("Warning! Setting an interface picture by pointer is deprecated. Please use integers instead.");
            pTex = message.Pointer();
        }
        else {
            pTex = message.Long();
        }
        SetNewPictureByPointer(pTex);
    }
    break;

    case 8: // remove texture from other picture to this
    {
        const std::string &srcNodeName = message.String();
        auto *pNod = static_cast<CINODE *>(ptrOwner->FindNode(srcNodeName.c_str(), nullptr));
        if (pNod->m_nNodeType != NODETYPE_PICTURE)
        {
            core.Trace("Warning! XINTERFACE:: node with name %s have not picture type.", srcNodeName.c_str());
        }
        else
        {
            ReleasePicture();
            auto *pOtherPic = static_cast<CXI_PICTURE *>(pNod);
            if (pOtherPic->m_pcGroupName)
            {
                m_pcGroupName = pOtherPic->m_pcGroupName;
                pOtherPic->m_pcGroupName = nullptr;
            }
            if (pOtherPic->m_idTex != -1)
            {
                m_idTex = pOtherPic->m_idTex;
                pOtherPic->m_idTex = -1;
            }
            for (int32_t n = 0; n < 4; n++)
            {
                m_v[n].tu = pOtherPic->m_v[n].tu;
                m_v[n].tv = pOtherPic->m_v[n].tv;
            }
            pOtherPic->ReleasePicture();
        }
    }
    break;
    }

    return 0;
}

void CXI_PICTURE::ChangeUV(FXYRECT &frNewUV)
{
    m_v[0].tu = frNewUV.left;
    m_v[0].tv = frNewUV.top;
    m_v[1].tu = frNewUV.left;
    m_v[1].tv = frNewUV.bottom;
    m_v[2].tu = frNewUV.right;
    m_v[2].tv = frNewUV.top;
    m_v[3].tu = frNewUV.right;
    m_v[3].tv = frNewUV.bottom;
}

void CXI_PICTURE::ChangeColor(uint32_t dwColor)
{
    m_v[0].color = m_v[1].color = m_v[2].color = m_v[3].color = dwColor;
}

void CXI_PICTURE::SetPictureSize(int32_t &nWidth, int32_t &nHeight)
{
    if (!m_pTex && m_idTex == -1)
    {
        m_bUse = false;
        nWidth = nHeight = 0;
        return;
    }

    if (nWidth <= 0)
    {
        // find the real width
        nWidth = 128;
    }
    if (nHeight <= 0)
    {
        // find the real height
        nHeight = 128;
    }

    if (nWidth < 0 || nHeight < 0)
    {
        m_bUse = false;
        nWidth = nHeight = 0;
        return;
    }

    XYRECT rNewPos = m_rect;
    if (rNewPos.right - rNewPos.left != nWidth)
    {
        rNewPos.left = (m_rect.left + m_rect.right - nWidth) / 2;
        rNewPos.right = rNewPos.left + nWidth;
    }
    if (rNewPos.bottom - rNewPos.top != nHeight)
    {
        rNewPos.top = (m_rect.top + m_rect.bottom - nHeight) / 2;
        rNewPos.bottom = rNewPos.top + nHeight;
    }
    ChangePosition(rNewPos);
}

void CXI_PICTURE::SetNewPictureByPointer(int32_t textureId)
{
    IDirect3DBaseTexture9 *texture = m_rs->GetTextureFromID(textureId);
    m_rs->TextureIncReference(textureId);
    if (texture)
        texture->AddRef();
    ReleasePicture();
    m_idTex = textureId;

    FXYRECT uv;
    uv.left = uv.top = 0.f;
    uv.right = uv.bottom = 1.f;
    ChangeUV(uv);
}

void CXI_PICTURE::ReleasePicture()
{
    m_mobileTexturePath.clear();
    PICTURE_TEXTURE_RELEASE(pPictureService, m_pcGroupName, m_idTex);

    STORM_DELETE(m_pcGroupName);
    TEXTURE_RELEASE(m_rs, m_idTex);
    VIDEOTEXTURE_RELEASE(m_rs, m_pTex);
}

#ifdef __ANDROID__
namespace {
int MobileSaveSlot(const char *node)
{
    if (!node) return -1;
    for (int slot = 0; slot < 10; ++slot)
        if (std::string(node) == "SAVEIMG" + std::to_string(slot + 1)) return slot;
    return -1;
}
}

void CXI_PICTURE::MobileCollect(std::vector<MobileWidget> &widgets) const
{
    const int slot = MobileSaveSlot(m_nodeName);
    if (slot < 0)
    {
        // Legacy Java forms render unknown widget types as blank text rows.
        if (!storm::android::imagePoll || !storm::android::imageReply)
            return;
        if (m_mobileTexturePath.empty() || m_pTex)
            return;
        auto ref =
            storm::mobile::Reference({m_mobileTexturePath, {m_v[0].tu, m_v[0].tv, m_v[3].tu, m_v[3].tv}}, m_v[0].color);
        if (!ref.handle)
            return;
        MobileWidget widget{m_nodeName, "image", "", "", -1, m_rect.top * 8192 + m_rect.left, false};
        // The stock tab's active artwork is named TabDeSelected. Compare the
        // actual atlas crop; no optimistic Android state or image paths leak.
        const std::string name = m_nodeName ? m_nodeName : "";
        if (name.size() == 8 && name.compare(0, 7, "TABBTN_") == 0 && name[7] >= '1' && name[7] <= '4' &&
            m_pcGroupName && storm::iEquals(m_pcGroupName, "TABS"))
        {
            FXYRECT active;
            if (pPictureService->GetTexturePos(m_pcGroupName, "TabDeSelected", active))
                widget.value = m_v[0].tu == active.left && m_v[0].tv == active.top &&
                                       m_v[3].tu == active.right && m_v[3].tv == active.bottom
                                   ? "1" : "0";
        }
        widget.enabled = m_bSelected && m_bClickable && !m_bLockStatus;
        if (widget.enabled && (CheckCommandUsed(ACTION_MOUSECLICK) || CheckCommandUsed(ACTION_ACTIVATE)))
            widget.actions.push_back("press");
        widget.images.push_back(ref);
        widgets.push_back(std::move(widget));
        return;
    }
    // Names and dates remain runtime SAVENOTES entries, joined by the Java presentation.
    widgets.push_back({m_nodeName, "save", "", m_bMakeBlind ? "1" : "0", slot,
        m_rect.top * 8192 + m_rect.left, m_bSelected && m_bClickable && !m_bLockStatus});
}

bool CXI_PICTURE::MobileApply(int index, const std::string &value)
{
    const int slot = MobileSaveSlot(m_nodeName);
    if (slot < 0 || index != slot || !m_bUse || !m_bSelected || !m_bClickable ||
        m_bLockedNode || m_bLockStatus) return false;
    const int code = value == "select" ? ACTION_MOUSECLICK :
        value == "activate" ? ACTION_MOUSEDBLCLICK : -1;
    if (code < 0 || !CheckCommandUsed(code)) return false;
    bool stop = false;
    DoAction(code, stop, true);
    return true;
}
#endif
