#include "sound_service.h"
#include "matrix.h"
#include "rands.h"
#include "v_file_service.h"
#include "vma.hpp"
#include "core.h"
#include "debug_entity.h"
#include "math3d/color.h"
#include "math_inlines.h"
CREATE_SERVICE(SoundService)
CREATE_CLASS(SoundVisualisationEntity)
const char *SoundService::GetRandomName(const tAlias *alias) const
{
    return alias->soundFiles.pickRandom().c_str();
}

int SoundService::GetAliasIndexByName(const char *szAliasName)
{
    const uint32_t dwSearchHash = MakeHashValue(szAliasName);
    for (size_t i = 0; i < Aliases.size(); i++)
    {
        if (Aliases[i].dwNameHash == dwSearchHash)
        {
            if (Aliases[i].Name == szAliasName)
            {
                return i;
            }
        }
    }
    return -1;
}

void SoundService::AnalyseNameStringAndAddToAlias(tAlias *_alias, const char *in_string) const
{
    static char fileName[COMMON_STRING_LENGTH];
    strncpy_s(fileName, in_string, COMMON_STRING_LENGTH);

    float probability = DEFAULT_PROBABILITY;

    char *col = strchr(fileName, ',');
    if (col)
    {
        // try to convert forthcoming number
        float flt;
        if (sscanf(++col, "%f", &flt) == 1)
        {
            probability = flt;
        }

        *(--col) = '\0'; // truncate at first ','
    }

    if constexpr (TRACE_INFORMATION)
        core.Trace("  -> sound %s, %f", fileName, probability);

    _alias->soundFiles.emplace(probability, fileName);
}

void SoundService::AddAlias(INIFILE &_iniFile, char *_sectionName)
{
    static char tempString[COMMON_STRING_LENGTH];
    if (!_sectionName)
        return;

    if constexpr (TRACE_INFORMATION)
        core.Trace("Add sound alias %s", _sectionName);
    Aliases.push_back(tAlias{});
    tAlias &alias = Aliases.back();
    alias.Name = _sectionName;
    alias.dwNameHash = MakeHashValue(alias.Name.c_str());
    alias.fMaxDistance = _iniFile.GetFloat(_sectionName, "maxDistance", -1.0f);
    alias.fMinDistance = _iniFile.GetFloat(_sectionName, "minDistance", -1.0f);
    alias.fVolume = _iniFile.GetFloat(_sectionName, "volume", -1.0f);
    alias.iPrior = _iniFile.GetInt(_sectionName, "prior", 128);

    if (_iniFile.ReadString(_sectionName, "name", tempString, COMMON_STRING_LENGTH, ""))
    {
        AnalyseNameStringAndAddToAlias(&alias, tempString);
        while (_iniFile.ReadStringNext(_sectionName, "name", tempString, COMMON_STRING_LENGTH))
            AnalyseNameStringAndAddToAlias(&alias, tempString);
    }
}

void SoundService::LoadAliasFile(const char *_filename)
{
    const static int SECTION_NAME_LENGTH = 128;
    static char sectionName[SECTION_NAME_LENGTH];

    std::string iniName = ALIAS_DIRECTORY;
    iniName += _filename;

    if constexpr (TRACE_INFORMATION)
        core.Trace("Find sound alias file %s", iniName.c_str());
    auto aliasIni = fio->OpenIniFile(iniName.c_str());
    if (!aliasIni)
        return;

    if (aliasIni->GetSectionName(sectionName, SECTION_NAME_LENGTH))
    {
        AddAlias(*aliasIni, sectionName);
        while (aliasIni->GetSectionNameNext(sectionName, SECTION_NAME_LENGTH))
        {
            AddAlias(*aliasIni, sectionName);
        }
    }
}

void SoundService::InitAliases()
{
    const auto vFilenames = fio->_GetPathsOrFilenamesByMask(ALIAS_DIRECTORY, "*.ini", false);
    for (std::string curName : vFilenames)
    {
        LoadAliasFile(curName.c_str());
    }
}

void SoundService::CreateEntityIfNeed()
{
    auto Debugentid_t = core.GetEntityId("SoundVisualisationEntity");
    if (!Debugentid_t)
    {
        Debugentid_t = core.CreateEntity("SoundVisualisationEntity");
        auto *pDebugEntity = static_cast<SoundVisualisationEntity *>(core.GetEntityPointer(Debugentid_t));
        pDebugEntity->SetMasterSoundService(this);
        pDebugEntity->Wakeup();
    }
}

void SoundService::DebugPrint3D(const CVECTOR &pos3D, float rad, int32_t line, float alpha, uint32_t color, float scale,
                                const char *format, ...) const
{
    static char buf[256];
    // print to the buffer
    va_list args;
    va_start(args, format);
    int32_t len = vsnprintf(buf, sizeof(buf) - 1, format, args);
    va_end(args);
    buf[sizeof(buf) - 1] = 0;
    // Looking for a point position on the screen
    static CMatrix mtx, view, prj;
    static D3DVIEWPORT9 vp;
    MTX_PRJ_VECTOR vrt;
    rs->GetTransform(D3DTS_VIEW, view);
    rs->GetTransform(D3DTS_PROJECTION, prj);
    mtx.EqMultiply(view, prj);
    view.Transposition();
    float dist = ~(pos3D - view.Pos());
    if (dist >= rad * rad)
        return;
    const float d = view.Vz() | view.Pos();
    if ((pos3D | view.Vz()) < d)
        return;
    rs->GetViewport(&vp);
    mtx.Projection((CVECTOR *)&pos3D, &vrt, 1, vp.Width * 0.5f, vp.Height * 0.5f, sizeof(CVECTOR),
                   sizeof(MTX_PRJ_VECTOR));
    // Looking for a position
    const int32_t fh = rs->CharHeight(FONT_DEFAULT) / 2;
    vrt.y -= (line + 0.5f) * fh;
    // Transparency
    const float kDist = 0.75f;
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    if (dist > kDist * kDist * rad * rad)
    {
        dist = 1.0f - (sqrtf(dist) - kDist * rad) / (rad - kDist * rad);
        alpha *= dist;
    }
    if (alpha <= 0.0f)
        return;
    color = (static_cast<uint32_t>(alpha * 255.0f) << 24) | (color & 0xffffff);
    // print the text
    rs->ExtPrint(FONT_DEFAULT, color, 0x00000000, PR_ALIGN_CENTER, false, scale, 0, 0, static_cast<int32_t>(vrt.x),
                 static_cast<int32_t>(vrt.y), buf);
}

void SoundService::Draw2DCircle(const CVECTOR &center, uint32_t dwColor, float fRadius, uint32_t dwColor2,
                                float fRadius2) const
{
    float fDelta = 0.2f;
    RS_LINE line[2];
    CVECTOR vStart;
    CVECTOR vEnd;
    CVECTOR vStartPoint;

    vStartPoint = CVECTOR(cosf(0) * fRadius, 0.0f, sinf(0) * fRadius);
    vStartPoint += center;
    for (float Angle = fDelta; Angle <= (PI * 2); Angle += fDelta)
    {
        auto vPoint = CVECTOR(cosf(Angle) * fRadius, 0.0f, sinf(Angle) * fRadius);
        vPoint += center;

        vStart = vStartPoint;
        vEnd = vPoint;

        line[0].vPos = vStart;
        line[0].dwColor = dwColor;
        line[1].vPos = vEnd;
        line[1].dwColor = dwColor;
        rs->DrawLines(line, 1, "Line");
        // rs->DrawVector(vStart, vEnd, dwColor);
        vStartPoint = vPoint;
    }

    vStart = vStartPoint;
    vEnd = CVECTOR(cosf(0) * fRadius, 0.0f, sinf(0) * fRadius);
    vEnd += center;

    line[0].vPos = vStart;
    line[0].dwColor = dwColor;
    line[1].vPos = vEnd;
    line[1].dwColor = dwColor;
    rs->DrawLines(line, 1, "Line");

    vStartPoint = CVECTOR(cosf(0) * fRadius2, 0.0f, sinf(0) * fRadius2);
    vStartPoint += center;
    for (float Angle = fDelta; Angle <= (PI * 2); Angle += fDelta)
    {
        auto vPoint = CVECTOR(cosf(Angle) * fRadius2, 0.0f, sinf(Angle) * fRadius2);
        vPoint += center;

        vStart = vStartPoint;
        vEnd = vPoint;

        line[0].vPos = vStart;
        line[0].dwColor = dwColor2;
        line[1].vPos = vEnd;
        line[1].dwColor = dwColor2;
        rs->DrawLines(line, 1, "Line");
        // rs->DrawVector(vStart, vEnd, dwColor);
        vStartPoint = vPoint;
    }

    vStart = vStartPoint;
    vEnd = CVECTOR(cosf(0) * fRadius2, 0.0f, sinf(0) * fRadius2);
    vEnd += center;

    line[0].vPos = vStart;
    line[0].dwColor = dwColor2;
    line[1].vPos = vEnd;
    line[1].dwColor = dwColor2;
    rs->DrawLines(line, 1, "Line");

    line[0].vPos = CVECTOR(fRadius, 0.0f, 0.0f) + center;
    line[0].dwColor = dwColor;
    line[1].vPos = CVECTOR(fRadius2, 0.0f, 0.0f) + center;
    line[1].dwColor = dwColor;
    rs->DrawLines(line, 1, "Line");

    line[0].vPos = CVECTOR(-fRadius, 0.0f, 0.0f) + center;
    line[0].dwColor = dwColor;
    line[1].vPos = CVECTOR(-fRadius2, 0.0f, 0.0f) + center;
    line[1].dwColor = dwColor;
    rs->DrawLines(line, 1, "Line");

    line[0].vPos = CVECTOR(0.0f, 0.0f, fRadius) + center;
    line[0].dwColor = dwColor;
    line[1].vPos = CVECTOR(0.0f, 0.0f, fRadius2) + center;
    line[1].dwColor = dwColor;
    rs->DrawLines(line, 1, "Line");

    line[0].vPos = CVECTOR(0.0f, 0.0f, -fRadius) + center;
    line[0].dwColor = dwColor;
    line[1].vPos = CVECTOR(0.0f, 0.0f, -fRadius2) + center;
    line[1].dwColor = dwColor;
    rs->DrawLines(line, 1, "Line");

    // rs->DrawVector(vStart, vEnd, dwColor);
}

bool SoundService::SFLB_SetScheme(const char *_schemeName)
{
    ResetScheme();
    return AddScheme(_schemeName);
}

bool SoundService::SetScheme(const char *_schemeName)
{
    return SFLB_SetScheme(_schemeName);
}

void SoundService::ResetScheme()
{
    SoundSchemeChannels.clear();
}

bool SoundService::AddScheme(const char *_schemeName)
{
    static char tempString[COMMON_STRING_LENGTH];
    auto ini = fio->OpenIniFile(SCHEME_INI_NAME);

    if (!ini)
        return false;

    if (ini->ReadString(const_cast<char *>(_schemeName), SCHEME_KEY_NAME, tempString, COMMON_STRING_LENGTH, ""))
    {
        AddSoundSchemeChannel(tempString);
        while (ini->ReadStringNext(const_cast<char *>(_schemeName), SCHEME_KEY_NAME, tempString, COMMON_STRING_LENGTH))
            AddSoundSchemeChannel(tempString);
    }

    if (ini->ReadString(const_cast<char *>(_schemeName), SCHEME_KEY_NAME_LOOP, tempString, COMMON_STRING_LENGTH, ""))
    {
        AddSoundSchemeChannel(tempString, true);
        while (ini->ReadStringNext(const_cast<char *>(_schemeName), SCHEME_KEY_NAME_LOOP, tempString,
                                   COMMON_STRING_LENGTH))
            AddSoundSchemeChannel(tempString, true);
    }

    return true;
}

bool SoundService::AddSoundSchemeChannel(char *in_string, bool _looped /*= false*/)
{
    static char tempString2[COMMON_STRING_LENGTH];
    strncpy_s(tempString2, in_string, COMMON_STRING_LENGTH);

    char *col = strchr(tempString2, ',');
    if (!col)
    {
        // only name, without delays
        tSoundSchemeChannel NewChannel;
        NewChannel.soundName = tempString2;
        NewChannel.minDelayTime = SCHEME_MIN_DELAY;
        NewChannel.maxDelayTime = SCHEME_MAX_DELAY;
        NewChannel.timeToNextPlay =
            static_cast<int32_t>(rand(static_cast<float>(NewChannel.maxDelayTime - NewChannel.minDelayTime)));
        NewChannel.volume = 1.f;
        NewChannel.looped = _looped;
        if (_looped)
            NewChannel.timeToNextPlay = 0;

        SoundSchemeChannels.push_back(NewChannel);
        return true;
    }

    tSoundSchemeChannel NewChannel;

    // try to convert forthcoming numbers
    int n1, n2;
    float f1;
    const int numbersConverted = sscanf(++col, "%d, %d, %f", &n1, &n2, &f1);
    switch (numbersConverted)
    {
    case 1:
        NewChannel.minDelayTime = SCHEME_MIN_DELAY;
        NewChannel.maxDelayTime = n1 * 1000;
        NewChannel.volume = 1.f;
        break;
    case 2:
        NewChannel.minDelayTime = n1 * 1000;
        NewChannel.maxDelayTime = n2 * 1000;
        NewChannel.volume = 1.f;
        break;
    case 3:
        NewChannel.minDelayTime = n1 * 1000;
        NewChannel.maxDelayTime = n2 * 1000;
        NewChannel.volume = f1;
        break;
    }
    *(--col) = 0; // truncate at first ','
    NewChannel.soundName = tempString2;
    NewChannel.looped = _looped;

    if (_looped)
        NewChannel.timeToNextPlay = 0;
    else
        NewChannel.timeToNextPlay =
            static_cast<int32_t>(rand(static_cast<float>(NewChannel.maxDelayTime - NewChannel.minDelayTime)));

    SoundSchemeChannels.push_back(NewChannel);
    return true;
}

void SoundService::ProcessSoundSchemes()
{
    // handle schemes
    const uint32_t dTime = core.GetDeltaTime();

    for (size_t i = 0; i < SoundSchemeChannels.size(); i++)
    {
        if (SoundSchemeChannels[i].looped)
        {
            if (SoundSchemeChannels[i].timeToNextPlay)
                continue;
            SoundSchemeChannels[i].timeToNextPlay = -1;
            SoundPlay(SoundSchemeChannels[i].soundName.c_str(), PCM_STEREO, VOLUME_FX, false, true, false, 0, nullptr,
                      -1.f, -1.f, 0, SoundSchemeChannels[i].volume);
        }
        else
        {
            if (SoundSchemeChannels[i].timeToNextPlay > 0)
            {
                SoundSchemeChannels[i].timeToNextPlay -= dTime;
            }
            else
            {
                SoundSchemeChannels[i].timeToNextPlay =
                    SoundSchemeChannels[i].minDelayTime +
                    static_cast<int32_t>(rand(
                        static_cast<float>(SoundSchemeChannels[i].maxDelayTime - SoundSchemeChannels[i].minDelayTime)));
                SoundPlay(SoundSchemeChannels[i].soundName.c_str(), PCM_STEREO, VOLUME_FX, false, false, false, 0,
                          nullptr, -1.f, -1.f, 0, SoundSchemeChannels[i].volume);
            }
        } // looped
    }
}
