// Effect ids of Caribbean Legend (Resource/Effects/effects.json): id -> file + playback speed.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace storm::effects
{

inline constexpr std::string_view kEffectsDirectory = "resource/effects/";
inline constexpr std::string_view kEffectsTable = "resource/effects/effects.json";

struct EffectRecord
{
    enum class Kind
    {
        effekseer,       // .efkefc, played by the Effects entity
        particle_system, // .xps, created by the "particles" entity
    };
    Kind kind = Kind::effekseer;
    std::string file;  // as named in the table, e.g. "CLTorchFire.efkefc" or "Fire.xps"
    float speed = 1.0f;

    // Game path of an Effekseer effect ("resource/effects/<file>").
    [[nodiscard]] std::string EffectPath() const;
};

// The table, loaded on first use through the file service. Ids are case-insensitive, as script
// strings are compared everywhere else in the engine.
class EffectCatalog
{
  public:
    // Ids that are not in the table are legacy particle system names (TEHO scripts and CL
    // quests call CreateParticleSystem("torch", ...)); the caller decides what to do with them.
    [[nodiscard]] std::optional<EffectRecord> Find(std::string_view id);
    // Re-reads effects.json. Returns false (and logs) when it cannot be read or parsed.
    bool Reload();

    [[nodiscard]] const std::map<std::string, EffectRecord> &Records();

  private:
    void EnsureLoaded();

    bool loaded_ = false;
    std::map<std::string, EffectRecord> records_; // key: lower-case id
};

EffectCatalog &Catalog();

// Reads a whole game file through the file service (case-insensitive resource paths).
bool ReadGameFile(const std::string &path, std::string &data);

} // namespace storm::effects
