#pragma once
#include "core.h"
#include "v_data.h"
#include "attributes.h"
#include <string>

namespace storm::android {
// Game-thread-only transient choice. Never restored from a save or preferences.
struct NewGameChoice {
    uint64_t revision = 0;
    bool selected = false;
    void reset(uint64_t next) { revision = next; selected = false; }
    bool apply(uint64_t current, const std::string &value) {
        if (current != revision) return false;
        if (value == "1@" + std::to_string(revision)) selected = true;
        else if (value == "0@" + std::to_string(revision)) selected = false;
        else return false;
        return true;
    }
};
inline NewGameChoice tutorialSkip;
inline bool TutorialSkipSupported() {
    auto *v = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates"));
    auto *attributes = v ? v->GetAClass() : nullptr;
    auto *supported = attributes ? attributes->FindAClass(attributes, "AndroidTutorialSkipSupported") : nullptr;
    auto *hero = static_cast<VDATA *>(core.GetScriptVariable("startHeroType"));
    int32_t type = 0;
    return supported && supported->HasValue() && supported->GetValue() == "1" &&
        hero && hero->Get(type) && type == 1;
}
inline void ApplyNewGameChoiceAfterFrame() {
    if (!tutorialSkip.selected) return;
    if (!TutorialSkipSupported()) { tutorialSkip.selected = false; return; }
    auto *v = static_cast<VDATA *>(core.GetScriptVariable("pchar"));
    auto *p = v ? v->GetAClass() : nullptr;
    auto *pending = p ? p->FindAClass(p, "PostEventQuest.questDelay.PostVideo_Start") : nullptr;
    if (!pending) return;
    tutorialSkip.selected = false; // consume once, before any script dispatch
    auto *video = p->FindAClass(p, "GenQuest.VideoAfterQuest");
    auto *hero = static_cast<VDATA *>(core.GetScriptVariable("startHeroType"));
    int32_t type = 0;
    if (!hero || !hero->Get(type) || type != 1 || !video || !video->HasValue() || video->GetValue() != "Sharlie_Start") return;
    // DoQuestDelayExit invokes this function in the normal game-thread scheduler.
    // This runs after NewGame_continue, before the next ProcessFrame can dispatch
    // its newly posted intro event. ClearEvents therefore cannot lose our handler.
    pending->SetAttribute("function", "AndroidStartWithoutTutorial");
}
}
