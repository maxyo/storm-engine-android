#pragma once
// The UI's dialogue watchdog (src/libs/ui/src/hud/hud.cpp kDialogueLost) found the game in a
// dialogue that no panel shows: the player cannot answer it and nothing else ends it. A safety
// net, never the fix: every end is an ERROR with what the dialogue was, so its cause gets fixed.
// It ends the dialogue the way the game ends a broken one: the script event "EmergencyDialogExit"
// (dialog.c: #event_handler EmergencyDialogExit → DialogExit, raised by DIALOG::EmergencyExit for
// invalid links). DialogExit frees the DIALOG entity, whose destructor resumes time; DialogMain
// stops time before the entity exists, so without one (the dialogue never began) it is resumed here.
#include <core.h>
#include <v_data.h>

#include <spdlog/spdlog.h>

#include <initializer_list>
#include <string>
#include <string_view>

namespace storm::android {

// `variable`.`path` of a script object (a reference followed), "<none>" where there is none.
inline std::string dialogueScriptText(const char *variable, std::initializer_list<const char *> path)
{
    auto *value = static_cast<VDATA *>(core.GetScriptVariable(variable));
    ATTRIBUTES *attributes = value ? value->GetAClass() : nullptr;
    const char *text = nullptr;
    for (auto step = path.begin(); attributes && step != path.end(); ++step)
        if (step + 1 == path.end())
            text = attributes->GetAttribute(*step);
        else
            attributes = attributes->GetAttributeClass(*step);
    return text ? text : "<none>";
}

inline void endUnseenDialogue(std::string_view report)
{
    auto *run = static_cast<VDATA *>(core.GetScriptVariable("dialogRun"));
    const bool running = run && run->GetInt() != 0;
    const bool entity = core.GetEntityId("DIALOG") != invalid_entity;
    if (!running)
    {
        spdlog::error("dialogue watchdog: {}; dialogRun={}: no dialogue runs, nothing to end", report,
                      run ? std::to_string(run->GetInt()) : std::string("<missing>"));
        return;
    }
    spdlog::error("dialogue watchdog: {}; DIALOG entity={} node={} character={} (file {}, its node {}); ending it (EmergencyDialogExit)",
                  report, entity ? 1 : 0, dialogueScriptText("Dialog", {"CurrentNode"}), dialogueScriptText("CharacterRef", {"id"}),
                  dialogueScriptText("CharacterRef", {"Dialog", "Filename"}), dialogueScriptText("CharacterRef", {"Dialog", "CurrentNode"}));
    core.Event("EmergencyDialogExit");
    if (!entity)
        core.SetTimeScale(1.f);
}

} // namespace storm::android
