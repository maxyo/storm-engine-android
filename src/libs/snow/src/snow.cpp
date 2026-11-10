#include "snow.hpp"

#include "core.h"
#include "dx9render.h"
#include "message.h"
#include "rands.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
constexpr float kFlakeSize = 0.04f;  // half size of a flake quad, m
constexpr float kSway = 0.3f;        // horizontal drift amplitude, m/s
constexpr uint32_t kMaxFlakes = 65536;

float Wrap(float value, float center, float half)
{
    const float span = 2.0f * half;
    while (value > center + half)
        value -= span;
    while (value < center - half)
        value += span;
    return value;
}
} // namespace

SNOW::~SNOW()
{
    if (renderer_ && texture_ >= 0)
        renderer_->TextureRelease(texture_);
}

bool SNOW::Init()
{
    renderer_ = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
    if (!renderer_)
        throw std::runtime_error("snow: no dx9render service");
    return true;
}

uint32_t SNOW::AttributeChanged(ATTRIBUTES *)
{
    dirty_ = true;
    return 0;
}

uint64_t SNOW::ProcessMessage(MESSAGE &message)
{
    spdlog::error("snow: message {} is not handled", message.Long());
    return 0;
}

void SNOW::ReadSettings()
{
    Settings next;
    if (AttributesPointer)
    {
        const auto *a = AttributesPointer;
        if (const char *texture = a->GetAttribute("flakeTexture"))
            next.texture = texture;
        next.flakes = a->GetAttributeAsDword("flakesMax", next.flakes);
        next.fall_min = a->GetAttributeAsFloat("fallSpeedMin", next.fall_min);
        next.fall_max = a->GetAttributeAsFloat("fallSpeedMax", next.fall_max);
        next.radius = a->GetAttributeAsFloat("visibilityRadius", next.radius);
        next.height = a->GetAttributeAsFloat("visibilityHeight", next.height);
    }
    const Settings defaults;
    if (next.flakes == 0 || next.flakes > kMaxFlakes)
    {
        spdlog::error("snow: flakesMax {} is outside 1..{}; using {}", next.flakes, kMaxFlakes, defaults.flakes);
        next.flakes = defaults.flakes;
    }
    if (!(next.fall_min > 0.0f) || next.fall_max < next.fall_min)
    {
        spdlog::error("snow: fall speeds {}..{} are not 0 < min <= max; using {}..{}", next.fall_min, next.fall_max,
                      defaults.fall_min, defaults.fall_max);
        next.fall_min = defaults.fall_min;
        next.fall_max = defaults.fall_max;
    }
    if (!(next.radius > 0.0f) || !(next.height > 0.0f))
    {
        spdlog::error("snow: visibility {} x {} m is not positive; using {} x {}", next.radius, next.height,
                      defaults.radius, defaults.height);
        next.radius = defaults.radius;
        next.height = defaults.height;
    }
    if (next.texture != settings_.texture || texture_ < 0)
    {
        if (texture_ >= 0)
            renderer_->TextureRelease(texture_);
        texture_ = renderer_->TextureCreate(next.texture.c_str());
        if (texture_ < 0)
            spdlog::error("snow: flake texture '{}' cannot be loaded; flakes are not drawn", next.texture);
    }
    settings_ = next;
}

void SNOW::Respawn(const glm::vec3 &eye)
{
    flakes_.resize(settings_.flakes);
    for (auto &flake : flakes_)
    {
        flake.pos = eye + glm::vec3(randCentered(2.0f * settings_.radius), randCentered(settings_.height),
                                    randCentered(2.0f * settings_.radius));
        flake.speed = settings_.fall_min + rand(settings_.fall_max - settings_.fall_min);
        flake.phase = rand(6.2831853f);
    }
}

void SNOW::Advance(std::vector<Flake> &flakes, const Settings &settings, const glm::vec3 &eye, float seconds,
                   float time)
{
    const float half_height = 0.5f * settings.height;
    for (auto &flake : flakes)
    {
        flake.pos.y -= flake.speed * seconds;
        flake.pos.x += std::sin(time + flake.phase) * kSway * seconds;
        flake.pos.z += std::cos(0.7f * time + flake.phase) * kSway * seconds;
        flake.pos.x = Wrap(flake.pos.x, eye.x, settings.radius);
        flake.pos.z = Wrap(flake.pos.z, eye.z, settings.radius);
        flake.pos.y = Wrap(flake.pos.y, eye.y, half_height);
    }
}

void SNOW::ProcessStage(Stage stage, uint32_t delta)
{
    if (stage == Stage::realize)
        Realize(delta);
}

void SNOW::Realize(uint32_t delta_ms)
{
    glm::vec3 eye, angles;
    float perspective;
    renderer_->GetCamera(eye, angles, perspective);
    if (dirty_)
    {
        ReadSettings();
        Respawn(eye);
        dirty_ = false;
    }
    const float seconds = static_cast<float>(delta_ms) * 0.001f;
    time_ += seconds;
    Advance(flakes_, settings_, eye, seconds, time_);
    if (texture_ < 0)
        return;

    rects_.resize(flakes_.size());
    const float inv_radius = 1.0f / settings_.radius;
    for (size_t i = 0; i < flakes_.size(); ++i)
    {
        const glm::vec3 d = flakes_[i].pos - eye;
        // Fade out towards the edge of the visibility cylinder so the wrap-around is not seen.
        const float fade = std::clamp(1.0f - std::sqrt(d.x * d.x + d.z * d.z) * inv_radius, 0.0f, 1.0f);
        auto &r = rects_[i];
        r.vPos = flakes_[i].pos;
        r.fSize = kFlakeSize;
        r.fAngle = flakes_[i].phase;
        r.dwColor = (static_cast<uint32_t>(fade * 255.0f) << 24) | 0x00ffffff;
        r.dwSubTexture = 0;
    }
    renderer_->TextureSet(0, texture_);
    renderer_->DrawRects(rects_.data(), static_cast<uint32_t>(rects_.size()), "snow_flakes");
}
