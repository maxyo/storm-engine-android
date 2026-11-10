// Owner of the Caribbean Legend PBR path inside the renderer (public API: storm/render/pbr.hpp). The shim asks
// prepare() for every draw while a material is set, and calls submit() after its fixed-function texture loop.
// Environments are baked on a worker thread and kept on disk (PbrConfig::cache_directory).
#pragma once

#include "storm/pbr/renderer.hpp"
#include "storm/render/pbr.hpp"

#include <bgfx/bgfx.h>
#include <d3d9.h>

#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace storm::shim
{
struct Device;
struct LayoutInfo;
} // namespace storm::shim

namespace storm::pbr_host
{

struct Draw
{
    enum class Kind : uint8_t
    {
        None, // no material set: the legacy program draws
        Pbr,  // `program` draws, submit() binds
        Skip, // the material cannot be drawn this frame (reported once): no draw at all
    } kind = Kind::None;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
};

class Host
{
  public:
    static Host &instance();

    void configure(const render::PbrConfig &config) { config_ = config; }
    void set_material(const render::PbrMaterial *material) { material_ = material; }
    void set_lighting(const render::PbrLighting &lighting);
    bool point_group(const char *group, std::array<float, 3> &rgb) const;

    void on_device_created();
    void shutdown();

    // `fixed_function`: the draw has no shader ports (PBR replaces the fixed-function program only).
    Draw prepare(const shim::Device &device, const shim::LayoutInfo &layout, bool fixed_function);
    void submit(const shim::Device &device, const Draw &draw);
    const pbr::Renderer &renderer() const { return renderer_; }

  private:
    struct Environment
    {
        std::future<std::unique_ptr<pbr::Environment>> baking;
        pbr::GpuEnvironment gpu;
        bool failed = false;
    };
    const pbr::GpuEnvironment *current_environment();
    void start_bake(const std::string &hdr);

    render::PbrConfig config_;
    const render::PbrMaterial *material_ = nullptr;
    render::PbrLighting lighting_{};
    bool has_lighting_ = false;
    pbr::Renderer renderer_;
    bool device_ready_ = false;
    bool device_failed_ = false;
    std::map<std::string, Environment> environments_; // by lower-case hdr path
    std::string environment_key_;
    uint64_t frame_set_ = UINT64_MAX; // frame index whose constants were sent
    D3DMATRIX frame_view_{};          // ... and the camera they were computed for
    std::set<std::string> reported_;  // one message per subject
    void report_once(const std::string &key, const std::string &message);
};

} // namespace storm::pbr_host
