// Textures that get no parallax whatever their mask says: the shipped list (data, src/parallax_exclusions.inc) and the
// owner's tuning file (the user list, in the app's files directory: his choices from the in-game debug view, not a
// cache; folded into the shipped list later).
//
// Both are text, one texture a line, `name` as NormalizeTexturePath gives it ("locations/bump_city"); '#' starts a
// comment. The shipped list holds names only (each means: no parallax). The user list holds `exclude <name>` and
// `include <name>` lines (include: the parallax whatever the shipped list or the trust coverage says); the last line
// for a name wins, and a toggle appends one. Both lists are logged when loaded, and every toggle when made.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace storm::fx
{

class ParallaxExclusions
{
  public:
    enum class Verdict
    {
        Default,  // the trust coverage decides
        Excluded, // the shipped list or the user's exclude
        Included, // the user's include
    };

    // The shipped list parsed from its text (ShippedText()); lines that are not a name are reported in `problems`.
    void LoadShipped(std::string_view text, std::vector<std::string> *problems = nullptr);
    // The user file; a missing file is an empty list (false: it exists and cannot be read).
    bool LoadUser(const std::filesystem::path &file);

    Verdict Of(std::string_view normalized_name) const;
    size_t shipped_count() const { return shipped_.size(); }
    size_t user_count() const { return user_.size(); }

    // Appends the user's choice for a name to the user file (created when missing); the new verdict. `parallax`: the
    // parallax wanted. False when the file cannot be written (nothing changes).
    bool SetUser(std::string_view normalized_name, bool parallax);

    static std::string_view ShippedText();

  private:
    std::unordered_map<std::string, bool> shipped_;
    std::unordered_map<std::string, bool> user_; // true: include
    std::filesystem::path user_file_;
};

} // namespace storm::fx
