// The engine's side of the portable UI on Android: the UI host lives in this process and
// draws inside the engine frame (src/libs/ui_host). Engine code publishes its states and
// interface models here, on the game thread, in-process; nothing of it crosses JNI.
// Implemented by src/libs/ui_host/src/engine_ui.cpp.
#pragma once
#ifdef __ANDROID__
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace storm::android::ui
{
// A state channel of the portable UI: JSON as the UI host's SetState takes it ("null" =
// nothing). Kept until the host exists, then delivered in order.
void state(std::string_view channel, std::string json);
// The open interface opened (`screen` not empty, `revision` of the opening) or closed.
void screen(const std::string &screen, uint64_t revision);
// Interface model (widget snapshot) of the open screen; the newest replaces older ones.
void interfaceModel(std::string json);
// A passing line of the game's log: a toast over the game.
void toast(std::string text);
// Something the player asked for failed: a lasting danger toast of the pack string `string_key`.
void alert(std::string string_key);
// Top of every engine loop iteration, before the queued commands are read: starts the host
// once the renderer is ready and applies what the platform thread sent (touches, presentation).
void tick();
// Engine commands the UI produced since the last call ("widget:…", "hold:…"), oldest first;
// a latest-value slot (stick position) appears once, with its newest command.
std::vector<std::string> takeCommands();
// Increments when a UI host starts (0 before the first): publishers whose commands carry an
// activity generation start a new one, and republish, for every new host.
uint64_t generation();
// Before the renderer goes away at engine exit.
void shutdown();
} // namespace storm::android::ui
#endif
