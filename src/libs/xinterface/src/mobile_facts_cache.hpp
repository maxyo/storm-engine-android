#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace storm::mobile
{

// The facts of the application's interface adapter (event AndroidInterfaceFacts, xinterface
// MobileCollectFacts) are computed by scripts from script state alone: attributes (characters,
// items, GameInterface) and whether the interface's segment is loaded. The event ran on every
// interface frame, opening and parsing language files each time, to find the same facts. They are
// collected again only when that input may have changed:
// - the opening (a new interface, or the same one opened again),
// - the character the interface shows (xi_refCharacter),
// - the game language,
// - script state: storm::ScriptStateGeneration (core attributes.h) counts every attribute write
//   that changes something and every segment load or unload, whoever does it (an event handler,
//   a message to the interface, the player's input reaching a script, the interface's own
//   "frame" handler, a node writing GameInterface.<node>.current).
// The generation is taken after the adapter ran, so its own writes (GameInterface.AndroidFacts)
// do not count as a change.
template <typename Facts> class FactsCache
{
  public:
    struct Key
    {
        uint64_t opening = 0;
        int subject = -1;
        std::string language;
        uint64_t state = 0;

        bool operator==(const Key &) const = default;
    };

    // The facts collected for `key`, or nullptr when they have to be collected again.
    const Facts *Find(const Key &key) const
    {
        return valid_ && key == key_ ? &facts_ : nullptr;
    }

    // `key.state` is the generation after the collection.
    const Facts &Store(Key key, Facts facts)
    {
        if (key.opening != key_.opening)
            collections_ = 0;
        ++collections_;
        key_ = std::move(key);
        facts_ = std::move(facts);
        valid_ = true;
        return facts_;
    }

    // Collections in the current opening (diagnostics).
    uint64_t Collections() const noexcept
    {
        return collections_;
    }

  private:
    bool valid_ = false;
    Key key_;
    Facts facts_{};
    uint64_t collections_ = 0;
};

} // namespace storm::mobile
