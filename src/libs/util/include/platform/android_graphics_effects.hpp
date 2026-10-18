#pragma once
#include "platform/android_ui.hpp"

namespace storm::android {
// Process-lifetime snapshots: preference edits require a game restart. Read JNI
// once per effect, never once per character/frame or during render-target changes.
inline bool CharacterShadowsEnabled() {
    static const bool enabled = graphicsSetting("character_shadows", 1) != 0;
    return enabled;
}
inline bool SeaFoamEnabled() {
    static const bool enabled = graphicsSetting("sea_foam", 1) != 0;
    return enabled;
}
}
