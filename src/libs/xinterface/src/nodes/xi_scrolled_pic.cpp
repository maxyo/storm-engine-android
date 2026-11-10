#include "xi_scrolled_pic.h"
#include "../mobile_image_bridge.hpp"
#ifdef __ANDROID__
#include "platform/android_ui.hpp"
#endif
#include <cmath>
#include <cstdio>
#include <stdio.h>

CXI_SCROLLEDPICTURE::CXI_SCROLLEDPICTURE()
{
    m_nNodeType = NODETYPE_SCROLLEDPICTURE;
}

CXI_SCROLLEDPICTURE::~CXI_SCROLLEDPICTURE()
{
    ReleaseAll();
}

void CXI_SCROLLEDPICTURE::Draw(bool bSelected, uint32_t Delta_Time)
{
    CXI_PICTURE::Draw(bSelected, Delta_Time);

    for (int32_t n = 0; n < m_aImg.size(); n++)
    {
        if (m_aImg[n].bShow)
        {
            m_aImg[n].pImg->Draw();
        }
    }
}

bool CXI_SCROLLEDPICTURE::Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VDX9RENDER *rs,
                               XYRECT &hostRect, XYPOINT &ScreenSize)
{
    const auto bSuccess = CXI_PICTURE::Init(ini1, name1, ini2, name2, rs, hostRect, ScreenSize);
    return bSuccess;
}

void CXI_SCROLLEDPICTURE::LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2)
{
    CXI_PICTURE::LoadIni(ini1, name1, ini2, name2);

    m_fpBaseSize.x = m_fpBaseSize.y = 1000.f;
    m_fpBaseSize = GetIniFloatPoint(ini1, name1, ini2, name2, "basesize", m_fpBaseSize);
    if (m_fpBaseSize.x <= 0.f)
        m_fpBaseSize.x = 1.f;
    if (m_fpBaseSize.y <= 0.f)
        m_fpBaseSize.y = 1.f;

    int32_t n;
    char keyName[128];
    m_aScale.clear();
    for (n = 1; n < 20; n++)
    {
        sprintf_s(keyName, "scale%d", n);
        FXYPOINT fpTemp;
        fpTemp.x = fpTemp.y = 2.f;
        fpTemp = GetIniFloatPoint(ini1, name1, ini2, name2, keyName, fpTemp);
        if (fpTemp.x > 1.f || fpTemp.y > 1.f)
            break; // not read or error
        m_aScale.push_back(fpTemp);
    }

    m_nScaleNum = GetIniLong(ini1, name1, ini2, name2, "startscale", 0) - 1;

    auto *pAttribute = core.Entity_GetAttributeClass(g_idInterface, m_nodeName);
    if (pAttribute)
    {
        auto *pAttr = pAttribute->GetAttributeClass("imagelist");
        if (pAttr)
        {
            const int32_t q = pAttr->GetAttributesNum();
            for (n = 0; n < q; n++)
            {
                auto *pA = pAttr->GetAttributeClass(n);
                if (pA)
                {
                    // int32_t i = m_aImg.Add();
                    m_aImg.push_back(BuildinImage{});
                    const int32_t i = m_aImg.size() - 1;
                    m_aImg[i].bShow = false;
                    m_aImg[i].fpPos.x = pA->GetAttributeAsFloat("x", 0.f);
                    m_aImg[i].fpPos.y = pA->GetAttributeAsFloat("y", 0.f);
                    m_aImg[i].fpSize.x = pA->GetAttributeAsFloat("width", -1.f);
                    m_aImg[i].fpSize.y = pA->GetAttributeAsFloat("height", -1.f);
                    m_aImg[i].id = pA->GetThisName() ? pA->GetThisName() : "";

                    m_aImg[i].pImg = new CXI_IMAGE;
                    Assert(m_aImg[i].pImg);
                    const char *pcGroupName = pA->GetAttribute("group");
                    if (pcGroupName)
                    {
                        m_aImg[i].group = pcGroupName;
                        m_aImg[i].pic = pA->GetAttribute("pic") ? pA->GetAttribute("pic") : "";
                        m_aImg[i].pImg->LoadFromBase(pcGroupName, pA->GetAttribute("pic"), true);
                        if (m_aImg[i].fpSize.x > 0.f && m_aImg[i].fpSize.y > 0.f)
                            m_aImg[i].pImg->SetSize(static_cast<int32_t>(m_aImg[i].fpSize.x),
                                                    static_cast<int32_t>(m_aImg[i].fpSize.y));
                    }
                    else
                    {
                        m_aImg[i].file = pA->GetAttribute("file") ? pA->GetAttribute("file") : "";
                        m_aImg[i].pImg->LoadFromFile(pA->GetAttribute("file"));
                    }
                    m_aImg[i].fpSize.x = static_cast<float>(m_aImg[i].pImg->GetWidth());
                    m_aImg[i].fpSize.y = static_cast<float>(m_aImg[i].pImg->GetHeight());
                }
            }
        }

        m_nScaleNum = pAttribute->GetAttributeAsDword("scale", m_nScaleNum);

        const auto fx = pAttribute->GetAttributeAsFloat("centerX", m_fpBaseSize.x * .5f);
        const auto fy = pAttribute->GetAttributeAsFloat("centerY", m_fpBaseSize.y * .5f);
        SetPosToCenter(fx, fy);
    }

    SetScale(m_nScaleNum);
}

void CXI_SCROLLEDPICTURE::ReleaseAll()
{
    CXI_PICTURE::ReleaseAll();
}

int CXI_SCROLLEDPICTURE::CommandExecute(int wActCode)
{
    return -1;
}

bool CXI_SCROLLEDPICTURE::IsClick(int buttonID, int32_t xPos, int32_t yPos)
{
    if (buttonID == MOUSE_RBUTTON)
    {
        if (xPos >= m_rect.left && xPos <= m_rect.right && yPos >= m_rect.top && yPos <= m_rect.bottom)
        {
            m_nScaleNum++;
            if (m_nScaleNum >= m_aScale.size())
                m_nScaleNum = 0;
            SetScale(m_nScaleNum);
        }
    }
    return true;
}

void CXI_SCROLLEDPICTURE::ChangePosition(XYRECT &rNewPos)
{
    CXI_PICTURE::ChangePosition(rNewPos);
    RecalculateTexPerPixel();
    UpdateBuildenImages();
}

void CXI_SCROLLEDPICTURE::SaveParametersToIni()
{
    CXI_PICTURE::SaveParametersToIni();
}

void CXI_SCROLLEDPICTURE::SetNewPicture(bool video, char *sNewTexName)
{
    CXI_PICTURE::SetNewPicture(video, sNewTexName);
}

void CXI_SCROLLEDPICTURE::SetNewPictureFromDir(char *dirName)
{
    CXI_PICTURE::SetNewPictureFromDir(dirName);
}

uint32_t CXI_SCROLLEDPICTURE::MessageProc(int32_t msgcode, MESSAGE &message)
{
    switch (msgcode)
    {
    case 10: // Set a new center
    {
        const auto fx = message.Float();
        const auto fy = message.Float();
        SetPosToCenter(fx, fy);
    }
        return 0;
        break;
    }
    return CXI_PICTURE::MessageProc(msgcode, message);
}

void CXI_SCROLLEDPICTURE::MoveMouseOutScreen(float fX, float fY)
{
    if (fX == 0.f && fY == 0.f)
        return;

    auto fDeltaU = fX * m_fUTexPerPixel;
    auto fDeltaV = fY * m_fVTexPerPixel;

    if (m_v[0].tu + fDeltaU < 0.f)
        fDeltaU = -m_v[0].tu;
    if (m_v[0].tv + fDeltaV < 0.f)
        fDeltaV = -m_v[0].tv;

    if (m_v[3].tu + fDeltaU > 1.f)
        fDeltaU = 1.f - m_v[3].tu;
    if (m_v[3].tv + fDeltaV > 1.f)
        fDeltaV = 1.f - m_v[3].tv;

    m_v[0].tu = (m_v[1].tu += fDeltaU);
    m_v[2].tu = (m_v[3].tu += fDeltaU);
    m_v[0].tv = (m_v[2].tv += fDeltaV);
    m_v[1].tv = (m_v[3].tv += fDeltaV);

    UpdateBuildenImages();
}

void CXI_SCROLLEDPICTURE::ChangeUV(FXYRECT &frNewUV)
{
    CXI_PICTURE::ChangeUV(frNewUV);
    RecalculateTexPerPixel();
    UpdateBuildenImages();
    auto *pAttribute = core.Entity_GetAttributeClass(g_idInterface, m_nodeName);
    if (pAttribute)
    {
        auto *pA = pAttribute->GetAttributeClass("offset");
        if (!pA)
            pA = pAttribute->CreateSubAClass(pAttribute, "offset");
        if (pA)
        {
            pA->SetAttributeUseFloat("x", frNewUV.left * m_fpBaseSize.x);
            pA->SetAttributeUseFloat("y", frNewUV.top * m_fpBaseSize.y);
        }
        pA = pAttribute->GetAttributeClass("size");
        if (!pA)
            pA = pAttribute->CreateSubAClass(pAttribute, "size");
        if (pA)
        {
            pA->SetAttributeUseFloat("x", (frNewUV.right - frNewUV.left) * m_fpBaseSize.x);
            pA->SetAttributeUseFloat("y", (frNewUV.bottom - frNewUV.top) * m_fpBaseSize.y);
        }
        pA = pAttribute->GetAttributeClass("scale");
        if (!pA)
            pA = pAttribute->CreateSubAClass(pAttribute, "scale");
        if (pA)
        {
            pA->SetAttributeUseFloat("x", (frNewUV.right - frNewUV.left) * m_fpBaseSize.x /
                                              static_cast<float>(m_rect.right - m_rect.left));
            pA->SetAttributeUseFloat("y", (frNewUV.bottom - frNewUV.top) * m_fpBaseSize.y /
                                              static_cast<float>(m_rect.bottom - m_rect.top));
        }
    }
}

void CXI_SCROLLEDPICTURE::RecalculateTexPerPixel()
{
    if (m_rect.right - m_rect.left <= 0)
        m_fUTexPerPixel = 0.f;
    else
        m_fUTexPerPixel = (m_v[3].tu - m_v[0].tu) / (m_rect.right - m_rect.left);

    if (m_rect.bottom - m_rect.top <= 0)
        m_fVTexPerPixel = 0.f;
    else
        m_fVTexPerPixel = (m_v[3].tv - m_v[0].tv) / (m_rect.bottom - m_rect.top);
}

void CXI_SCROLLEDPICTURE::UpdateBuildenImages()
{
    for (int32_t n = 0; n < m_aImg.size(); n++)
    {
        m_aImg[n].bShow = false;

        auto fx = m_aImg[n].fpPos.x / m_fpBaseSize.x;
        auto fy = m_aImg[n].fpPos.y / m_fpBaseSize.y;
        if (fx < m_v[0].tu || fy < m_v[0].tv || fx > m_v[3].tu || fy > m_v[3].tv || m_fUTexPerPixel <= 0.f ||
            m_fVTexPerPixel <= 0.f)
            continue;
        fx = m_v[0].pos.x + (fx - m_v[0].tu) / m_fUTexPerPixel;
        fy = m_v[0].pos.y + (fy - m_v[0].tv) / m_fVTexPerPixel;
        m_aImg[n].pImg->SetPosition(static_cast<int32_t>(fx), static_cast<int32_t>(fy), IPType_Center);
        m_aImg[n].bShow = true;
    }
}

void CXI_SCROLLEDPICTURE::SetPosToCenter(float fX, float fY)
{
    RecalculateTexPerPixel();

    auto fLeft = fX / m_fpBaseSize.x;
    auto fTop = fY / m_fpBaseSize.y;
    const auto fWidth = (m_rect.right - m_rect.left) * m_fUTexPerPixel;
    const auto fHeight = (m_rect.bottom - m_rect.top) * m_fVTexPerPixel;

    if (fLeft <= fWidth * .5f)
        fLeft = 0.f;
    else
        fLeft -= fWidth * .5f;
    if (fTop <= fHeight * .5f)
        fTop = 0.f;
    else
        fTop -= fHeight * .5f;

    if (fLeft + fWidth > 1.f)
        fLeft = 1.f - fWidth;
    if (fTop + fHeight > 1.f)
        fTop = 1.f - fHeight;

    FXYRECT frNewUV;
    frNewUV.left = fLeft;
    frNewUV.top = fTop;
    frNewUV.right = fLeft + fWidth;
    frNewUV.bottom = fTop + fHeight;

    ChangeUV(frNewUV);
}

void CXI_SCROLLEDPICTURE::SetScale(int32_t nScaleIdx)
{
    if (nScaleIdx < 0 || nScaleIdx >= m_aScale.size())
        return;
    SetScale(m_aScale[nScaleIdx].x, m_aScale[nScaleIdx].y);
}

void CXI_SCROLLEDPICTURE::SetScale(float fsx, float fsy)
{
    if (fsx < 0.f)
        fsx = 0.f;
    if (fsx > 1.f)
        fsx = 1.f;
    if (fsy < 0.f)
        fsy = 0.f;
    if (fsy > 1.f)
        fsy = 1.f;

    const auto fCenterX = (m_v[3].tu + m_v[0].tu) * .5f;
    const auto fCenterY = (m_v[3].tv + m_v[0].tv) * .5f;

    auto fLeft = fCenterX - fsx * .5f;
    auto fTop = fCenterY - fsy * .5f;
    if (fLeft < 0.f)
        fLeft = 0.f;
    if (fTop < 0.f)
        fTop = 0.f;
    if (fLeft + fsx > 1.f)
        fLeft = 1.f - fsx;
    if (fTop + fsy > 1.f)
        fTop = 1.f - fsy;

    FXYRECT frNewUV;
    frNewUV.left = fLeft;
    frNewUV.top = fTop;
    frNewUV.right = fLeft + fsx;
    frNewUV.bottom = fTop + fsy;
    ChangeUV(frNewUV);
}

#ifdef __ANDROID__
// The scrolled picture as a map for touch (type "map"): the whole picture as a texture source,
// context "base" (the size the scripts measure it in, ini basesize) and "view" (the part the
// game shows now, in the same units), then one "map_marker" widget per imagelist entry with
// its centre and size in base units. The portable UI pans and zooms the picture itself; a tap
// comes back as "point@<opening>:<x>,<y>" in base units and "release" ends the hold.
void CXI_SCROLLEDPICTURE::MobileCollect(std::vector<MobileWidget> &widgets) const
{
    if (!storm::android::imagePoll || !storm::android::imageReply || m_pTex)
        return;
    const int order = m_rect.top * 8192 + m_rect.left;
    MobileWidget map{m_nodeName, "map", "", "", -1, order, m_bSelected && !m_bLockStatus};
    // A scrolled picture may carry no texture of its own: MapBest.ini draws the map as the
    // picture MAIN_BACK under a transparent MAP that holds the markers and the clicks.
    if (const std::string texture = MobileTextureName(m_mobileTexturePath); !texture.empty())
    {
        auto picture = storm::mobile::Reference({m_mobileTexturePath, {0.f, 0.f, 1.f, 1.f}}, m_v[0].color);
        picture.source = {"texture", {}, texture, {}, {}, -1};
        if (picture.handle)
            map.images.push_back(picture);
    }
    map.context["base"] = MobileNumber(m_fpBaseSize.x) + "," + MobileNumber(m_fpBaseSize.y);
    map.context["view"] = MobileNumber(m_v[0].tu * m_fpBaseSize.x) + "," + MobileNumber(m_v[0].tv * m_fpBaseSize.y) + "," +
                          MobileNumber(m_v[3].tu * m_fpBaseSize.x) + "," + MobileNumber(m_v[3].tv * m_fpBaseSize.y);
    if (map.enabled && m_pCommands[RClickCommand()].bUse && m_pCommands[RClickCommand()].sEventName)
        map.actions = {"point", "release"};
    widgets.push_back(std::move(map));
    for (size_t n = 0; n < m_aImg.size(); ++n)
    {
        const BuildinImage &image = m_aImg[n];
        storm::mobile::ImageRef ref;
        if (!image.group.empty())
            ref = storm::mobile::AtlasReference(pPictureService, image.group.c_str(), image.pic.c_str());
        else if (const std::string file = MobileTextureName(image.file); !file.empty())
        {
            ref = storm::mobile::Reference({image.file, {0.f, 0.f, 1.f, 1.f}});
            ref.source = {"texture", {}, file, {}, {}, -1};
        }
        if (!ref.handle)
            continue;
        MobileWidget marker{m_nodeName, "map_marker", "", "", static_cast<int>(n), order, false};
        marker.rowId = image.id;
        marker.images.push_back(ref);
        marker.context["x"] = MobileNumber(image.fpPos.x);
        marker.context["y"] = MobileNumber(image.fpPos.y);
        marker.context["width"] = MobileNumber(image.fpSize.x);
        marker.context["height"] = MobileNumber(image.fpSize.y);
        widgets.push_back(std::move(marker));
    }
}

int CXI_SCROLLEDPICTURE::RClickCommand() const
{
    for (int i = 0; i < COMMAND_QUANTITY; i++)
        if (pCommandsList[i].code == ACTION_MOUSERCLICK)
            return i;
    return 0;
}

bool CXI_SCROLLEDPICTURE::MobileApply(int index, const std::string &value)
{
    const COMMAND_ACTION &command = m_pCommands[RClickCommand()];
    if (index != -1 || !m_bUse || !command.bUse || !command.sEventName)
        return false;
    if (value == "release")
    {
        // The game shows what the right button picked for as long as it is held; its release
        // is the script's MouseRClickUP. The window it opened usually locks the map meanwhile.
        core.Event("MouseRClickUP");
        return true;
    }
    if (!m_bSelected || m_bLockedNode || m_bLockStatus)
        return false;
    const std::string prefix = "point@" + std::to_string(storm::android::mobileScreenRevision) + ":";
    float x = 0.f, y = 0.f;
    if (value.compare(0, prefix.size(), prefix) != 0 || std::sscanf(value.c_str() + prefix.size(), "%f,%f", &x, &y) != 2 ||
        !std::isfinite(x) || !std::isfinite(y) || m_fUTexPerPixel <= 0.f || m_fVTexPerPixel <= 0.f)
        return false;
    // The screen point where the game shows this point of the picture now, so that the script
    // reads it from mousepos the way it reads a right click there (its own offset/scale maths).
    // The event goes out at once: the next mouse update would overwrite mousepos.
    auto *root = core.Entity_GetAttributePointer(g_idInterface);
    auto *mouse = root ? root->GetAttributeClass("mousepos") : nullptr;
    if (root && !mouse)
        mouse = root->CreateSubAClass(root, "mousepos");
    if (!mouse)
        return false;
    mouse->SetAttributeUseFloat("x", m_rect.left + (x / m_fpBaseSize.x - m_v[0].tu) / m_fUTexPerPixel);
    mouse->SetAttributeUseFloat("y", m_rect.top + (y / m_fpBaseSize.y - m_v[0].tv) / m_fVTexPerPixel);
    core.Event(command.sEventName, "ls", RClickCommand(), m_nodeName);
    return true;
}
#endif
