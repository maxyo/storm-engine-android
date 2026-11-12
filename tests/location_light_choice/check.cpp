// The lights of a model in a location (Lights::SetLightsAt -> storm::location::ChooseLights): chosen where the model
// is, whatever the camera does, and without a jump while it walks. The room below is a tavern like the game's:
// candlesticks of three candles on the tables and along the walls, a fireplace.
#include "light_choice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
struct P
{
    float x, y, z;
};
struct Lamp
{
    P pos;
    float luminance, att0, att2, range;
};

int failures = 0;
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                           \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

std::vector<Lamp> Tavern()
{
    std::vector<Lamp> lamps;
    const auto candlestick = [&](float x, float y, float z) {
        for (const float d : {-0.12f, 0.0f, 0.12f})
            lamps.push_back({{x + d, y, z - d}, 1.6f, 0.5f, 1.5f, 15.0f});
    };
    for (float x = -6.0f; x <= 6.0f; x += 3.0f) // tables
        candlestick(x, 1.0f, 1.5f);
    for (float x = -7.0f; x <= 7.0f; x += 3.5f) // sconces of both long walls
    {
        candlestick(x, 2.4f, -4.9f);
        candlestick(x, 2.4f, 4.9f);
    }
    lamps.push_back({{-7.8f, 0.6f, 0.0f}, 1.44f, 1.5f, 0.2f, 40.0f}); // fireplace
    return lamps;
}

float Distance(P a, P b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

std::vector<uint32_t> Choose(const std::vector<Lamp> &lamps, P at, size_t budget = 7)
{
    return storm::location::ChooseLights(
        lamps.size(),
        [&](size_t i) {
            return storm::location::LightStrength(lamps[i].luminance, lamps[i].att0, 0.0f, lamps[i].att2, lamps[i].range,
                                                  Distance(lamps[i].pos, at));
        },
        budget);
}

// The lamp light a model at `at` receives: what Lights::SetLightsAt hands to the fixed-function pipeline.
float LightAt(const std::vector<Lamp> &lamps, P at)
{
    float sum = 0.0f;
    for (const uint32_t i : Choose(lamps, at))
        sum += storm::location::LightStrength(lamps[i].luminance, lamps[i].att0, 0.0f, lamps[i].att2, lamps[i].range,
                                              Distance(lamps[i].pos, at));
    return sum;
}
} // namespace

int main()
{
    const std::vector<Lamp> lamps = Tavern();
    const P hero{-2.0f, 1.2f, 2.3f}; // a character next to a table, or a lit model of the location at its own place

    // The choice takes the model's position and the lights, nothing else: wherever the camera orbits or backs off to,
    // the model gets the same lights. (The camera is not an input; the renderer-level proof on real frames is the
    // scene study of the commit: a hero standing still keeps his light while the camera backs off.)
    const std::vector<uint32_t> chosen = Choose(lamps, hero);
    const float reference = LightAt(lamps, hero);
    CHECK(reference > 0.3f && chosen.size() == 7);
    for (int step = 0; step < 16; ++step)
        CHECK(Choose(lamps, hero) == chosen);

    // Strongest first; every light left out is no stronger than the weakest chosen one.
    const auto strength = [&](uint32_t i) {
        return storm::location::LightStrength(lamps[i].luminance, lamps[i].att0, 0.0f, lamps[i].att2, lamps[i].range,
                                              Distance(lamps[i].pos, hero));
    };
    for (size_t i = 1; i < chosen.size(); ++i)
        CHECK(strength(chosen[i - 1]) >= strength(chosen[i]));
    for (uint32_t i = 0; i < lamps.size(); ++i)
        if (std::find(chosen.begin(), chosen.end(), i) == chosen.end())
            CHECK(strength(i) <= strength(chosen.back()));

    // No jump: a model walking through the whole room (1 cm steps) never sees its lamp light change by more than
    // a few per cent of the brightest spot between two steps, although the set of seven changes many times on the way.
    float brightest = 0.0f, largest_step = 0.0f;
    int changes = 0;
    std::vector<uint32_t> last_set;
    float last = -1.0f;
    for (float x = -7.5f; x <= 7.5f; x += 0.01f)
    {
        const P at{x, 1.2f, 0.4f * std::sin(x) + 0.2f};
        const float light = LightAt(lamps, at);
        std::vector<uint32_t> set = Choose(lamps, at);
        std::sort(set.begin(), set.end());
        if (last >= 0.0f)
        {
            largest_step = std::max(largest_step, std::fabs(light - last));
            changes += set != last_set ? 1 : 0;
        }
        brightest = std::max(brightest, light);
        last = light;
        last_set = set;
    }
    CHECK(changes >= 8);
    CHECK(largest_step < 0.03f * brightest);

    // A model in the middle of a ring of equal lamps is lit by seven of them (no light is lost to a tie), in list order.
    std::vector<Lamp> ring;
    for (int i = 0; i < 9; ++i)
        ring.push_back({{std::cos(0.7f * static_cast<float>(i)) * 3.0f, 0.0f, std::sin(0.7f * static_cast<float>(i)) * 3.0f}, 1.0f, 1.0f, 0.0f, 10.0f});
    CHECK((Choose(ring, P{0.0f, 0.0f, 0.0f}) == std::vector<uint32_t>{0, 1, 2, 3, 4, 5, 6}));

    // A strong fireplace farther away beats a nearer weak candle; a lamp out of range or without light is never chosen.
    const std::vector<Lamp> mixed = {{{1.0f, 0.0f, 0.0f}, 0.05f, 0.5f, 1.5f, 15.0f},
                                     {{3.0f, 0.0f, 0.0f}, 1.44f, 1.5f, 0.2f, 40.0f},
                                     {{2.0f, 0.0f, 0.0f}, 5.0f, 0.5f, 1.5f, 1.0f},
                                     {{0.5f, 0.0f, 0.0f}, 0.0f, 0.5f, 1.5f, 15.0f}};
    CHECK((Choose(mixed, P{0.0f, 0.0f, 0.0f}, 1) == std::vector<uint32_t>{1}));
    CHECK((Choose(mixed, P{0.0f, 0.0f, 0.0f}) == std::vector<uint32_t>{1, 0}));

    // Fewer lights than the budget: all of them. None: nothing.
    CHECK(Choose(std::vector<Lamp>(lamps.begin(), lamps.begin() + 4), hero).size() == 4);
    CHECK(Choose({}, hero).empty());

    if (failures)
        return 1;
    std::printf("PASS: a model's lights follow its own position (largest step %.2f %% of the brightest spot over %d set changes)\n",
                100.0f * largest_step / brightest, changes);
    return 0;
}
