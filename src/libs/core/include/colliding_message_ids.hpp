#pragma once

// Message and parameter ids whose value means different things in To Each His Own and Caribbean
// Legend 2.0 (both games' script headers fix the values, see shared/messages.h and shared/sound.h).
// A compile-time constant cannot serve both games, so the one place that dispatches each value
// asks collidingIdMeaning() with storm::gameProfile() and handles the meaning of the running game.
// Ids outside this table mean the same in both games.

#include "game_profile.hpp"

#include <cstdint>
#include <string_view>

namespace storm
{
enum class CollidingIdMeaning
{
    None, // not a colliding id
    WhrLightningAdd,  // TEHO MSG_WHR_LIGHTNING_ADD (lightning entity)
    WhrUpdateState,   // CL MSG_WHR_UPDATE_STATE (weather entity)
    SoundResume,      // TEHO MSG_SOUND_RESUME "lll": id, fade time
    SoundPause,       // CL MSG_SOUND_PAUSE "lll": id, paused
    SoundTypeMp3_3D,  // TEHO SOUND_MP3_3D (SoundPlay type)
    SoundTypeFmodEvent, // CL SOUND_FMOD_EVENT (SoundPlay type; CL 2.0 scripts never pass it)
};

enum class CollidingIdDomain
{
    Message,       // first long of an entity message
    SoundPlayType, // the type argument of MSG_SOUND_PLAY
};

struct CollidingId
{
    CollidingIdDomain domain;
    int32_t value;
    CollidingIdMeaning teho;
    CollidingIdMeaning caribbean_legend;
};

inline constexpr CollidingId kCollidingIds[] = {
    {CollidingIdDomain::Message, 54000, CollidingIdMeaning::WhrLightningAdd, CollidingIdMeaning::WhrUpdateState},
    {CollidingIdDomain::Message, 77012, CollidingIdMeaning::SoundResume, CollidingIdMeaning::SoundPause},
    {CollidingIdDomain::SoundPlayType, 3, CollidingIdMeaning::SoundTypeMp3_3D, CollidingIdMeaning::SoundTypeFmodEvent},
};

constexpr CollidingIdMeaning collidingIdMeaning(GameProfile profile, CollidingIdDomain domain, int32_t value)
{
    for (const auto &id : kCollidingIds)
        if (id.domain == domain && id.value == value)
            return profile == GameProfile::CaribbeanLegend ? id.caribbean_legend : id.teho;
    return CollidingIdMeaning::None;
}

constexpr std::string_view collidingIdName(CollidingIdMeaning meaning)
{
    switch (meaning)
    {
    case CollidingIdMeaning::WhrLightningAdd:
        return "MSG_WHR_LIGHTNING_ADD";
    case CollidingIdMeaning::WhrUpdateState:
        return "MSG_WHR_UPDATE_STATE";
    case CollidingIdMeaning::SoundResume:
        return "MSG_SOUND_RESUME";
    case CollidingIdMeaning::SoundPause:
        return "MSG_SOUND_PAUSE";
    case CollidingIdMeaning::SoundTypeMp3_3D:
        return "SOUND_MP3_3D";
    case CollidingIdMeaning::SoundTypeFmodEvent:
        return "SOUND_FMOD_EVENT";
    case CollidingIdMeaning::None:
        break;
    }
    return "";
}
} // namespace storm
