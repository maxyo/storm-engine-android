#include "effects/parallax_probe.hpp"

#include "effects/normal_maps.hpp"
#include "shim/util.hpp"
#include "storm/fx/normal_map.hpp"

#include <cmath>

namespace storm::effects
{

void ParallaxProbe::set_enabled(bool on)
{
    if (on == enabled_)
        return;
    enabled_ = on;
    shim::logf("effects: parallax probe %s (fx_pom_probe)", on ? "on: the relief texture under the screen centre" : "off");
    if (!on)
    {
        release();
        state_ = render::ParallaxProbeState{.serial = state_.serial + 1};
        return;
    }
    state_.on = true;
    ++state_.serial;
}

void ParallaxProbe::release()
{
    for (bgfx::OcclusionQueryHandle &query : queries_)
        if (bgfx::isValid(query))
            bgfx::destroy(query);
    queries_.clear();
    for (Batch &batch : batches_)
        batch = Batch{};
    if (remapped_)
        bgfx::setViewOrder(0, UINT16_MAX, nullptr);
    remapped_ = placed_ = view_set_ = false;
}

void ParallaxProbe::shutdown()
{
    release();
}

void ParallaxProbe::on_device_lost()
{
    queries_.clear(); // the handles went with the device
    for (Batch &batch : batches_)
        batch = Batch{};
    remapped_ = placed_ = view_set_ = false;
}

bgfx::OcclusionQueryHandle ParallaxProbe::candidate(const NormalMapEntry *entry, const float view[16], const float proj[16],
                                                    const float center[3], float radius, const Target &target)
{
    if (!enabled_ || !entry || placed_ || !state_.available)
        return BGFX_INVALID_HANDLE;
    if (queries_.empty())
    {
        // OpenGL ES 3.0 and Vulkan have occlusion queries (bgfx has no capability bit for them); queries that never come
        // back are reported in end_frame().
        queries_.resize(kBatches * kPerFrame);
        for (bgfx::OcclusionQueryHandle &query : queries_)
            query = bgfx::createOcclusionQuery();
        for (const bgfx::OcclusionQueryHandle &query : queries_)
            if (!bgfx::isValid(query))
            {
                shim::logf("ERROR: effects: parallax probe: %d occlusion queries cannot be created; nothing is found",
                           kBatches * kPerFrame);
                release();
                state_.available = false;
                ++state_.serial;
                return BGFX_INVALID_HANDLE;
            }
    }
    Batch &batch = batches_[current_];
    if (!fx::OnCentreRay(view, proj, center, radius))
        return BGFX_INVALID_HANDLE;
    if (batch.draws.size() >= kPerFrame)
    {
        if (!overflow_logged_)
            shim::logf("ERROR: effects: parallax probe: more than %d relief draws on the centre ray in a frame; the later "
                       "ones are not tested (the target can be wrong there)",
                       kPerFrame);
        overflow_logged_ = true;
        return BGFX_INVALID_HANDLE;
    }
    if (!view_set_)
    {
        target_ = target;
        view_set_ = true;
        bgfx::setViewFrameBuffer(kView, target.framebuffer);
        bgfx::setViewMode(kView, bgfx::ViewMode::Sequential);
        bgfx::setViewClear(kView, BGFX_CLEAR_NONE);
        bgfx::setViewRect(kView, static_cast<int16_t>(target.rect[0]), static_cast<int16_t>(target.rect[1]), target.rect[2],
                          target.rect[3], target.min_z, target.max_z);
    }
    else if (!(target == target_))
    {
        if (!mismatch_logged_)
            shim::logf("ERROR: effects: parallax probe: relief draws of one frame on the centre ray in views with different "
                       "viewports; only those of the first are tested");
        mismatch_logged_ = true;
        return BGFX_INVALID_HANDLE;
    }
    batch.draws.push_back({entry->name, fx::ReliefClassName(entry->relief)});
    return queries_[static_cast<size_t>(current_ * kPerFrame) + batch.draws.size() - 1];
}

bool ParallaxProbe::place(bgfx::ViewId last_view)
{
    if (!enabled_ || placed_ || batches_[current_].draws.empty() || last_view <= kView)
        return false;
    const std::vector<uint16_t> order = fx::ProbeViewOrder(kView, last_view);
    bgfx::setViewOrder(0, UINT16_MAX, nullptr); // setViewOrder swaps from the current order: identity first
    bgfx::setViewOrder(kView, static_cast<uint16_t>(order.size()), order.data());
    remapped_ = placed_ = true;
    return true;
}

void ParallaxProbe::before_frame()
{
    if (remapped_ && !placed_)
    {
        bgfx::setViewOrder(0, UINT16_MAX, nullptr);
        remapped_ = false;
    }
}

void ParallaxProbe::end_frame(bool scene_frame, NormalMaps &normal_maps)
{
    if (!enabled_ || queries_.empty())
    {
        placed_ = view_set_ = false;
        return;
    }
    Batch &done = batches_[current_];
    // Probe draws submitted but never placed behind their scene ran before it: their results mean nothing.
    done.valid = scene_frame && (done.draws.empty() || placed_);
    placed_ = view_set_ = false;
    current_ = (current_ + 1) % kBatches;

    // The batch of kBatches - 1 frames ago, read before its queries are used again.
    Batch &batch = batches_[current_];
    if (batch.valid)
    {
        std::vector<int32_t> samples(batch.draws.size());
        for (size_t i = 0; i < batch.draws.size(); ++i)
        {
            int32_t result = 0;
            switch (bgfx::getResult(queries_[static_cast<size_t>(current_ * kPerFrame) + i], &result))
            {
            case bgfx::OcclusionQueryResult::Invisible:
                samples[i] = 0;
                break;
            case bgfx::OcclusionQueryResult::Visible:
                samples[i] = std::max(result, 1);
                break;
            default:
                samples[i] = fx::kProbeNoResult;
                break;
            }
        }
        const fx::ProbePick pick = fx::PickProbeTarget(samples);
        inconclusive_ = pick.conclusive ? 0 : inconclusive_ + 1;
        if (inconclusive_ == kGiveUp)
        {
            shim::logf("ERROR: effects: parallax probe: the occlusion queries of %d frames in a row did not come back; "
                       "nothing is found",
                       kGiveUp);
            state_.available = false;
            ++state_.serial;
        }
        if (pick.conclusive)
        {
            const std::string target = pick.index >= 0 ? batch.draws[static_cast<size_t>(pick.index)].name : std::string();
            if (target != state_.target)
            {
                state_.target = target;
                target_class_ = pick.index >= 0 ? batch.draws[static_cast<size_t>(pick.index)].relief_class : "";
                refresh(normal_maps, true);
            }
        }
    }
    // A used batch's queries are made anew: a fresh query has no result until its own draw's comes back (bgfx keeps a
    // query's last result, which would be read as this use's).
    if (!batch.draws.empty())
        for (size_t i = 0; i < batch.draws.size(); ++i)
        {
            bgfx::OcclusionQueryHandle &query = queries_[static_cast<size_t>(current_ * kPerFrame) + i];
            bgfx::destroy(query);
            query = bgfx::createOcclusionQuery();
            if (!bgfx::isValid(query))
            {
                shim::logf("ERROR: effects: parallax probe: an occlusion query cannot be created again; nothing is found");
                release();
                state_.available = false;
                ++state_.serial;
                return;
            }
        }
    batch = Batch{};
    // The target's relief comes on the GPU, or its verdict changes, while it stays under the centre.
    if (!state_.target.empty() && ++refresh_frames_ >= 10)
        refresh(normal_maps, false);
}

void ParallaxProbe::refresh(NormalMaps &normal_maps, bool log)
{
    refresh_frames_ = 0;
    render::ParallaxProbeState next;
    next.on = state_.on;
    next.available = state_.available;
    next.target = state_.target;
    next.relief_class = target_class_;
    if (!next.target.empty())
    {
        const ParallaxInfo info = normal_maps.parallax_info(next.target);
        if (!info.found)
        {
            // Its texture was released (another location): nothing is under the crosshair until a frame says so.
            next.target.clear();
            next.relief_class.clear();
            target_class_.clear();
        }
        else
        {
            next.uploaded = info.uploaded;
            next.parallax = info.parallax;
            next.coverage = info.coverage;
            next.reason = info.reason;
            next.trust_width = info.trust_width;
            next.trust_height = info.trust_height;
            next.trust = info.trust;
        }
    }
    const bool changed = next.target != state_.target || next.relief_class != state_.relief_class ||
                         next.uploaded != state_.uploaded || next.parallax != state_.parallax ||
                         next.coverage != state_.coverage || next.reason != state_.reason || next.trust != state_.trust;
    if (!changed && !log)
        return;
    next.serial = state_.serial + 1;
    state_ = std::move(next);
    if (state_.target.empty())
        shim::logf("effects: parallax probe: no relief texture under the crosshair");
    else
        shim::logf("effects: parallax probe: %s (class %s, %s, trusted %.0f %% of the texels, mask %dx%d): parallax %s%s%s",
                   state_.target.c_str(), state_.relief_class.c_str(), state_.uploaded ? "on the GPU" : "relief not ready",
                   static_cast<double>(state_.coverage * 100.0f), state_.trust_width, state_.trust_height,
                   state_.parallax ? "on" : "off", state_.parallax ? "" : ": ", state_.reason.c_str());
}

void ParallaxProbe::toggle(NormalMaps &normal_maps)
{
    refresh(normal_maps, false); // the target may have gone with its location
    if (state_.target.empty())
    {
        shim::logf("effects: parallax probe: fx_pom_toggle with no relief texture under the crosshair%s: nothing switched",
                   enabled_ ? "" : " (fx_pom_probe is off)");
        return;
    }
    bool now = false;
    if (!normal_maps.toggle_parallax(state_.target, now))
    {
        shim::logf("ERROR: effects: parallax probe: the parallax of %s cannot be switched (the user list cannot be written)", state_.target.c_str());
        return;
    }
    refresh(normal_maps, true);
}

} // namespace storm::effects
