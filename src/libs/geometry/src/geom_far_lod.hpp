// A GEOM's far LODs at run time (far_lod/README.md): made in the background on first load (or read from the cache),
// then each object drawn at the coarsest level of each of its clusters whose error stays under the preset's pixels.
#pragma once

#include "far_lod.hpp"
#include "geos.h"

#include <storm/fx/bakes.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace storm::render::far_lod
{
struct View;
}

class GeomFarLod
{
  public:
    // The model's data as GEOM read it from the file (vertex bytes and the triangles), for the background job.
    struct Source
    {
        std::string name; // the model, for the log
        std::vector<storm::geometry::far_lod::MeshObject> objects;
        std::vector<std::vector<uint8_t>> buffers;
        std::vector<uint32_t> strides;
        std::vector<uint16_t> triangles;
    };

    // far_lod::Wanted for the file GEOM opens (resource\\models\\...).
    static bool Wanted(const char *file_name, float radius, bool animated);

    GeomFarLod(Source source, GEOM_SERVICE &srv);
    ~GeomFarLod();
    GeomFarLod(const GeomFarLod &) = delete;
    GeomFarLod &operator=(const GeomFarLod &) = delete;

    // Main thread, at draw: true once the LODs are in place (the first call after the job finished installs them).
    bool Ready();
    // Draws object `o` (vertex buffer already set) at its levels for the current far_lod::View and pass; false when
    // the object must be drawn whole from the model's own index buffer (far LODs off, or a pass that already used the
    // object's composed indices with another choice this frame).
    bool Draw(int32_t o, const GEOS::OBJECT &object, int32_t stride);

  private:
    struct Job;
    // Per object, per pass: the clusters' current levels (hysteresis), and for objects of more than one cluster the
    // indices composed for that choice in a buffer of their own.
    struct Slot
    {
        std::vector<uint8_t> levels;
        GEOS::ID buffer = -1;
        uint32_t capacity = 0;            // triangles
        std::vector<uint32_t> order;      // clusters in the composed buffer (nearest first)
        std::vector<uint32_t> offset;     // first triangle of each of them in the buffer
        uint32_t triangles = 0;
        uint64_t frame = UINT64_MAX;      // last frame it was composed in
        bool composed = false;
    };

    void install();
    bool choose(Slot &slot, uint32_t first_cluster, uint32_t count, const storm::render::far_lod::View &view) const;
    bool compose(Slot &slot, uint32_t first_cluster, uint32_t count);

    GEOM_SERVICE &srv_;
    std::string name_;
    std::shared_ptr<Job> job_;
    storm::fx::BakeTicket ticket_;
    std::optional<storm::geometry::far_lod::Lods> lods_;
    GEOS::ID static_buffer_ = -1;
    std::vector<std::array<Slot, 3>> slots_; // per object, per far_lod::Pass
};
