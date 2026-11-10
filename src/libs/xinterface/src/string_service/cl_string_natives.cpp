// String natives Caribbean Legend 2.0 declares in Program/interface/utilite.c before
// #libriary "script_interface_functions" (SCRIPT_INTERFACE_FUNCTIONS::Init registers them):
// - LanguageGetFileStringsQuantity(id): number of strings in a file opened with LanguageOpenFile.
// - GetRichTextWidth(text, font, scale): pixel width of a one-line text in a font. CL passes
//   announcement texts that may carry inline <...> markup; the markup is not drawn, so it is left
//   out of the measured text.

#include "cl_string_natives.h"

#include "str_service.h"

#include "core.h"
#include "dx9render.h"
#include "script_stub.hpp"
#include "v_s_stack.h"

#include <string>

#include <spdlog/spdlog.h>

namespace
{
uint32_t languageGetFileStringsQuantity(VS_STACK *s)
{
    auto *v = static_cast<VDATA *>(s->Pop());
    const int32_t id = v ? v->GetInt() : -1;
    auto *strings = static_cast<STRSERVICE *>(core.GetService("STRSERVICE"));
    const int32_t count = strings ? strings->GetUsersStringsQuantity(id) : -1;
    if (count < 0)
        spdlog::error("LanguageGetFileStringsQuantity: no open string file {}; returns 0", id);
    auto *r = static_cast<VDATA *>(s->Push());
    if (!r)
        return IFUNCRESULT_FAILED;
    r->Set(count < 0 ? 0 : count);
    return IFUNCRESULT_OK;
}

std::string withoutMarkup(const char *text)
{
    std::string out;
    bool inTag = false;
    for (const char *p = text; p && *p; ++p)
    {
        if (*p == '<')
            inTag = true;
        else if (*p == '>' && inTag)
            inTag = false;
        else if (!inTag)
            out += *p;
    }
    return out;
}

uint32_t getRichTextWidth(VS_STACK *s)
{
    auto *vScale = static_cast<VDATA *>(s->Pop());
    auto *vFont = static_cast<VDATA *>(s->Pop());
    auto *vText = static_cast<VDATA *>(s->Pop());
    float scale = 1.0f;
    if (vScale)
        vScale->Get(scale);
    const char *font = vFont ? vFont->GetString() : nullptr;
    const char *text = vText ? vText->GetString() : nullptr;
    int32_t width = 0;
    auto *rs = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
    if (rs && font && *font)
    {
        const int32_t id = rs->LoadFont(font);
        if (id >= 0)
        {
            width = rs->StringWidth(withoutMarkup(text).c_str(), id, scale);
            rs->UnloadFont(font);
        }
        else
            spdlog::error("GetRichTextWidth: font '{}' cannot be loaded; returns 0", font);
    }
    auto *r = static_cast<VDATA *>(s->Push());
    if (!r)
        return IFUNCRESULT_FAILED;
    r->Set(width);
    return IFUNCRESULT_OK;
}
} // namespace

void registerClStringNatives()
{
    storm::registerScriptFunction("LanguageGetFileStringsQuantity", "int", 1, languageGetFileStringsQuantity);
    storm::registerScriptFunction("GetRichTextWidth", "int", 3, getRichTextWidth);
}
