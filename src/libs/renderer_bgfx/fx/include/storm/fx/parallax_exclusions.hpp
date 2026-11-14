// The parallax lists: the shipped one (data, src/parallax_exclusions.inc) and the owner's tuning file (the user list, in
// the app's files directory: his choices from the in-game debug view, not a cache; folded into the shipped list later).
// A texture takes the parallax by default when its class does (ReliefTakesParallax: masonry) and its trusted share
// reaches kParallaxMinCoverage; the lists decide the exceptions either way.
//
// Both are text, one texture a line: `include <name>` (the parallax whatever its class and coverage say) or `exclude <name>`
// (none), the name as NormalizeTexturePath gives it ("locations/bump_city"); '#' starts a comment. The user list wins over the
// shipped one, and the last line for a name wins; a toggle appends one. Both lists are logged when loaded, and every
// toggle when made.
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
        Default,  // the class and the trust coverage decide
        Excluded, // `exclude` in a list
        Included, // `include` in a list
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
    std::unordered_map<std::string, bool> shipped_; // true: include
    std::unordered_map<std::string, bool> user_; // true: include
    std::filesystem::path user_file_;
};

} // namespace storm::fx
