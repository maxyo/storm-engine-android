// Tavern mini-games (interfaces "boal_dice" and "boal_cards", mockups V2-Extra-Dice*,
// V2-Extra-Cards*): the 2D felt of the original with our own dice and cards. The game draws
// its pieces with script images and keeps the turn in script variables; the engine exports
// both (xinterface mobile_table_game.hpp): images by their script name, the money lines,
// and the marker ANDROID_TABLE_GAME whose context holds the facts read here. Fills <s-table-game>.
//
// The rules stay in the script. A tap on our die is B_HeroDice<n>, which charges the stake
// at once (ClickHeroDice) — the marked die rises with «−stake» and cannot be unmarked.
// B_PACK throws the cup / takes a card / starts another round, B_ICON_1 keeps the dice /
// stands, EXIT_BTN leaves (the script lets it only between rounds).
#include "controls.h"

#include <array>
#include <cctype>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"B_PACK", "button", true},
    {"ANDROID_TABLE_GAME", "table_game", true},
    {"EXIT_BTN", "button"},
    {"B_ICON_1", "button"},
    {"B_ICON_2", "button"}, // the hero's portrait: no command in either game
    {"B_HeroDice*", "button"},
    {"INFO_TEXT", "text"},
    {"Money", "text"},
    {"MoneyInChest", "text"},
    {"ICON_*", "image"},
    {"HeroDice*", "image"},
    {"CompDice*", "image"},
    {"Dice*", "image"}, // dice rolling over the felt: shown as they settle in the rows
    {"DiceCup", "image"},
    {"PCard*", "image"},
    {"Pack", "image"},
    {"BLANK", "image"}, // the card flying from the deck
    {"Money_*", "image"},
    {"GOLD", "image"},
};

int Int(const Widget &marker, const char *key)
{
    const auto found = marker.context.find(key);
    return found == marker.context.end() ? 0 : std::atoi(found->second.c_str());
}

std::string Str(const Widget &marker, const char *key)
{
    const auto found = marker.context.find(key);
    return found == marker.context.end() ? std::string() : found->second;
}

std::vector<std::string> List(const std::string &text)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (!text.empty() && at <= text.size())
    {
        const size_t end = std::min(text.find(',', at), text.size());
        out.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

// 1500 → "1 500".
std::string Spaced(int value)
{
    std::string digits = std::to_string(std::abs(value)), out;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (i && (digits.size() - i) % 3 == 0)
            out += ' ';
        out += digits[i];
    }
    return value < 0 ? "−" + out : out;
}

// "кубик / кубика / кубиков": the Russian three forms; other languages one / many.
std::string Counted(Bindings &bindings, const std::string &language, int n, const char *one, const char *few, const char *many)
{
    const char *key = many;
    if (language == "russian")
    {
        const int tens = n % 100, units = n % 10;
        key = tens >= 11 && tens <= 14 ? many : units == 1 ? one : units >= 2 && units <= 4 ? few : many;
    }
    else if (n == 1)
        key = one;
    return std::to_string(n) + " " + bindings.Text(key);
}

// Pip centres (% of the die) of faces 1…6.
Json Pips(int face)
{
    static const std::vector<std::array<int, 2>> kFaces[6] = {
        {{50, 50}},
        {{28, 28}, {72, 72}},
        {{26, 26}, {50, 50}, {74, 74}},
        {{26, 26}, {74, 26}, {26, 74}, {74, 74}},
        {{26, 26}, {74, 26}, {50, 50}, {26, 74}, {74, 74}},
        {{28, 24}, {72, 24}, {28, 50}, {72, 50}, {28, 76}, {72, 76}},
    };
    Json pips = Json::array();
    if (face >= 1 && face <= 6)
        for (const auto &pip : kFaces[face - 1])
            pips.push_back(Json::array({pip[0], pip[1]}));
    return pips;
}

constexpr int kTilt[] = {-3, 2, -2, 3, -1, 2, -3, 1};

const Widget *Image(const Admission &admitted, const std::string &node)
{
    const Widget *image = admitted.One(node);
    return image && image->type == "image" ? image : nullptr;
}

// "dice_3_1" → 3.
int DieFace(const std::string &picture)
{
    return picture.rfind("dice_", 0) == 0 ? std::atoi(picture.c_str() + 5) : 0;
}

// Points of a card, PROGRAM/characters/characters_init.c (NullCharacter.Cards.*.count).
int CardPoints(const std::string &rank)
{
    if (rank == "A")
        return 11;
    if (rank == "K")
        return 4;
    if (rank == "Q")
        return 3;
    if (rank == "J")
        return 2;
    return std::atoi(rank.c_str());
}

class TableGameScreen final : public Screen
{
  public:
    explicit TableGameScreen(bool dice) : dice_(dice)
    {
    }
    const char *Component() const override
    {
        return "s-table-game";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget &marker = *admitted.One("ANDROID_TABLE_GAME");
        if (!marker.unavailable_reason.empty() && marker.unavailable_reason != reported_)
        {
            reported_ = marker.unavailable_reason;
            bindings.Error("table game: " + reported_);
        }
        const std::string language = admitted.model->language;
        const int rate = Int(marker, "rate"), money = Int(marker, "moneyHero"), bank = Int(marker, "bank");
        const int stage = Int(marker, "stage"), turn = Int(marker, "turn"), first = Int(marker, "first");
        const bool open = Int(marker, "open") != 0;
        const Widget *pack = admitted.One("B_PACK");
        const Widget *pass = admitted.One("B_ICON_1");
        const Widget *exit = admitted.One("EXIT_BTN");

        Json view = Json::object();
        view["kind"] = dice_ ? "dice" : "cards";
        view["title"] = bindings.Text(dice_ ? "games.dice" : "games.cards");
        view["stats"] = Json::array({{{"label", bindings.Text("games.stake")}, {"value", Spaced(rate)}},
                                     {{"label", bindings.Text("games.money")}, {"value", Spaced(money)}}});
        view["locked"] = !open;

        // The bank, and after a round what it came to: we remember the money and the bank of
        // the round (the dice script empties the bank when the round ends).
        const std::string opponent = Str(marker, "opponent");
        Json table = {{"opponent", opponent}, {"line", admitted.Label("INFO_TEXT")}, {"bankLabel", ""}, {"bankTone", "gold"}};
        if (const std::string face = Str(marker, "opponentFace"); !face.empty())
            table["portrait"] = "INTERFACES/PORTRAITS/128/face_" + face;
        table["coins"] = "silver";
        for (const Widget *widget : admitted.known)
            if (widget->type == "image" && Upper(widget->node).rfind("MONEY_", 0) == 0)
            {
                table["coins"] = widget->label == "gold" ? "gold" : "silver";
                break;
            }
        if (!open)
        {
            round_money_ = money;
            if (bank > 0)
                round_bank_ = bank;
            table["bank"] = bank > 0 ? Spaced(bank) : "";
        }
        else if (round_money_ >= 0 && money > round_money_)
        {
            table["bankLabel"] = bindings.Text("games.won");
            table["bank"] = "+" + Spaced(money - round_money_);
            table["bankTone"] = "win";
        }
        else if (round_money_ >= 0 && round_bank_ > 0)
        {
            table["bankLabel"] = bindings.Text("games.taken") + (opponent.empty() ? "" : " " + opponent.substr(0, opponent.find(' ')));
            table["bank"] = Spaced(round_bank_);
            table["bankTone"] = "loss";
        }
        else
            table["bank"] = "";
        view["table"] = table;

        const std::string wait = bindings.Text("games.wait");
        Json buttons = Json::array();
        const auto button = [&](const char *id, const std::string &label, const std::string &sub, const char *variant,
                                bool enabled, const std::string &reason) {
            buttons.push_back({{"id", id}, {"label", label}, {"sub", sub}, {"variant", variant}, {"disabled", !enabled},
                               {"reason", enabled ? std::string() : reason}, {"busy", bindings.Pending(id)}});
        };
        // Between rounds: another round (B_PACK) or leave; the script says when the opponent quits.
        const auto between = [&](bool again) {
            const std::string stake = bindings.Text("games.stake_short") + " " + Spaced(rate);
            if (again)
            {
                button("leave", bindings.Text("games.leave"), "", "secondary", exit != nullptr, "");
                button("again", bindings.Text("games.again"), stake, "primary", true, "");
            }
            else
            {
                button("again", bindings.Text("games.again"), "", "secondary", false, "");
                button("leave", bindings.Text("games.leave"), "", "primary", exit != nullptr, "");
            }
        };

        Json pack_pieces = Json::array();
        if (dice_)
        {
            const bool locked = Int(marker, "locked") != 0;
            const std::vector<std::string> hero = List(Str(marker, "heroDice")), mix = List(Str(marker, "heroMix"));
            const bool throw_first = !open && turn == 1 && !locked && stage < 2;
            const bool reroll = !open && turn == 1 && !locked && ((first == 1 && stage == 2) || (first == -1 && stage == 3));
            int marked = 0;
            Json ours = Json::array();
            bool all_ours = true;
            for (int n = 1; n <= 5; ++n)
            {
                const Widget *image = Image(admitted, "HeroDice" + std::to_string(n));
                const bool in_cup = n <= static_cast<int>(mix.size()) && mix[n - 1] == "1";
                const int remembered = n <= static_cast<int>(hero.size()) ? std::atoi(hero[n - 1].c_str()) : 0;
                if (in_cup && reroll)
                    ++marked;
                if (!image && !(reroll && in_cup && remembered))
                {
                    all_ours = false;
                    continue;
                }
                Json die = {{"kind", "die"}, {"rot", kTilt[n - 1]}};
                if (image)
                {
                    die["pips"] = Pips(DieFace(image->label));
                    const Widget *hit = admitted.One("B_HeroDice" + std::to_string(n));
                    if (hit && !open)
                    {
                        die["action"] = "mark";
                        die["value"] = std::to_string(n);
                        const bool can = reroll && money >= rate;
                        die["disabled"] = !can;
                        die["reason"] = !reroll ? wait : bindings.Text("games.no_money");
                    }
                }
                else
                {
                    die["pips"] = Pips(remembered);
                    die["lift"] = true;
                    die["cost"] = "−" + Spaced(rate);
                    all_ours = false;
                }
                ours.push_back(std::move(die));
            }
            if (throw_first)
                marked = 5;
            Json theirs = Json::array();
            bool all_theirs = true;
            for (int n = 1; n <= 5; ++n)
                if (const Widget *image = Image(admitted, "CompDice" + std::to_string(n)))
                    theirs.push_back({{"kind", "die"}, {"rot", kTilt[n + 2]}, {"pips", Pips(DieFace(image->label))}});
                else
                    all_theirs = false;
            const auto combo = [&](const char *key) {
                const int type = Int(marker, key);
                return type >= 1 && type <= 8 ? bindings.Text("games.combo_" + std::to_string(type)) : std::string();
            };
            view["top"] = {{"pieces", theirs}, {"label", all_theirs ? combo("compType") : ""}, {"size", 44}, {"below", true}};
            view["bottom"] = {{"pieces", ours}, {"label", all_ours ? combo("heroType") : ""}, {"size", 60}, {"costs", true}};

            const bool roll = (throw_first || reroll) && marked > 0;
            const bool keep = reroll && marked == 0;
            pack_pieces.push_back({{"kind", "cup"}, {"action", "press"}, {"value", ""}, {"disabled", !roll && !(open && !locked)}, {"reason", open ? "" : wait}});
            if (open)
                between(!locked);
            else if (keep)
                button("throw", bindings.Text("games.keep"), "", "primary", pass != nullptr, wait);
            else
                button("throw", bindings.Text("games.roll"),
                       roll ? Counted(bindings, language, marked, "games.die_one", "games.die_few", "games.die_many") : "",
                       "primary", roll, wait);

            std::array<const Widget *, 5> hits{};
            for (int n = 1; n <= 5; ++n)
                hits[n - 1] = admitted.One("B_HeroDice" + std::to_string(n));
            bindings.On("bottom", "mark", [&bindings, hits](const Event &event) {
                const int n = std::atoi(event.value.c_str());
                if (n >= 1 && n <= 5 && hits[n - 1])
                    bindings.Send(*hits[n - 1], "press");
            });
            bindings.On("throw", "", [&bindings, pack, pass, keep](const Event &) {
                const Widget *target = keep ? pass : pack;
                if (target)
                    bindings.Send(*target, "press");
            });
            bindings.On("pack", "", [&bindings, pack](const Event &) { bindings.Send(*pack, "press"); });
        }
        else
        {
            const bool moving = Int(marker, "cardMove") != 0;
            const bool playing = !open && turn == 1 && stage >= 2 && stage != 100;
            Json ours = Json::array(), theirs = Json::array();
            int our_points = 0, their_points = 0;
            bool hidden = false;
            for (int n = 0; n < 36; ++n)
            {
                const Widget *image = Image(admitted, "PCard" + std::to_string(n));
                if (!image)
                    continue;
                Json &row = n < 18 ? ours : theirs;
                const std::string &picture = image->label;
                const size_t cut = picture.find('_');
                Json card = {{"rot", kTilt[row.size() % 8] * 2}};
                if (cut == std::string::npos)
                {
                    card["kind"] = "back";
                    hidden = hidden || n >= 18;
                }
                else
                {
                    const std::string suit = picture.substr(0, cut), rank = picture.substr(cut + 1);
                    card["kind"] = "card";
                    card["suit"] = suit;
                    card["red"] = suit == "hearts" || suit == "diamonds";
                    card["rank"] = rank.size() == 1 && !std::isdigit(static_cast<unsigned char>(rank[0]))
                                       ? bindings.Text("games.rank_" + rank)
                                       : rank;
                    (n < 18 ? our_points : their_points) += CardPoints(rank);
                }
                row.push_back(std::move(card));
            }
            const auto points = [&](int n) {
                return Counted(bindings, language, n, "games.point_one", "games.point_few", "games.point_many");
            };
            view["top"] = {{"pieces", theirs}, {"label", !theirs.empty() && !hidden ? points(their_points) : ""}, {"size", 90}};
            view["bottom"] = {{"pieces", ours}, {"label", ours.empty() ? "" : points(our_points)}, {"size", 90}};
            const bool take = playing && !moving && money >= rate;
            if (Image(admitted, "Pack"))
                pack_pieces.push_back({{"kind", "deck"}, {"action", "press"}, {"value", ""}, {"disabled", !take && !open},
                                       {"reason", playing ? bindings.Text("games.no_money") : wait}});
            if (open)
                between(stage != 100);
            else
            {
                button("stand", bindings.Text("games.enough"), "", "secondary", playing && pass, wait);
                button("take", bindings.Text("games.take"), take ? "−" + Spaced(rate) : "", "primary", take,
                       playing ? bindings.Text("games.no_money") : wait);
            }
            bindings.On("take", "", [&bindings, pack](const Event &) { bindings.Send(*pack, "press"); });
            bindings.On("stand", "", [&bindings, pass](const Event &) {
                if (pass)
                    bindings.Send(*pass, "press");
            });
            bindings.On("pack", "", [&bindings, pack](const Event &) { bindings.Send(*pack, "press"); });
        }
        view["pack"] = pack_pieces;
        view["buttons"] = buttons;

        bindings.On("again", "", [&bindings, pack](const Event &) { bindings.Send(*pack, "press"); });
        bindings.On("leave", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        bindings.On("header-back", "", [&bindings, exit, open](const Event &) {
            if (open)
                bindings.Back(exit);
            else
                bindings.Toast(bindings.Text("games.round"));
        });
        bindings.Ignore("top", "");
        return view;
    }

  private:
    bool dice_;
    int round_money_ = -1; // our money and the bank while the last round was played
    int round_bank_ = 0;
    std::string reported_;
};

} // namespace

std::unique_ptr<Screen> CreateDiceScreen()
{
    return std::make_unique<TableGameScreen>(true);
}

std::unique_ptr<Screen> CreateCardsScreen()
{
    return std::make_unique<TableGameScreen>(false);
}

} // namespace storm::ui::screens
