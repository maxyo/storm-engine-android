#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace storm::android
{
// MainMenu and Character are the game's keys (Esc, F2). CharacterScreen and ShipScreen open
// that screen itself, whatever section F2 would reopen: a HUD portrait or squadron ship tap
// (the UI then selects its subject in the screen).
enum class GameplayShortcut { MainMenu, Character, CharacterScreen, ShipScreen };

// Names are control identifiers consumed by the game's existing
// "Control Activation" handler. Never accept arbitrary script event/function names.
inline const char *shortcutControl(GameplayShortcut shortcut)
{
    switch (shortcut)
    {
    case GameplayShortcut::MainMenu: return "MainMenu";
    case GameplayShortcut::Character: return "Interface";
    case GameplayShortcut::CharacterScreen:
    case GameplayShortcut::ShipScreen: return nullptr;
    }
    return nullptr;
}
// The screen of a shortcut without a control: the argument of the app script adapter's
// "AndroidOpenScreen" event (mobile-ui/PROGRAM/interface/android_open_screen.c), which opens
// it under the same refusals as the F2 key.
inline const char *shortcutScreen(GameplayShortcut shortcut)
{
    switch (shortcut)
    {
    case GameplayShortcut::CharacterScreen: return "character";
    case GameplayShortcut::ShipScreen: return "ship";
    case GameplayShortcut::MainMenu:
    case GameplayShortcut::Character: return nullptr;
    }
    return nullptr;
}

class GameplayShortcuts
{
    struct Pending { GameplayShortcut shortcut; uint32_t queuedAt; };
    std::optional<Pending> pending_;

    void expire(uint32_t now)
    {
        // Unsigned subtraction also handles SDL_GetTicks wrapping.
        if (pending_ && static_cast<uint32_t>(now - pending_->queuedAt) >= lifetimeMs)
            pending_.reset();
    }

  public:
    static constexpr uint32_t lifetimeMs = 30000;

    bool offer(std::string_view name, uint32_t now)
    {
        GameplayShortcut shortcut;
        if (name == "mainmenu") shortcut = GameplayShortcut::MainMenu;
        else if (name == "character") shortcut = GameplayShortcut::Character;
        else if (name == "characterscreen") shortcut = GameplayShortcut::CharacterScreen;
        else if (name == "shipscreen") shortcut = GameplayShortcut::ShipScreen;
        else return false;
        expire(now);
        // Only one menu intent may be outstanding. Coalesce repeats and reject
        // conflicting commands instead of opening multiple screens from a batch.
        if (pending_) return pending_->shortcut == shortcut;
        pending_ = Pending{shortcut, now};
        return true;
    }

    std::optional<GameplayShortcut> take(uint32_t now, bool ready)
    {
        expire(now);
        if (!ready || !pending_) return std::nullopt;
        const auto shortcut = pending_->shortcut;
        pending_.reset(); // Consume before calling scripts, even if they refuse the action.
        return shortcut;
    }

    void clear() { pending_.reset(); }
};
} // namespace storm::android
