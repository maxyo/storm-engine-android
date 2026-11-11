#include "xservice.h"

#include "core.h"
#include "dx9render.h"
#include "string_compare.hpp"
#include "v_file_service.h"
#include "platform/platform.hpp"

#include <filesystem>

XSERVICE::XSERVICE()
    : m_fWScale(0), m_fHScale(0), m_fWAdd(0), m_fHAdd(0)
{
    m_pRS = nullptr;
}

XSERVICE::~XSERVICE()
{
}

void XSERVICE::Init(VDX9RENDER *pRS, int32_t lWidth, int32_t lHeight)
{
    m_pRS = pRS;

    // Half a texel inwards on every picture edge keeps bilinear sampling inside the picture.
    m_fWAdd = 0.5f;
    m_fHAdd = 0.5f;

    LoadAllPicturesInfo();
}

int32_t XSERVICE::GetTextureID(const char *sImageListName)
{
    const auto n = FindGroup(sImageListName);
    if (n < 0)
        return -1;
    auto &texture = m_textures[n];
    if (texture.references <= 0)
    {
        const std::string path = "INTERFACES\\" + m_catalog.groups()[n].texture;
        texture.id = m_pRS->TextureCreate(path.c_str());
        texture.references = 1;
    }
    else
        texture.references++;
    return texture.id;
}

int32_t XSERVICE::FindGroup(const char *sImageListName) const
{
    if (!sImageListName)
        return -1;
    const auto &groups = m_catalog.groups();
    for (size_t n = 0; n < groups.size(); n++)
        if (storm::iEquals(groups[n].name, sImageListName))
            return static_cast<int32_t>(n);
    return -1;
}

bool XSERVICE::ReleaseTextureID(const char *sImageListName)
{
    const auto n = FindGroup(sImageListName);
    if (n < 0)
        return false;
    auto &texture = m_textures[n];
    if (--texture.references == 0)
    {
        m_pRS->TextureRelease(texture.id);
        return true;
    }
    return false;
}

// The group a picture index belongs to, or -1.
static int32_t GroupOfPicture(const storm::xinterface::PictureCatalog &catalog, int32_t pictureNum)
{
    const auto &groups = catalog.groups();
    for (size_t gn = 0; gn < groups.size(); gn++)
        if (pictureNum >= groups[gn].pictureStart && pictureNum < groups[gn].pictureStart + groups[gn].pictureCount)
            return static_cast<int32_t>(gn);
    return -1;
}

bool XSERVICE::GetTexturePos(int32_t pictureNum, FXYRECT &texRect)
{
    return GetTexturePos(0, pictureNum, texRect);
}

bool XSERVICE::GetTexturePos(int32_t pictureNum, XYRECT &texRect)
{
    const auto &pictures = m_catalog.pictures();
    if (pictureNum >= 0 && pictureNum < static_cast<int32_t>(pictures.size()))
    {
        const auto &picture = pictures[pictureNum];
        texRect = {picture.left, picture.top, picture.right, picture.bottom};
        return true;
    }

    texRect = {};
    return false;
}

bool XSERVICE::GetTexturePos(const char *sImageListName, const char *sImageName, FXYRECT &texRect)
{
    return GetTexturePos(GetImageNum(sImageListName, sImageName), texRect);
}

bool XSERVICE::GetTexturePos(const char *sImageListName, const char *sImageName, XYRECT &texRect)
{
    return GetTexturePos(GetImageNum(sImageListName, sImageName), texRect);
}

bool XSERVICE::GetTexturePos(int nTextureModify, int32_t pictureNum, FXYRECT &texRect)
{
    const auto &pictures = m_catalog.pictures();
    const auto gn = pictureNum >= 0 && pictureNum < static_cast<int32_t>(pictures.size())
                        ? GroupOfPicture(m_catalog, pictureNum)
                        : -1;
    if (gn < 0)
    {
        texRect = {};
        return false;
    }

    const auto &picture = pictures[pictureNum];
    const auto &group = m_catalog.groups()[gn];
    FXYRECT rectTmp;
    rectTmp.left = static_cast<float>(picture.left);
    rectTmp.top = static_cast<float>(picture.top);
    rectTmp.right = static_cast<float>(picture.right);
    rectTmp.bottom = static_cast<float>(picture.bottom);
    if (nTextureModify & TEXTURE_MODIFY_HORZFLIP)
    {
        const auto tmp = rectTmp.left + m_fWAdd * 2.f;
        rectTmp.left = rectTmp.right - m_fWAdd * 2.f;
        rectTmp.right = tmp;
    }
    if (nTextureModify & TEXTURE_MODIFY_VERTFLIP)
    {
        const auto tmp = rectTmp.top + m_fHAdd * 2.f;
        rectTmp.top = rectTmp.bottom - m_fHAdd * 2.f;
        rectTmp.bottom = tmp;
    }
    texRect.left = (rectTmp.left + m_fWAdd) / group.width;
    texRect.right = (rectTmp.right - m_fWAdd) / group.width;
    texRect.top = (rectTmp.top + m_fHAdd) / group.height;
    texRect.bottom = (rectTmp.bottom - m_fHAdd) / group.height;
    return true;
}

bool XSERVICE::GetTexturePos(int nTextureModify, const char *sImageListName, const char *sImageName, FXYRECT &texRect)
{
    return GetTexturePos(nTextureModify, GetImageNum(sImageListName, sImageName), texRect);
}

void XSERVICE::GetTextureCutForSize(const char *pcImageListName, const FXYPOINT &pntLeftTopUV, const XYPOINT &pntSize,
                                    int32_t nSrcWidth, int32_t nSrcHeight, FXYRECT &outUV)
{
    const auto n = FindGroup(pcImageListName);
    if (n >= 0)
    {
        const auto &group = m_catalog.groups()[n];
        if (nSrcWidth < group.width)
            nSrcWidth = group.width;
        if (nSrcHeight < group.height)
            nSrcHeight = group.height;
    }
    auto fW = 1.f;
    if (nSrcWidth > 0)
        fW = static_cast<float>(pntSize.x) / nSrcWidth + pntLeftTopUV.x;
    auto fH = 1.f;
    if (nSrcHeight > 0)
        fH = static_cast<float>(pntSize.y) / nSrcHeight + pntLeftTopUV.y;
    if (fW > 1.f)
        fW = 1.f;
    if (fH > 1.f)
        fH = 1.f;
    outUV.left = pntLeftTopUV.x;
    outUV.top = pntLeftTopUV.y;
    outUV.right = fW;
    outUV.bottom = fH;
}

// Reads one picture list file into plain sections for the catalog.
static std::vector<storm::xinterface::PictureIniSection> ReadPictureIni(const std::string &file)
{
    auto ini = fio->OpenIniFile(file.c_str());
    if (!ini)
        throw std::runtime_error("picture list not found: " + file);

    std::vector<storm::xinterface::PictureIniSection> sections;
    char section[255];
    char param[255];
    if (ini->GetSectionName(section, sizeof(section) - 1))
        do
        {
            auto &out = sections.emplace_back();
            out.name = section;
            ini->ReadString(section, "sTextureName", param, sizeof(param) - 1, "");
            out.texture = param;
            out.width = ini->GetInt(section, "wTextureWidth", 1024);
            out.height = ini->GetInt(section, "wTextureHeight", 1024);
            if (ini->ReadString(section, "picture", param, sizeof(param) - 1, ""))
                do
                    out.pictures.emplace_back(param);
                while (ini->ReadStringNext(section, "picture", param, sizeof(param) - 1));
        } while (ini->GetSectionNameNext(section, sizeof(section) - 1));
    return sections;
}

void XSERVICE::LoadAllPicturesInfo()
{
    const auto listIni = [](const std::string &directory) {
        const std::filesystem::path root = std::filesystem::u8path(fio->ConvertPathResource(directory.c_str()));
        std::vector<std::string> files;
        for (const auto &path : fio->_GetFsPathsByMask(directory.c_str(), "*.ini", true, false, true, true))
        {
            const auto relative = path.lexically_relative(root).u8string();
            files.emplace_back(relative.begin(), relative.end());
        }
        return files;
    };

    m_catalog = {};
    for (const auto &file : storm::xinterface::pictureIniFiles(storm::gameProfile(), listIni))
        m_catalog.add(file, ReadPictureIni(file));
    m_textures.assign(m_catalog.groups().size(), {});
    core.Trace("Interface pictures: %zu groups, %zu pictures", m_catalog.groups().size(), m_catalog.pictures().size());
}

void XSERVICE::ReleaseAll()
{
    for (const auto &texture : m_textures)
        if (texture.references != 0)
            m_pRS->TextureRelease(texture.id);
    m_textures.clear();
    m_catalog = {};
}

int32_t XSERVICE::GetImageNum(const char *sImageListName, const char *sImageName)
{
    if (sImageName == nullptr)
        return -1;

    const auto &pictures = m_catalog.pictures();
    if (sImageListName != nullptr)
    {
        const auto n = FindGroup(sImageListName);
        if (n < 0)
            return -1;
        const auto &group = m_catalog.groups()[n];
        for (auto j = group.pictureStart; j < group.pictureStart + group.pictureCount; j++)
            if (storm::iEquals(pictures[j].name, sImageName))
                return j;
        return -1;
    }

    for (size_t i = 0; i < pictures.size(); i++)
        if (storm::iEquals(pictures[i].name, sImageName))
            return static_cast<int32_t>(i);
    return -1;
}

//============================================================================
//    COMBINE STRINGS CLASS
//============================================================================

ComboString::ComboString()
{
}

ComboString::~ComboString()
{
}

void ComboString::AddToCombo(char *fontName, const XYPOINT &posStrStart, char *str)
{
}

void ComboString::AddToCombo(XYRECT posPic, char *picTexName, FXYRECT picUV)
{
}

void ComboString::PrintComboString(int comboStrID)
{
}

int ComboString::GetComboString(int align, int x, int y, int needWidth, int needHeight, int *allHeight, char *formatStr,
                                char *fontlist)
{
    return -1;
}

void ComboString::ComboStringRelease(int comboStrID)
{
}

// Resolve atlas metadata without creating or retaining a renderer texture.
bool XSERVICE::ResolveImageSource(const char *group, const char *image, std::string &path, FXYRECT &uv, int32_t &width,
                                  int32_t &height)
{
    if (!group || !image)
        return false;
    const int g = FindGroup(group);
    if (g < 0)
        return false;
    const auto &list = m_catalog.groups()[g];
    if (list.texture.empty() || list.width <= 0 || list.height <= 0)
        return false;
    const int pic = GetImageNum(group, image);
    XYRECT rect;
    if (pic < 0 || !GetTexturePos(pic, rect))
        return false;
    if (rect.left < 0 || rect.top < 0 || rect.right > list.width || rect.bottom > list.height ||
        rect.right <= rect.left || rect.bottom <= rect.top)
        return false;
    path = "INTERFACES\\" + list.texture;
    uv = FXYRECT(float(rect.left) / list.width, float(rect.top) / list.height, float(rect.right) / list.width,
                 float(rect.bottom) / list.height);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    return true;
}
