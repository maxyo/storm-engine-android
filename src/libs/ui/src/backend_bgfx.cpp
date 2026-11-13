// bgfx backend: RmlUi 6 render interface (geometry, textures, scissor, stencil clip masks,
// transforms, layers, filters, gradients) on bgfx views handed out by the frame owner.
//
// The RmlUi calls of a frame are recorded and executed at EndFrame. Execution follows the
// stock GL3 renderer pass by pass (MSAA layer stack sharing one stencil buffer,
// post-process ping-pong buffers, the same blur and shadow passes and shaders), so both
// backends produce the same pixels.
//
// bgfx renders into one frame buffer per view, and the frame owner hands out a small
// range of views. Every draw is therefore placed by a scheduler: it goes into the first
// view of its render target that comes after everything it depends on, judged by the
// screen regions it writes and reads. Filtered elements in different places share
// views, so the number of views follows the depth of the effect chain rather than the
// number of elements.
//
// Spans that only render a texture (box-shadow images) are cut out of the frame, run in a
// separate small set of frame buffers, and are resumed in later frames when the views
// left over by the frame do not suffice; the element draws without the shadow until then.
//
// Coordinates follow the GL conventions of bgfx's GLES backend (bottom-left texture
// origin, BGFX_CONFIG_GL_NORMALIZE_NDC_CONVENTIONS=0). On a backend whose render targets
// have the origin at the top (Vulkan) every pass into a layer or post buffer is drawn
// upside down (Executor::UpsideDown: clip space, view rectangle and scissor mirrored top
// to bottom), so the buffers hold the same texels at the same texture coordinates as on
// GL and nothing else differs. The final target is drawn as it is on both.
#include "storm_ui/backend_bgfx.h"

#include "storm_ui/ui.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <bgfx/bgfx.h>

#include "storm/render/shader_blob.hpp"

// Every shader of shaders/, for OpenGL ES and for Vulkan (renderer_bgfx cmake/storm_shaders.cmake).
#include "storm_ui_shaders/includes.inc"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace storm::ui
{

namespace
{

constexpr int kMaxStops = 16;
constexpr uint64_t kWriteColor = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;
constexpr uint64_t kBlendPremultiplied = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
constexpr uint32_t kSampleClamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr uint32_t kStencilKeep = BGFX_STENCIL_FUNC_RMASK(0xff) | BGFX_STENCIL_OP_FAIL_S_KEEP | BGFX_STENCIL_OP_FAIL_Z_KEEP;

// Handles from before a device loss must be forgotten, not destroyed.
uint32_t g_epoch = 1;

template <typename Handle> void DestroyIfCurrent(Handle &handle, uint32_t epoch)
{
    if (bgfx::isValid(handle) && epoch == g_epoch)
        bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
}

const bgfx::VertexLayout &Layout()
{
    static const bgfx::VertexLayout layout = [] {
        bgfx::VertexLayout result;
        result.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        return result;
    }();
    return layout;
}

// ---- resources --------------------------------------------------------------------
// GPU objects are created at execution time (the device may be lost while RmlUi records)
// and kept alive by recorded commands after RmlUi released them.

struct Geometry
{
    std::vector<Rml::Vertex> vertices; // until uploaded
    std::vector<int> indices;
    uint32_t index_count = 0;
    Rml::Vector2f min, max; // bounds of the positions
    // Dynamic buffers although written once: bgfx places them in its shared vertex and index buffers (one layout,
    // so their offsets stay whole vertices), where a static buffer each made a GL buffer per element and opening a
    // screen created a few hundred in its first frame.
    bgfx::DynamicVertexBufferHandle vb = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle ib = BGFX_INVALID_HANDLE;
    uint32_t epoch = 0;

    ~Geometry()
    {
        DestroyIfCurrent(vb, epoch);
        DestroyIfCurrent(ib, epoch);
    }
    bool Ensure()
    {
        if (bgfx::isValid(vb) && epoch == g_epoch)
            return true;
        if (vertices.empty() || indices.empty())
            return false;
        vb = bgfx::createDynamicVertexBuffer(bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(Rml::Vertex))),
                                             Layout());
        ib = bgfx::createDynamicIndexBuffer(bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(int))),
                                            BGFX_BUFFER_INDEX32);
        epoch = g_epoch;
        std::vector<Rml::Vertex>().swap(vertices);
        std::vector<int>().swap(indices);
        return bgfx::isValid(vb) && bgfx::isValid(ib);
    }
};

struct Texture
{
    enum class Kind
    {
        Pixels,  // RGBA8 premultiplied from RmlUi
        Target,  // SaveLayerAsTexture output, filled by a blit
        Picture, // game picture owned by the frame owner (straight alpha)
    };
    Kind kind = Kind::Pixels;
    int width = 0;
    int height = 0;
    std::vector<Rml::byte> pixels; // Pixels: until uploaded
    std::string picture;           // Picture: path for FrameOwner
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint32_t epoch = 0;
    bool filled = false;   // Target: the blit was submitted
    bool released = false; // RmlUi gave it back

    ~Texture()
    {
        DestroyIfCurrent(handle, epoch);
    }
    bool Current() const
    {
        return bgfx::isValid(handle) && epoch == g_epoch;
    }
    bool Ensure()
    {
        if (Current())
            return true;
        if (kind == Kind::Pixels)
        {
            if (pixels.empty())
                return false;
            handle = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE);
            bgfx::updateTexture2D(handle, 0, 0, 0, 0, uint16_t(width), uint16_t(height), bgfx::copy(pixels.data(), uint32_t(pixels.size())));
            std::vector<Rml::byte>().swap(pixels);
        }
        else if (kind == Kind::Target)
        {
            handle = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST);
            filled = false;
        }
        else
            return false;
        epoch = g_epoch;
        return bgfx::isValid(handle);
    }
};

enum class FilterType
{
    Passthrough,
    Blur,
    DropShadow,
    ColorMatrix,
    MaskImage
};

struct Filter
{
    FilterType type = FilterType::Passthrough;
    float blend_factor = 1.0f;
    float sigma = 0.0f;
    Rml::Vector2f offset;
    Rml::ColourbPremultiplied color;
    Rml::Matrix4f color_matrix = Rml::Matrix4f::Identity();
};

enum class GradientFunction
{
    Linear,
    Radial,
    Conic,
    RepeatingLinear,
    RepeatingRadial,
    RepeatingConic
}; // fs_ui_gradient.sc

struct Shader
{
    GradientFunction function = GradientFunction::Linear;
    Rml::Vector2f p;
    Rml::Vector2f v;
    std::vector<float> stop_positions;
    std::vector<Rml::Colourf> stop_colors;
};

// RmlUi handles are owning pointers to these.
template <typename T> using Ref = std::shared_ptr<T>;
template <typename T> uintptr_t ToHandle(Ref<T> ref)
{
    return reinterpret_cast<uintptr_t>(new Ref<T>(std::move(ref)));
}
template <typename T> const Ref<T> &FromHandle(uintptr_t handle)
{
    return *reinterpret_cast<Ref<T> *>(handle);
}
template <typename T> void ReleaseHandle(uintptr_t handle)
{
    delete reinterpret_cast<Ref<T> *>(handle);
}

// ---- recorded frame ---------------------------------------------------------------

struct DrawState
{
    Rml::Rectanglei scissor = Rml::Rectanglei::MakeInvalid(); // RmlUi (top-left) pixels
    bool stencil_test = false;
    int stencil_ref = 1;
    bool transformed = false; // an element transform applies
    Rml::Matrix4f transform;  // projection * element transform
};

enum class Op : uint8_t
{
    Draw,
    ClipMask,
    PushLayer,
    Composite,
    PopLayer,
    SaveTexture,
    SaveMask,
};

struct Command
{
    Op op = Op::Draw;
    DrawState state;
    Ref<Geometry> geometry;
    Rml::Vector2f translation;
    Ref<Texture> texture; // Draw: sampled; SaveTexture: output
    Ref<const Shader> shader;
    Rml::ClipMaskOperation clip = Rml::ClipMaskOperation::Set;
    int source = 0;
    int destination = 0;
    Rml::BlendMode blend = Rml::BlendMode::Blend;
    std::vector<Ref<const Filter>> filters;
    // PushLayer: the part of the layer read before it is popped (composite, save). Only
    // that part is cleared, so layers of different elements can share views.
    Rml::Rectanglei layer_reads = Rml::Rectanglei::MakeInvalid();
    bool layer_read_whole = false;
};

using Commands = std::vector<Command>;

// A span of commands that only renders into a texture: drawn in the job frame buffers
// with `depth` layers below it, possibly over several frames.
struct Job
{
    Commands commands;
    int depth = 1;
    Rml::Vector2i extent; // pixels the job covers from the origin
    bool placed = false;  // moved to its own spot in the job buffers
    size_t cursor = 0;
    int layers = 0;
    std::array<int, 3> post = {0, 1, 2};
};

// ---- GPU objects owned by the backend ------------------------------------------------

enum class ProgramId
{
    Color,
    Texture,
    TextureStraight,
    Gradient,
    QuadColor,
    Passthrough,
    ColorMatrix,
    BlendMask,
    Blur,
    DropShadow,
    Count
};

struct Uniforms
{
    bgfx::UniformHandle transform, translate, clip, gradient, gradient_pv, stop_colors, stop_positions, factor, color_matrix,
        blur_weights, texel_offset, region, shadow_color, tex, mask;
};

struct FrameBuffer
{
    bgfx::FrameBufferHandle fb = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
};

// Layers (multisampled, one shared depth/stencil) and post buffers (0..2 ping-pong,
// 3 the mask image) of one size. Set 0 serves the frame, set 1 the texture jobs.
struct TargetSet
{
    int width = 0;
    int height = 0;
    std::vector<FrameBuffer> layers;
    bgfx::TextureHandle depth_stencil = BGFX_INVALID_HANDLE;
    std::array<FrameBuffer, 4> post{};

    void Destroy()
    {
        for (FrameBuffer &target : layers)
        {
            DestroyIfCurrent(target.fb, g_epoch);
            DestroyIfCurrent(target.color, g_epoch);
        }
        layers.clear();
        for (FrameBuffer &target : post)
            DestroyIfCurrent(target.fb, g_epoch); // owns its texture
        post.fill({});
        DestroyIfCurrent(depth_stencil, g_epoch);
    }
    void Forget()
    {
        layers.clear();
        post.fill({});
        depth_stencil = BGFX_INVALID_HANDLE;
    }
};

enum class TargetKind : uint8_t
{
    Final,   // the UI target: surface back buffer or the host's frame buffer
    Layer,   // colour of a layer
    Post,    // post buffer
    Stencil, // the stencil buffer shared by a set's layers (dependencies only)
    Texture, // a SaveLayerAsTexture output (dependencies only)
};
struct Target
{
    TargetKind kind = TargetKind::Final;
    uint8_t set = 0;
    int index = 0;
    const Texture *texture = nullptr;
    bool operator==(const Target &other) const
    {
        return kind == other.kind && set == other.set && index == other.index && texture == other.texture;
    }
};

// Pixel rectangle in target coordinates, top-left origin, exclusive end.
struct Region
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool Empty() const
    {
        return x1 <= x0 || y1 <= y0;
    }
    bool Overlaps(const Region &o) const
    {
        return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
    }
    Region Intersect(const Region &o) const
    {
        return {std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
    }
    Region Union(const Region &o) const
    {
        return {std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1)};
    }
};

struct ViewRect
{
    int x = 0, y = 0, w = 0, h = 0; // bgfx (top-left) view rectangle
    bool operator==(const ViewRect &o) const
    {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

struct Access
{
    Target target;
    Region region;
};

// A few accesses of one pass, without allocations.
struct Accesses
{
    std::array<Access, 3> items{};
    int count = 0;
    Accesses() = default;
    Accesses(std::initializer_list<Access> list)
    {
        for (const Access &access : list)
            Add(access);
    }
    void Add(const Access &access)
    {
        items[size_t(count++)] = access;
    }
    const Access *begin() const
    {
        return items.data();
    }
    const Access *end() const
    {
        return items.data() + count;
    }
};

// Places passes into views. A pass goes into the first view of its render target (and
// viewport) that comes after every earlier pass it conflicts with: writes after reads
// and writes of overlapping regions, reads after overlapping writes. Accesses marked
// "early" happen at the start of their view (bgfx blits) rather than in draw order.
class Schedule
{
  public:
    struct Slot
    {
        Target target;
        ViewRect rect;
        uint16_t clear = BGFX_CLEAR_NONE;
    };

    int Place(Target target, ViewRect rect, const Accesses &writes, const Accesses &reads, uint16_t clear = BGFX_CLEAR_NONE)
    {
        int earliest = 0;
        for (const Bucket &bucket : buckets_)
        {
            // The stencil is an attachment: its accesses follow draw order inside a view.
            if (bucket.target.kind == TargetKind::Stencil)
            {
                for (const Access &write : writes)
                    if (bucket.target == write.target && Hit(bucket, write.region))
                        earliest = std::max(earliest, bucket.slot);
                for (const Access &read : reads)
                    if (bucket.write && bucket.target == read.target && Hit(bucket, read.region))
                        earliest = std::max(earliest, bucket.slot);
                continue;
            }
            for (const Access &write : writes)
                if (bucket.target == write.target && Hit(bucket, write.region))
                    earliest = std::max(earliest, bucket.write || bucket.early ? bucket.slot : bucket.slot + 1);
            if (bucket.write)
                for (const Access &read : reads)
                    if (bucket.target == read.target && Hit(bucket, read.region))
                        earliest = std::max(earliest, bucket.early ? bucket.slot : bucket.slot + 1);
        }
        int slot = -1;
        if (clear == BGFX_CLEAR_NONE)
            for (int s = earliest; s < int(slots_.size()); ++s)
                if (slots_[size_t(s)].target == target && slots_[size_t(s)].rect == rect)
                {
                    slot = s;
                    break;
                }
        if (slot < 0)
        {
            slot = int(slots_.size());
            slots_.push_back({target, rect, clear});
        }
        for (const Access &write : writes)
            Add(write, slot, true, false);
        for (const Access &read : reads)
            Add(read, slot, false, false);
        return slot;
    }
    void AddEarly(const Access &access, int slot, bool write)
    {
        Add(access, slot, write, true);
    }
    int Size() const
    {
        return int(slots_.size());
    }
    const std::vector<Slot> &Slots() const
    {
        return slots_;
    }

  private:
    static constexpr size_t kMaxRegions = 64; // beyond that a bucket keeps only its bounds

    struct Bucket
    {
        Target target;
        int slot = 0;
        bool write = false;
        bool early = false;
        bool coarse = false;
        Region bounds;
        std::vector<Region> regions;
    };

    static bool Hit(const Bucket &bucket, const Region &region)
    {
        if (!bucket.bounds.Overlaps(region))
            return false;
        if (bucket.coarse)
            return true;
        for (const Region &r : bucket.regions)
            if (r.Overlaps(region))
                return true;
        return false;
    }

    void Add(const Access &access, int slot, bool write, bool early)
    {
        if (access.region.Empty())
            return;
        for (Bucket &bucket : buckets_)
            if (bucket.slot == slot && bucket.write == write && bucket.early == early && bucket.target == access.target)
            {
                bucket.bounds = bucket.bounds.Union(access.region);
                if (!bucket.coarse)
                {
                    bucket.regions.push_back(access.region);
                    bucket.coarse = bucket.regions.size() > kMaxRegions;
                }
                return;
            }
        Bucket bucket;
        bucket.target = access.target;
        bucket.slot = slot;
        bucket.write = write;
        bucket.early = early;
        bucket.bounds = access.region;
        // The base layer and its stencil take the bulk of the draws and are rarely shared.
        bucket.coarse = (access.target.kind == TargetKind::Layer && access.target.index == 0) || access.target.kind == TargetKind::Stencil ||
                        access.target.kind == TargetKind::Final;
        if (!bucket.coarse)
            bucket.regions.push_back(access.region);
        buckets_.push_back(std::move(bucket));
    }

    std::vector<Slot> slots_;
    std::vector<Bucket> buckets_;
};

void SigmaToParameters(float desired_sigma, int &pass_level, float &sigma)
{
    constexpr int max_num_passes = 10;
    constexpr float max_single_pass_sigma = 3.0f;
    pass_level = Rml::Math::Clamp(Rml::Math::Log2(int(desired_sigma * (2.f / max_single_pass_sigma))), 0, max_num_passes);
    sigma = Rml::Math::Clamp(desired_sigma / float(1 << pass_level), 0.0f, max_single_pass_sigma);
}

Rml::Colourf ToColourf(Rml::ColourbPremultiplied c)
{
    Rml::Colourf result;
    for (int i = 0; i < 4; i++)
        result[i] = float(c[i]) / 255.f;
    return result;
}

// RmlUi rectangle <-> GL (bottom-left) rectangle of a target of height `h`.
Rml::Rectanglei Flipped(Rml::Rectanglei rect, int h)
{
    Rml::Rectanglei result = rect;
    result.p0.y = h - rect.p1.y;
    result.p1.y = h - rect.p0.y;
    return result;
}

class BgfxBackendImpl;

// Turns recorded commands into bgfx submissions in two runs over the same commands: the
// planning run places every pass (Schedule), the live run submits it to the planned view.
class Executor
{
  public:
    explicit Executor(BgfxBackendImpl &backend) : b_(&backend)
    {
    }
    // Live run replaying `plan`.
    Executor(BgfxBackendImpl &backend, const Executor &plan, uint16_t first_view);

    int Views() const
    {
        return schedule_.Size();
    }
    void Main(const Commands &commands, bool clear_final, bool degraded);
    // Main frame within `views`: composites whose filters do not fit lose them.
    void SetBudget(int views)
    {
        budget_ = views;
    }
    // Takes over the filter decisions of a run over the same commands.
    void TakeDecisions(const Executor &other)
    {
        unfiltered_ = other.unfiltered_;
    }
    void BeginJob(const Job &job);
    void Step(const Command &command)
    {
        Run(command, false);
    }
    void EndJob(Job &job) const
    {
        job.layers = layers_;
        job.post = post_;
    }
    // Planning: draws left out because the texture they sample is still being made by a queued job.
    int Starved() const
    {
        return starved_;
    }

  private:
    void Run(const Command &command, bool degraded);
    void Composite(const Command &command, bool unfiltered);
    static uint32_t StencilTest(const DrawState &state);
    int Begin(Target target, ViewRect rect, const Accesses &writes, const Accesses &reads, uint16_t clear = BGFX_CLEAR_NONE);
    bgfx::ViewId View(int slot) const
    {
        return bgfx::ViewId(first_view_ + slot);
    }

    TargetSet &Set() const;
    int Width() const
    {
        return Set().width;
    }
    int Height() const
    {
        return Set().height;
    }
    ViewRect Full() const
    {
        return {0, 0, Width(), Height()};
    }
    Region Whole() const
    {
        return {0, 0, Width(), Height()};
    }
    Region Clip(const Rml::Rectanglei &scissor) const
    {
        if (!scissor.Valid())
            return Whole();
        return Region{scissor.Left(), scissor.Top(), scissor.Right(), scissor.Bottom()}.Intersect(Whole());
    }
    Region FromGl(Rml::Rectanglei rect_gl) const
    {
        return Clip(Flipped(rect_gl, Height()));
    }
    Region DrawRegion(const Command &command) const;
    static Rml::Rectanglei RectOf(const Region &region)
    {
        return Rml::Rectanglei::FromCorners({region.x0, region.y0}, {region.x1, region.y1});
    }

    Target Post(int role) const
    {
        return {TargetKind::Post, set_, post_[size_t(role)]};
    }
    Target PostSlot(int slot) const
    {
        return {TargetKind::Post, set_, slot};
    }
    Target Layer(int index) const
    {
        return {TargetKind::Layer, set_, index};
    }
    Target Stencil() const
    {
        return {TargetKind::Stencil, set_};
    }
    void SwapPost()
    {
        std::swap(post_[0], post_[1]);
    }
    bgfx::FrameBufferHandle Handle(Target target) const;
    bgfx::TextureHandle Color(Target target) const;

    bool SetScissor(int slot, const Rml::Rectanglei &rect) const;
    void Geometry(int slot, const Command &command, ProgramId program, uint64_t state, uint32_t stencil);
    void Quad(int slot, ProgramId program, uint64_t state, Rml::Vector2f ndc0, Rml::Vector2f ndc1, Rml::Vector2f uv0, Rml::Vector2f uv1,
              const Rml::Rectanglei &scissor, uint32_t stencil = BGFX_STENCIL_NONE);
    void FullQuad(int slot, ProgramId program, uint64_t state, const Rml::Rectanglei &scissor, Rml::Vector2f uv_offset = {},
                  Rml::Vector2f uv_scaling = Rml::Vector2f(1.f), uint32_t stencil = BGFX_STENCIL_NONE)
    {
        Quad(slot, program, state, Rml::Vector2f(-1.f), Rml::Vector2f(1.f), uv_offset, uv_offset + uv_scaling, scissor, stencil);
    }
    // The pass of `slot` draws its target upside down (see the head of this file).
    bool UpsideDown(int slot) const;
    // A bgfx rectangle (top-left origin) of the slot's target, mirrored top to bottom where it is drawn upside down.
    ViewRect Placed(int slot, ViewRect rect) const;
    void SetClip(int slot) const;
    void BindTexture(uint8_t stage, Target source) const;
    void SetFactor(float factor) const;
    void SetRegion(Rml::Rectanglei rect_gl) const;

    void CopyLayerToPost(int layer, const Rml::Rectanglei &scissor);
    // `source`: where the first filter reads (a layer, sparing the copy to post 0).
    void Filters(const std::vector<Ref<const Filter>> &filters, const Rml::Rectanglei &scissor, std::optional<Target> source = {});
    // `first_source`: where the first pass reads (a layer, sparing the copy to post 0).
    void Blur(float sigma, int source_destination, int temp, Rml::Rectanglei window_gl, std::optional<Target> first_source = {});
    void ClipMask(const Command &command);
    void SaveTexture(const Command &command);

    BgfxBackendImpl *b_;
    bool live_ = false;
    uint16_t first_view_ = 0;
    Schedule schedule_;
    std::vector<int> plan_; // planned slot of every Begin(), in order
    size_t next_ = 0;
    uint8_t set_ = 0;
    std::optional<Rml::Matrix4f> projection_; // job runs: projection of the job buffers
    int layers_ = 0;
    std::array<int, 3> post_ = {0, 1, 2};
    std::unordered_set<const Texture *> filled_; // filled by this frame's blits
    int budget_ = 0;                                // planning: views the frame may use (0: no limit)
    int starved_ = 0;                               // Starved()
    std::unordered_set<const Command *> unfiltered_; // composites drawn without their filters
};
class BgfxBackendImpl final : public BgfxBackend, public Rml::RenderInterface
{
  public:
    explicit BgfxBackendImpl(const BgfxBackendOptions &options) : options_(options)
    {
    }
    ~BgfxBackendImpl() override
    {
        ReleaseShownPictures();
        DestroyGpu();
    }

    bool Start(std::string *error)
    {
        if (!CreateGpu())
        {
            if (error)
                *error = "bgfx UI backend: shader programs failed";
            return false;
        }
        return true;
    }

    // ---- RenderBackend ------------------------------------------------------------
    Rml::RenderInterface &Interface() override
    {
        return *this;
    }
    void SetViewport(int width, int height) override
    {
        replayable_ = false;
        width = std::max(width, 1);
        height = std::max(height, 1);
        projection_ = Rml::Matrix4f::ProjectOrtho(0, float(width), float(height), 0, -10000, 10000);
        if (width == sets_[0].width && height == sets_[0].height)
            return;
        sets_[0].Destroy();
        sets_[0].width = width;
        sets_[0].height = height;
    }
    void BeginFrame(bool clear) override
    {
        clear_ = clear;
        commands_.clear();
        state_ = {};
        state_.transform = projection_;
        state_.transformed = false;
        depth_ = 1;
        open_layers_.clear();
    }
    void EndFrame() override;
    bool FrameHeld() const override
    {
        return held_frames_ > 0;
    }
    PictureState AcquireGamePicture(const std::string &texture) override;

    // ---- BgfxBackend ----------------------------------------------------------------
    void AttachHost(Host *host) override
    {
        host_ = host;
    }
    void SetDensity(float density) override
    {
        density_override_ = density;
    }
    void SetTarget(uint16_t frame_buffer) override
    {
        target_.idx = frame_buffer;
    }
    void SetViews(uint16_t first, uint16_t count) override
    {
        first_view_ = first;
        view_count_ = count;
    }
    bool UpdateTexture(Rml::TextureHandle handle, Rml::Rectanglei region, Rml::Span<const Rml::byte> rgba) override;
    size_t PendingWork() const override
    {
        return jobs_.size();
    }
    double NextFrameDelay() const override
    {
        return jobs_.empty() ? next_frame_delay_ : 0.0;
    }

    // ---- FrameClient ------------------------------------------------------------------
    void OnSurfaceChanged(const storm::render::SurfaceMetrics &metrics) override
    {
        if (!metrics.valid || !metrics.width_px || !metrics.height_px)
            return;
        const float density = density_override_ > 0.f ? density_override_ : metrics.density;
        if (host_)
            host_->Resize(int(metrics.width_px), int(metrics.height_px), density);
        else
            SetViewport(int(metrics.width_px), int(metrics.height_px));
    }
    void OnUiRender(const storm::render::FrameContext &frame) override
    {
        SetViews(frame.ui_view_begin, frame.ui_view_count);
        if (!host_)
            return;
        // The engine frame is drawn anew every time, so the UI must be in it every time. A
        // frame the host calls unchanged (Host::Prepare) submits the commands recorded for the
        // last drawn one again, without RmlUi's update and render.
        double delay = 0.0;
        if (host_->Prepare(delay) || !replayable_)
            next_frame_delay_ = host_->Frame();
        else
        {
            Submit(shown_main_, shown_clear_, false);
            next_frame_delay_ = delay;
        }
    }
    void OnDeviceLost() override
    {
        ++g_epoch; // every handle created so far is gone
        device_ready_ = false;
        programs_.fill(BGFX_INVALID_HANDLE);
        uniforms_ = {};
        for (TargetSet &set : sets_)
            set.Forget();
        jobs_.clear();
        commands_.clear();
        shown_main_.clear();
        held_frames_ = 0;
        replayable_ = false;
        ReleaseShownPictures();
        // RmlUi re-creates textures and geometry on demand through this interface.
        Rml::ReleaseTextures(this);
        Rml::ReleaseCompiledGeometry(this);
    }
    void OnDeviceRestored() override
    {
        device_ready_ = CreateGpu();
    }
    bool WantsContinuousFrames() override
    {
        return next_frame_delay_ <= 0.0 || !jobs_.empty();
    }

    // ---- Rml::RenderInterface ---------------------------------------------------------
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
    {
        auto geometry = std::make_shared<storm::ui::Geometry>();
        geometry->vertices.assign(vertices.begin(), vertices.end());
        geometry->indices.assign(indices.begin(), indices.end());
        geometry->index_count = uint32_t(indices.size());
        geometry->min = Rml::Vector2f(std::numeric_limits<float>::max());
        geometry->max = Rml::Vector2f(std::numeric_limits<float>::lowest());
        for (const Rml::Vertex &vertex : vertices)
        {
            geometry->min = Rml::Math::Min(geometry->min, vertex.position);
            geometry->max = Rml::Math::Max(geometry->max, vertex.position);
        }
        return ToHandle(std::move(geometry));
    }
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override
    {
        Command &command = Record(Op::Draw);
        command.geometry = FromHandle<storm::ui::Geometry>(geometry);
        command.translation = translation;
        if (texture)
            command.texture = FromHandle<Texture>(texture);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
    {
        ReleaseHandle<storm::ui::Geometry>(geometry);
        replayable_ = false; // the recorded frame may use it
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override
    {
        const std::string scheme = GamePictureSource("");
        if (source.compare(0, scheme.size(), scheme) == 0)
            return LoadPicture(dimensions, source.substr(scheme.size()));
        std::vector<unsigned char> rgba;
        if (!DecodePackImage(source, dimensions.x, dimensions.y, rgba))
            return 0;
        return GenerateTexture({rgba.data(), rgba.size()}, dimensions);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override
    {
        if (dimensions.x <= 0 || dimensions.y <= 0 || source.size() < size_t(dimensions.x) * size_t(dimensions.y) * 4)
            return 0;
        auto texture = std::make_shared<Texture>();
        texture->width = dimensions.x;
        texture->height = dimensions.y;
        texture->pixels.assign(source.begin(), source.end());
        return ToHandle(std::move(texture));
    }
    void ReleaseTexture(Rml::TextureHandle handle) override
    {
        const Ref<Texture> &texture = FromHandle<Texture>(handle);
        texture->released = true;
        replayable_ = false; // the recorded frame may use it
        // A game picture stays with the frame on the screen, which may be shown again while the next
        // one waits for its textures (EndFrame): given back when another frame takes its place.
        if (texture->kind == Texture::Kind::Picture)
            shown_pictures_.push_back(texture->picture);
        ReleaseHandle<Texture>(handle);
    }

    void EnableScissorRegion(bool enable) override
    {
        if (!enable)
            state_.scissor = Rml::Rectanglei::MakeInvalid();
    }
    void SetScissorRegion(Rml::Rectanglei region) override
    {
        state_.scissor = region;
    }
    void EnableClipMask(bool enable) override
    {
        state_.stencil_test = enable;
    }
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override
    {
        Command &command = Record(Op::ClipMask);
        command.clip = operation;
        command.geometry = FromHandle<storm::ui::Geometry>(geometry);
        command.translation = translation;
        // Same reference bookkeeping as the GL renderer.
        state_.stencil_ref = operation == Rml::ClipMaskOperation::Intersect ? state_.stencil_ref + 1 : 1;
    }
    void SetTransform(const Rml::Matrix4f *transform) override
    {
        state_.transform = transform ? projection_ * *transform : projection_;
        state_.transformed = transform && *transform != Rml::Matrix4f::Identity();
    }

    Rml::LayerHandle PushLayer() override
    {
        open_layers_.push_back(commands_.size());
        Record(Op::PushLayer);
        return Rml::LayerHandle(depth_++);
    }
    // Notes that `layer` is read inside the current scissor.
    void NoteLayerRead(int layer)
    {
        if (layer < 1 || layer > int(open_layers_.size()))
            return;
        Command &push = commands_[open_layers_[size_t(layer - 1)]];
        if (!state_.scissor.Valid())
            push.layer_read_whole = true;
        else
            push.layer_reads = push.layer_reads.Valid() ? push.layer_reads.Join(state_.scissor) : state_.scissor;
    }
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override
    {
        NoteLayerRead(int(source));
        Command &command = Record(Op::Composite);
        command.source = int(source);
        command.destination = int(destination);
        command.blend = blend_mode;
        for (const Rml::CompiledFilterHandle filter : filters)
            command.filters.push_back(FromHandle<const Filter>(filter));
    }
    void PopLayer() override
    {
        Record(Op::PopLayer);
        --depth_;
        if (!open_layers_.empty())
            open_layers_.pop_back();
    }
    Rml::TextureHandle SaveLayerAsTexture() override
    {
        const Rml::Rectanglei bounds = state_.scissor;
        if (!bounds.Valid() || bounds.Width() <= 0 || bounds.Height() <= 0)
            return 0;
        auto texture = std::make_shared<Texture>();
        texture->kind = Texture::Kind::Target;
        texture->width = bounds.Width();
        texture->height = bounds.Height();
        NoteLayerRead(depth_ - 1);
        Record(Op::SaveTexture).texture = texture;
        return ToHandle(std::move(texture));
    }
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override
    {
        NoteLayerRead(depth_ - 1);
        Record(Op::SaveMask);
        auto filter = std::make_shared<Filter>();
        filter->type = FilterType::MaskImage;
        return ToHandle<const Filter>(std::move(filter));
    }

    Rml::CompiledFilterHandle CompileFilter(const Rml::String &name, const Rml::Dictionary &parameters) override;
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override
    {
        ReleaseHandle<const Filter>(filter);
        replayable_ = false; // the recorded frame may use it
    }
    Rml::CompiledShaderHandle CompileShader(const Rml::String &name, const Rml::Dictionary &parameters) override;
    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                      Rml::TextureHandle) override
    {
        Command &command = Record(Op::Draw);
        command.shader = FromHandle<const Shader>(shader);
        command.geometry = FromHandle<storm::ui::Geometry>(geometry);
        command.translation = translation;
    }
    void ReleaseShader(Rml::CompiledShaderHandle shader) override
    {
        ReleaseHandle<const Shader>(shader);
        replayable_ = false; // the recorded frame may use it
    }

  private:
    friend class Executor;

    Command &Record(Op op)
    {
        Command &command = commands_.emplace_back();
        command.op = op;
        command.state = state_;
        return command;
    }

    bool CreateGpu();
    void DestroyGpu();
    const FrameBuffer &LayerTarget(TargetSet &set, int index);
    const FrameBuffer &PostTarget(TargetSet &set, int slot);

    Rml::TextureHandle LoadPicture(Rml::Vector2i &dimensions, const std::string &path);
    void ReleasePicture(const std::string &path);
    void ReleaseShownPictures()
    {
        for (const std::string &path : shown_pictures_)
            ReleasePicture(path);
        shown_pictures_.clear();
    }
    // True while a queued job still has to fill `texture`.
    bool Awaited(const Texture &texture) const
    {
        for (const Job &job : jobs_)
            for (const Command &command : job.commands)
                if (command.op == Op::SaveTexture && command.texture.get() == &texture && !texture.released)
                    return true;
        return false;
    }
    bgfx::TextureHandle PictureHandle(const Texture &texture) const;

    void ExtractJobs(Commands &main);
    // Plans the queued jobs into the views the frame leaves over; returns the command
    // range of each job that runs this frame.
    std::vector<std::pair<size_t, size_t>> PlanJobs(Executor &planner, int budget);
    bool PlaceJob(Job &job);
    // Effects dropped because the frame owner's view range is too small: an error, not a
    // mode. Logged on the first occurrence and every 600th after.
    void ReportOverflow(const char *what)
    {
        if (overflows_++ % 600 == 0)
            Rml::Log::Message(Rml::Log::LT_ERROR, "UI needs more than the %d bgfx views of the frame owner: %s (%d times)", int(view_count_),
                              what, overflows_);
    }
    struct Shelf
    {
        int x = 0, y = 0, height = 0;
    } shelf_;

    BgfxBackendOptions options_;
    Host *host_ = nullptr;
    float density_override_ = 0.f;
    double next_frame_delay_ = 0.0;
    bool device_ready_ = true;
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    uint16_t first_view_ = storm::render::kUiViewBegin;
    uint16_t view_count_ = storm::render::kUiViewCount;

    Rml::Matrix4f projection_ = Rml::Matrix4f::Identity();

    // Recording.
    bool clear_ = false;
    Commands commands_;
    // The commands of the frame on the screen, without its texture jobs: submitted again for a
    // frame the host calls unchanged (`replayable_`), and instead of a frame that would be drawn
    // without textures its jobs are still making (EndFrame).
    Commands shown_main_;
    bool shown_clear_ = false;
    bool replayable_ = false;
    int held_frames_ = 0; // frames in a row that were not shown (FrameHeld)
    std::vector<std::string> shown_pictures_; // game pictures RmlUi gave back that the frame on the screen may draw
    // Submits the queued texture jobs that fit and `main`. With `complete_only`, a `main` that
    // would be drawn without a texture a job is still making is not submitted at all: false.
    bool Submit(const Commands &main, bool clear, bool complete_only);
    DrawState state_;
    int depth_ = 1;
    std::vector<size_t> open_layers_; // indices of the PushLayer commands of open layers
    std::deque<Job> jobs_;
    int overflows_ = 0;

    // GPU objects.
    std::array<bgfx::ProgramHandle, size_t(ProgramId::Count)> programs_{};
    Uniforms uniforms_{};
    int msaa_ = 0;
    std::array<TargetSet, 2> sets_{}; // 0: frame (surface size), 1: texture jobs

    struct PictureRef
    {
        bool acquired = false;
        int users = 0;
        int width = 0;
        int height = 0;
    };
    std::unordered_map<std::string, PictureRef> pictures_;
};
bool BgfxBackendImpl::CreateGpu()
{
    // Programs keep their shaders referenced; our own references go once all are linked.
    std::vector<bgfx::ShaderHandle> shaders;
    const auto shader = [&shaders](const storm::render::ShaderBlob &blob, const char *name) {
        return shaders.emplace_back(storm::render::CreateShader(blob, name));
    };
#define STORM_UI_SHADER(name) shader(STORM_SHADER_BLOB(name), #name)
    const bgfx::ShaderHandle vs = STORM_UI_SHADER(vs_ui), vs_quad = STORM_UI_SHADER(vs_ui_quad);
    const auto link = [&](ProgramId id, bgfx::ShaderHandle vertex, bgfx::ShaderHandle fragment) {
        programs_[size_t(id)] = bgfx::createProgram(vertex, fragment, false);
    };
    link(ProgramId::Color, vs, STORM_UI_SHADER(fs_ui_color));
    link(ProgramId::Texture, vs, STORM_UI_SHADER(fs_ui_texture));
    link(ProgramId::TextureStraight, vs, STORM_UI_SHADER(fs_ui_texture_straight));
    link(ProgramId::Gradient, vs, STORM_UI_SHADER(fs_ui_gradient));
    link(ProgramId::QuadColor, vs_quad, STORM_UI_SHADER(fs_ui_color));
    link(ProgramId::Passthrough, vs_quad, STORM_UI_SHADER(fs_ui_passthrough));
    link(ProgramId::ColorMatrix, vs_quad, STORM_UI_SHADER(fs_ui_color_matrix));
    link(ProgramId::BlendMask, vs_quad, STORM_UI_SHADER(fs_ui_blend_mask));
    link(ProgramId::Blur, vs_quad, STORM_UI_SHADER(fs_ui_blur));
    link(ProgramId::DropShadow, vs_quad, STORM_UI_SHADER(fs_ui_drop_shadow));
#undef STORM_UI_SHADER
    for (bgfx::ShaderHandle handle : shaders)
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);

    using T = bgfx::UniformType;
    uniforms_.transform = bgfx::createUniform("u_ui_transform", T::Mat4);
    uniforms_.translate = bgfx::createUniform("u_ui_translate", T::Vec4);
    uniforms_.clip = bgfx::createUniform("u_ui_clip", T::Vec4);
    uniforms_.gradient = bgfx::createUniform("u_ui_gradient", T::Vec4);
    uniforms_.gradient_pv = bgfx::createUniform("u_ui_gradient_pv", T::Vec4);
    uniforms_.stop_colors = bgfx::createUniform("u_ui_stop_colors", T::Vec4, kMaxStops);
    uniforms_.stop_positions = bgfx::createUniform("u_ui_stop_positions", T::Vec4, kMaxStops);
    uniforms_.factor = bgfx::createUniform("u_ui_factor", T::Vec4);
    uniforms_.color_matrix = bgfx::createUniform("u_ui_color_matrix", T::Mat4);
    uniforms_.blur_weights = bgfx::createUniform("u_ui_blur_weights", T::Vec4);
    uniforms_.texel_offset = bgfx::createUniform("u_ui_texel_offset", T::Vec4);
    uniforms_.region = bgfx::createUniform("u_ui_region", T::Vec4);
    uniforms_.shadow_color = bgfx::createUniform("u_ui_shadow_color", T::Vec4);
    uniforms_.tex = bgfx::createUniform("s_ui_tex", T::Sampler);
    uniforms_.mask = bgfx::createUniform("s_ui_mask", T::Sampler);

    const bgfx::Caps *caps = bgfx::getCaps();
    // The layers are RGBA8 colour + D24S8 depth/stencil: both must render with the sample count (per-format caps).
    const auto layer_msaa = [caps](int samples) {
        const uint32_t flag = samples >= 4 ? BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X4 : BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X2;
        return (caps->formats[bgfx::TextureFormat::RGBA8] & flag) && (caps->formats[bgfx::TextureFormat::D24S8] & flag);
    };
    msaa_ = 0;
    for (int samples = options_.msaa_samples; samples >= 2 && msaa_ == 0; samples /= 2)
        if (layer_msaa(samples))
            msaa_ = samples;
    if (msaa_ != options_.msaa_samples)
        Rml::Log::Message(Rml::Log::LT_WARNING, "UI layers: MSAA %dx is not available for RGBA8 + D24S8 targets: %s", options_.msaa_samples,
                          msaa_ ? "2x instead" : "no multisampling");
    for (const bgfx::ProgramHandle handle : programs_)
        if (!bgfx::isValid(handle))
            return false;
    return true;
}


void BgfxBackendImpl::DestroyGpu()
{
    if (!device_ready_)
        return;
    for (TargetSet &set : sets_)
        set.Destroy();
    for (bgfx::ProgramHandle &program : programs_)
        DestroyIfCurrent(program, g_epoch);
    for (bgfx::UniformHandle *uniform : {&uniforms_.transform, &uniforms_.translate, &uniforms_.clip, &uniforms_.gradient, &uniforms_.gradient_pv,
                                          &uniforms_.stop_colors, &uniforms_.stop_positions, &uniforms_.factor, &uniforms_.color_matrix,
                                          &uniforms_.blur_weights, &uniforms_.texel_offset, &uniforms_.region, &uniforms_.shadow_color,
                                          &uniforms_.tex, &uniforms_.mask})
        DestroyIfCurrent(*uniform, g_epoch);
}

// Layers: RGBA8 colour (multisampled like the GL renderer) sharing one depth/stencil buffer.
const FrameBuffer &BgfxBackendImpl::LayerTarget(TargetSet &set, int index)
{
    if (int(set.layers.size()) <= index)
        set.layers.resize(size_t(index) + 1);
    FrameBuffer &target = set.layers[size_t(index)];
    if (bgfx::isValid(target.fb))
        return target;
    const uint64_t msaa = msaa_ >= 4 ? BGFX_TEXTURE_RT_MSAA_X4 : msaa_ >= 2 ? BGFX_TEXTURE_RT_MSAA_X2 : BGFX_TEXTURE_RT;
    const uint16_t w = uint16_t(set.width), h = uint16_t(set.height);
    if (!bgfx::isValid(set.depth_stencil))
        set.depth_stencil = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D24S8, msaa | BGFX_TEXTURE_RT_WRITE_ONLY);
    target.color = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, msaa | kSampleClamp);
    bgfx::Attachment attachments[2];
    attachments[0].init(target.color, bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);
    attachments[1].init(set.depth_stencil, bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);
    target.fb = bgfx::createFrameBuffer(2, attachments, false);
    return target;
}

const FrameBuffer &BgfxBackendImpl::PostTarget(TargetSet &set, int slot)
{
    FrameBuffer &target = set.post[size_t(slot)];
    if (!bgfx::isValid(target.fb))
    {
        target.color = bgfx::createTexture2D(uint16_t(set.width), uint16_t(set.height), false, 1, bgfx::TextureFormat::RGBA8,
                                             BGFX_TEXTURE_RT | kSampleClamp);
        target.fb = bgfx::createFrameBuffer(1, &target.color, true);
    }
    return target;
}

// ---- game pictures ----------------------------------------------------------------

// One frame owner reference per picture path, taken by the first Acquire and given back
// when RmlUi releases the last texture made from it.
RenderBackend::PictureState BgfxBackendImpl::AcquireGamePicture(const std::string &texture)
{
    if (!options_.owner)
        return PictureState::Unavailable; // logged once by the element
    PictureRef &ref = pictures_[texture];
    const storm::render::GamePicture picture =
        ref.acquired ? options_.owner->QueryGamePicture(texture) : options_.owner->AcquireGamePicture(texture);
    if (picture.state == storm::render::GamePicture::State::Failed)
    {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Game picture '%s' could not be loaded by the renderer", texture.c_str());
        if (ref.acquired)
            options_.owner->ReleaseGamePicture(texture);
        pictures_.erase(texture);
        return PictureState::Failed;
    }
    ref.acquired = true;
    if (picture.state == storm::render::GamePicture::State::Pending)
        return PictureState::Pending;
    ref.width = picture.width;
    ref.height = picture.height;
    return PictureState::Ready;
}

Rml::TextureHandle BgfxBackendImpl::LoadPicture(Rml::Vector2i &dimensions, const std::string &path)
{
    const auto found = pictures_.find(path);
    if (found == pictures_.end() || found->second.width <= 0 || found->second.height <= 0)
        return 0;
    ++found->second.users;
    dimensions = {found->second.width, found->second.height};
    auto texture = std::make_shared<Texture>();
    texture->kind = Texture::Kind::Picture;
    texture->width = dimensions.x;
    texture->height = dimensions.y;
    texture->picture = path;
    return ToHandle(std::move(texture));
}

void BgfxBackendImpl::ReleasePicture(const std::string &path)
{
    const auto found = pictures_.find(path);
    if (found == pictures_.end() || --found->second.users > 0)
        return;
    if (options_.owner)
        options_.owner->ReleaseGamePicture(path);
    pictures_.erase(found);
}

bgfx::TextureHandle BgfxBackendImpl::PictureHandle(const Texture &texture) const
{
    if (!options_.owner)
        return BGFX_INVALID_HANDLE;
    const storm::render::GamePicture picture = options_.owner->QueryGamePicture(texture.picture);
    if (picture.state != storm::render::GamePicture::State::Ready)
        return BGFX_INVALID_HANDLE;
    return {options_.owner->BgfxTexture(picture.texture)};
}

bool BgfxBackendImpl::UpdateTexture(Rml::TextureHandle handle, Rml::Rectanglei region, Rml::Span<const Rml::byte> rgba)
{
    if (!handle)
        return false;
    Texture &texture = *FromHandle<Texture>(handle);
    if (texture.kind != Texture::Kind::Pixels || region.Left() < 0 || region.Top() < 0 || region.Right() > texture.width ||
        region.Bottom() > texture.height || rgba.size() < size_t(region.Width()) * size_t(region.Height()) * 4)
        return false;
    if (texture.Current())
    {
        bgfx::updateTexture2D(texture.handle, 0, 0, uint16_t(region.Left()), uint16_t(region.Top()), uint16_t(region.Width()),
                              uint16_t(region.Height()), bgfx::copy(rgba.data(), uint32_t(region.Width() * region.Height() * 4)));
        return true;
    }
    if (texture.pixels.empty())
        return false; // uploaded before a device loss; RmlUi regenerates it
    for (int row = 0; row < region.Height(); ++row)
        std::memcpy(&texture.pixels[(size_t(region.Top() + row) * size_t(texture.width) + size_t(region.Left())) * 4],
                    &rgba[size_t(row) * size_t(region.Width()) * 4], size_t(region.Width()) * 4);
    return true;
}

// ---- filters and shaders (parameters as in RmlUi's GL3 renderer) ---------------------

Rml::CompiledFilterHandle BgfxBackendImpl::CompileFilter(const Rml::String &name, const Rml::Dictionary &parameters)
{
    Filter filter;
    if (name == "opacity")
    {
        filter.type = FilterType::Passthrough;
        filter.blend_factor = Rml::Get(parameters, "value", 1.0f);
    }
    else if (name == "blur")
    {
        filter.type = FilterType::Blur;
        filter.sigma = Rml::Get(parameters, "sigma", 1.0f);
    }
    else if (name == "drop-shadow")
    {
        filter.type = FilterType::DropShadow;
        filter.sigma = Rml::Get(parameters, "sigma", 0.f);
        filter.color = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
        filter.offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
    }
    else if (name == "brightness")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
    }
    else if (name == "contrast")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        const float grayness = 0.5f - 0.5f * value;
        filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
        filter.color_matrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
    }
    else if (name == "invert")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
        const float inverted = 1.f - 2.f * value;
        filter.color_matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
        filter.color_matrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
    }
    else if (name == "grayscale")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        const float rev = 1.f - value;
        const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
        filter.color_matrix = Rml::Matrix4f::FromRows({gray.x + rev, gray.y, gray.z, 0.f}, {gray.x, gray.y + rev, gray.z, 0.f},
                                                      {gray.x, gray.y, gray.z + rev, 0.f}, {0.f, 0.f, 0.f, 1.f});
    }
    else if (name == "sepia")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        const float rev = 1.f - value;
        const Rml::Vector3f r = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
        const Rml::Vector3f g = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
        const Rml::Vector3f b = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
        filter.color_matrix = Rml::Matrix4f::FromRows({r.x + rev, r.y, r.z, 0.f}, {g.x, g.y + rev, g.z, 0.f}, {b.x, b.y, b.z + rev, 0.f},
                                                      {0.f, 0.f, 0.f, 1.f});
    }
    else if (name == "hue-rotate")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        const float s = Rml::Math::Sin(value), c = Rml::Math::Cos(value);
        filter.color_matrix = Rml::Matrix4f::FromRows(
            {0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0.f},
            {0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0.f},
            {0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0.f}, {0.f, 0.f, 0.f, 1.f});
    }
    else if (name == "saturate")
    {
        filter.type = FilterType::ColorMatrix;
        const float v = Rml::Get(parameters, "value", 1.0f);
        filter.color_matrix = Rml::Matrix4f::FromRows({0.213f + 0.787f * v, 0.715f - 0.715f * v, 0.072f - 0.072f * v, 0.f},
                                                      {0.213f - 0.213f * v, 0.715f + 0.285f * v, 0.072f - 0.072f * v, 0.f},
                                                      {0.213f - 0.213f * v, 0.715f - 0.715f * v, 0.072f + 0.928f * v, 0.f}, {0.f, 0.f, 0.f, 1.f});
    }
    else
    {
        Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", name.c_str());
        return 0;
    }
    return ToHandle<const Filter>(std::make_shared<Filter>(filter));
}

Rml::CompiledShaderHandle BgfxBackendImpl::CompileShader(const Rml::String &name, const Rml::Dictionary &parameters)
{
    Shader shader;
    const bool repeating = Rml::Get(parameters, "repeating", false);
    if (name == "linear-gradient")
    {
        shader.function = repeating ? GradientFunction::RepeatingLinear : GradientFunction::Linear;
        shader.p = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
        shader.v = Rml::Get(parameters, "p1", Rml::Vector2f(0.f)) - shader.p;
    }
    else if (name == "radial-gradient")
    {
        shader.function = repeating ? GradientFunction::RepeatingRadial : GradientFunction::Radial;
        shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
        shader.v = Rml::Vector2f(1.f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
    }
    else if (name == "conic-gradient")
    {
        shader.function = repeating ? GradientFunction::RepeatingConic : GradientFunction::Conic;
        shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
        const float angle = Rml::Get(parameters, "angle", 0.f);
        shader.v = {Rml::Math::Cos(angle), Rml::Math::Sin(angle)};
    }
    else
    {
        Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", name.c_str());
        return 0;
    }
    const auto found = parameters.find("color_stop_list");
    if (found == parameters.end() || found->second.GetType() != Rml::Variant::COLORSTOPLIST)
        return 0;
    const Rml::ColorStopList &stops = found->second.GetReference<Rml::ColorStopList>();
    const int count = Rml::Math::Min(int(stops.size()), kMaxStops);
    for (int i = 0; i < count; i++)
    {
        shader.stop_positions.push_back(stops[size_t(i)].position.number);
        shader.stop_colors.push_back(ToColourf(stops[size_t(i)].color));
    }
    if (shader.stop_positions.empty())
        return 0;
    return ToHandle<const Shader>(std::make_shared<Shader>(std::move(shader)));
}


// ---- frame scheduling ---------------------------------------------------------------

// Cuts out every outermost PushLayer..PopLayer span that ends in SaveLayerAsTexture, never
// composites into or from a layer below it, and draws untransformed inside scissor rectangles:
// such a span only fills its output texture and can run anywhere and any time.
void BgfxBackendImpl::ExtractJobs(Commands &main)
{
    struct Span
    {
        size_t start;
        int layer; // layer index pushed by the span
        bool saves = false;
        bool valid = true;
        Rml::Vector2i extent;
    };
    std::vector<Span> open;
    struct Range
    {
        size_t start, end; // inclusive
        int depth;
        Rml::Vector2i extent;
    };
    std::vector<Range> ranges;
    int depth = 1;
    for (size_t i = 0; i < main.size(); ++i)
    {
        const Command &command = main[i];
        for (Span &span : open)
        {
            if (command.state.transformed || !command.state.scissor.Valid())
                span.valid = false;
            else
                span.extent = Rml::Math::Max(span.extent, command.state.scissor.p1);
        }
        switch (command.op)
        {
        case Op::PushLayer:
            open.push_back({i, depth});
            if (command.state.transformed || !command.state.scissor.Valid())
                open.back().valid = false;
            else
                open.back().extent = command.state.scissor.p1;
            ++depth;
            break;
        case Op::SaveTexture:
            if (!open.empty() && open.back().layer == depth - 1)
                open.back().saves = true;
            break;
        case Op::SaveMask:
            for (Span &span : open)
                span.valid = false;
            break;
        case Op::Composite:
            // A span must neither write nor read a layer below its own: it runs in other
            // buffers, where those layers do not exist (the frame renderer's filter cache
            // composites the element's layer into a pushed layer and saves that one).
            for (Span &span : open)
                if (command.destination < span.layer || command.source < span.layer)
                    span.valid = false;
            break;
        case Op::PopLayer:
            --depth;
            if (!open.empty())
            {
                const Span span = open.back();
                open.pop_back();
                if (span.valid && span.saves)
                {
                    while (!ranges.empty() && ranges.back().start > span.start)
                        ranges.pop_back(); // nested inside this one
                    ranges.push_back({span.start, i, span.layer, span.extent});
                }
            }
            break;
        default:
            break;
        }
    }
    if (ranges.empty())
        return;
    Commands rest;
    rest.reserve(main.size());
    size_t at = 0;
    for (const Range &range : ranges)
    {
        for (; at < range.start; ++at)
            rest.push_back(std::move(main[at]));
        Job job;
        job.depth = range.depth;
        job.extent = range.extent;
        for (; at <= range.end; ++at)
            job.commands.push_back(std::move(main[at]));
        jobs_.push_back(std::move(job));
    }
    for (; at < main.size(); ++at)
        rest.push_back(std::move(main[at]));
    main = std::move(rest);
}

std::vector<std::pair<size_t, size_t>> BgfxBackendImpl::PlanJobs(Executor &planner, int budget)
{
    // Jobs whose textures RmlUi already gave back are pointless.
    for (auto it = jobs_.begin(); it != jobs_.end();)
    {
        bool needed = false;
        for (const Command &command : it->commands)
            if (command.op == Op::SaveTexture && !command.texture->released)
                needed = true;
        it = needed ? std::next(it) : jobs_.erase(it);
    }
    // The job buffers have the surface size. Each job is moved to its own spot in them
    // (shelf packing), so the passes of different jobs share views.
    TargetSet &set = sets_[1];
    if (set.width != sets_[0].width || set.height != sets_[0].height)
    {
        set.Destroy();
        set.width = sets_[0].width;
        set.height = sets_[0].height;
        shelf_ = {};
        bool placed = false;
        for (const Job &job : jobs_)
            placed = placed || job.placed;
        if (placed)
        {
            // Placed for the old size: let RmlUi render those textures again.
            jobs_.clear();
            Rml::ReleaseTextures(this);
            return {};
        }
    }
    bool any_placed = false;
    for (const Job &job : jobs_)
        any_placed = any_placed || job.placed;
    if (!any_placed)
        shelf_ = {};
    std::vector<std::pair<size_t, size_t>> ranges;
    for (size_t j = 0; j < jobs_.size(); ++j)
    {
        Job &job = jobs_[j];
        if (!job.placed && !PlaceJob(job))
        {
            // No room beside the placed jobs. When every one of them finishes in this frame, the
            // buffers are free again for the views after theirs: the next jobs start over in
            // them within the same frame (a screen's worth of panel shadows is made in one
            // frame, not in one frame per buffer-full).
            bool finishing = true;
            for (size_t done = 0; done < j; ++done)
                finishing = finishing && done < ranges.size() && ranges[done].second >= jobs_[done].commands.size();
            if (!finishing)
                break; // no room until the placed jobs are done
            shelf_ = {};
            if (!PlaceJob(job))
                break;
        }
        const size_t start = job.cursor;
        size_t end = start;
        bool full = false;
        planner.BeginJob(job);
        while (end < job.commands.size())
        {
            Executor before = planner;
            planner.Step(job.commands[end]);
            if (planner.Views() <= budget)
            {
                ++end;
                continue;
            }
            planner = std::move(before);
            full = true;
            if (end == start)
            {
                // Would this step fit into a frame of its own at all?
                Executor alone(*this);
                alone.BeginJob(job);
                alone.Step(job.commands[end]);
                if (alone.Views() > int(view_count_) - 2)
                {
                    ReportOverflow("a box-shadow/layer texture left empty");
                    jobs_.erase(jobs_.begin() + std::ptrdiff_t(j));
                    return ranges;
                }
            }
            break;
        }
        ranges.emplace_back(start, end);
        if (full)
            break;
    }
    return ranges;
}

// Moves a job (scissors, translations) to the next free spot of the job buffers. The gap
// keeps the half-resolution blur passes of neighbours apart.
bool BgfxBackendImpl::PlaceJob(Job &job)
{
    constexpr int gap = 32;
    const TargetSet &set = sets_[1];
    const int w = job.extent.x + gap, h = job.extent.y + gap;
    if (shelf_.x + w > set.width)
        shelf_ = {0, shelf_.y + shelf_.height, 0};
    if (shelf_.x + w > set.width && shelf_.x > 0)
        return false;
    if (shelf_.y + job.extent.y > set.height)
        return false;
    const Rml::Vector2i offset(shelf_.x, shelf_.y);
    shelf_.x += w;
    shelf_.height = std::max(shelf_.height, h);
    for (Command &command : job.commands)
    {
        command.state.scissor = Rml::Rectanglei::FromCorners(command.state.scissor.p0 + offset, command.state.scissor.p1 + offset);
        command.translation += Rml::Vector2f(offset);
        if (command.layer_reads.Valid())
            command.layer_reads = Rml::Rectanglei::FromCorners(command.layer_reads.p0 + offset, command.layer_reads.p1 + offset);
    }
    job.placed = true;
    return true;
}

void BgfxBackendImpl::EndFrame()
{
    Commands main = std::move(commands_);
    commands_.clear();
    replayable_ = false;
    if (!device_ready_ || sets_[0].width <= 0)
    {
        held_frames_ = 0;
        return;
    }
    ExtractJobs(main);
    // A frame is shown whole or not at all. Layer textures (box shadows: the panels of a screen)
    // are made by jobs that may not fit into this frame's views beside the frame itself; drawn
    // now, the frame would show the screen without them and the next one with them (a screen
    // that appears twice). Such a frame is held back: the jobs run, the frame on the screen is
    // submitted again, and the host draws the new one again until its textures are there.
    // Bounded: a job that never finishes is an error, and the frame is then shown as it is.
    constexpr int kMaxHeldFrames = 30;
    const bool bounded = held_frames_ >= kMaxHeldFrames;
    if (bounded)
        Rml::Log::Message(Rml::Log::LT_ERROR, "UI frame shown without %d layer textures: their jobs did not finish in %d frames", int(jobs_.size()),
                          kMaxHeldFrames);
    if (!Submit(main, clear_, !bounded))
    {
        ++held_frames_;
        Submit(shown_main_, shown_clear_, false);
        return;
    }
    held_frames_ = 0;
    // A frame that saves layers as textures (the frame renderer filling its filter cache) is
    // not repeated: the next one draws from the cache.
    replayable_ = std::none_of(main.begin(), main.end(), [](const Command &command) { return command.op == Op::SaveTexture; });
    shown_main_ = std::move(main);
    shown_clear_ = clear_;
    ReleaseShownPictures();
}

bool BgfxBackendImpl::Submit(const Commands &main, bool clear, bool complete_only)
{
    const bool clear_ = clear; // the frame's own clear, also for a frame submitted again
    Executor counter(*this);
    counter.SetBudget(view_count_);
    counter.Main(main, clear_, false);
    bool degraded = false;
    int main_views = counter.Views();
    if (main_views > view_count_)
    {
        degraded = true;
        Executor fallback(*this);
        fallback.Main(main, clear_, true);
        main_views = fallback.Views();
        ReportOverflow("layers of this frame drawn without effects");
    }

    // Texture jobs first, in the views the frame leaves over, then the frame itself.
    Executor planner(*this);
    planner.TakeDecisions(counter);
    const std::vector<std::pair<size_t, size_t>> ranges = PlanJobs(planner, int(view_count_) - main_views);
    planner.Main(main, clear_, degraded);
    if (complete_only && planner.Starved() > 0)
        return false;

    Executor live(*this, planner, first_view_);
    for (size_t j = 0; j < ranges.size(); ++j)
    {
        Job &job = jobs_[j];
        live.BeginJob(job);
        for (size_t i = ranges[j].first; i < ranges[j].second; ++i)
            live.Step(job.commands[i]);
        live.EndJob(job);
        job.cursor = ranges[j].second;
    }
    live.Main(main, clear_, degraded);
    while (!jobs_.empty() && jobs_.front().cursor >= jobs_.front().commands.size())
        jobs_.pop_front();
    return true;
}

// ---- executor -----------------------------------------------------------------------

Executor::Executor(BgfxBackendImpl &backend, const Executor &plan, uint16_t first_view)
    : b_(&backend), live_(true), first_view_(first_view), schedule_(plan.schedule_), plan_(plan.plan_), unfiltered_(plan.unfiltered_)
{
    const std::vector<Schedule::Slot> &slots = schedule_.Slots();
    // Tile GPUs: what the frame no longer needs is invalidated after the last view that draws into it
    // (BGFX_CLEAR_DISCARD_*, patches/bgfx-gl-framebuffer-discard.patch), so it is never written back to memory.
    // Only the main UI's layers (set 0): it redraws them from a clear every frame. The jobs' layers (set 1) keep
    // their content across frames. A layer's multisampled colour is not needed once resolved (OpenGL ES); without MSAA the
    // attachment is the texture the composite samples and stays. The set's depth/stencil goes after its last view.
    std::vector<uint16_t> discard(slots.size(), BGFX_CLEAR_NONE);
    int last_layer_slot = -1;
    for (size_t s = 0; s < slots.size(); ++s)
    {
        const Target &target = slots[s].target;
        if (target.kind != TargetKind::Layer || target.set != 0)
            continue;
        last_layer_slot = int(s);
        bool written_later = false;
        for (size_t later = s + 1; later < slots.size() && !written_later; ++later)
            written_later = slots[later].target == target;
        // Not on Vulkan: bgfx resolves a multisampled texture there after its render pass, from the stored
        // attachment, so the discard would leave nothing to resolve.
        if (!written_later && b_->msaa_ > 0 && bgfx::getRendererType() != bgfx::RendererType::Vulkan)
            discard[s] |= BGFX_CLEAR_DISCARD_COLOR_0;
    }
    if (last_layer_slot >= 0)
        discard[size_t(last_layer_slot)] |= BGFX_CLEAR_DISCARD_DEPTH | BGFX_CLEAR_DISCARD_STENCIL;
    for (size_t s = 0; s < slots.size(); ++s)
    {
        const Schedule::Slot &slot = slots[s];
        const bgfx::ViewId view = View(int(s));
        bgfx::setViewName(view, "ui");
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(view, Handle(slot.target));
        const ViewRect rect = Placed(int(s), slot.rect);
        bgfx::setViewRect(view, uint16_t(rect.x), uint16_t(rect.y), uint16_t(rect.w), uint16_t(rect.h));
        bgfx::setViewScissor(view, 0, 0, 0, 0);
        bgfx::setViewTransform(view, nullptr, nullptr);
        bgfx::setViewClear(view, slot.clear | discard[s], 0, 1.0f, 0);
        bgfx::touch(view);
    }
}

int Executor::Begin(Target target, ViewRect rect, const Accesses &writes, const Accesses &reads, uint16_t clear)
{
    if (live_)
        return plan_[next_++];
    const int slot = schedule_.Place(target, rect, writes, reads, clear);
    plan_.push_back(slot);
    return slot;
}

TargetSet &Executor::Set() const
{
    return b_->sets_[set_];
}

bgfx::FrameBufferHandle Executor::Handle(Target target) const
{
    TargetSet &set = b_->sets_[target.set];
    switch (target.kind)
    {
    case TargetKind::Final:
        return b_->target_;
    case TargetKind::Layer:
        return b_->LayerTarget(set, target.index).fb;
    case TargetKind::Post:
        return b_->PostTarget(set, target.index).fb;
    default:
        return BGFX_INVALID_HANDLE;
    }
}

bgfx::TextureHandle Executor::Color(Target target) const
{
    TargetSet &set = b_->sets_[target.set];
    if (target.kind == TargetKind::Layer)
        return b_->LayerTarget(set, target.index).color;
    if (target.kind == TargetKind::Post)
        return b_->PostTarget(set, target.index).color;
    return BGFX_INVALID_HANDLE;
}

Region Executor::DrawRegion(const Command &command) const
{
    const storm::ui::Geometry &geometry = *command.geometry;
    if (geometry.index_count == 0)
        return {};
    Region region = Clip(command.state.scissor);
    Rml::Vector2f min = geometry.min + command.translation, max = geometry.max + command.translation;
    if (command.state.transformed && !projection_)
    {
        // Bounds of the transformed corners in pixels.
        const Rml::Vector2f corners[4] = {min, {max.x, min.y}, max, {min.x, max.y}};
        Rml::Vector2f lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (const Rml::Vector2f &corner : corners)
        {
            const Rml::Vector4f clip = command.state.transform * Rml::Vector4f(corner.x, corner.y, 0.f, 1.f);
            if (clip.w <= 1e-6f)
                return region; // behind the eye: no useful bounds
            const Rml::Vector2f pixel((clip.x / clip.w + 1.f) * 0.5f * float(Width()), (1.f - clip.y / clip.w) * 0.5f * float(Height()));
            lo = Rml::Math::Min(lo, pixel);
            hi = Rml::Math::Max(hi, pixel);
        }
        min = lo;
        max = hi;
    }
    return region.Intersect({int(std::floor(min.x)) - 1, int(std::floor(min.y)) - 1, int(std::ceil(max.x)) + 1, int(std::ceil(max.y)) + 1});
}

bool Executor::SetScissor(int slot, const Rml::Rectanglei &rect) const
{
    if (!rect.Valid())
        return true;
    const Region region = Clip(rect);
    if (region.Empty())
        return false;
    const ViewRect placed = Placed(slot, {region.x0, region.y0, region.x1 - region.x0, region.y1 - region.y0});
    bgfx::setScissor(uint16_t(placed.x), uint16_t(placed.y), uint16_t(placed.w), uint16_t(placed.h));
    return true;
}

bool Executor::UpsideDown(int slot) const
{
    return !bgfx::getCaps()->originBottomLeft && schedule_.Slots()[size_t(slot)].target.kind != TargetKind::Final;
}

ViewRect Executor::Placed(int slot, ViewRect rect) const
{
    if (UpsideDown(slot))
        rect.y = b_->sets_[schedule_.Slots()[size_t(slot)].target.set].height - rect.y - rect.h;
    return rect;
}

void Executor::SetClip(int slot) const
{
    const float clip[4] = {UpsideDown(slot) ? -1.f : 1.f, 0.f, 0.f, 0.f};
    bgfx::setUniform(b_->uniforms_.clip, clip);
}

void Executor::BindTexture(uint8_t stage, Target source) const
{
    bgfx::setTexture(stage, stage == 0 ? b_->uniforms_.tex : b_->uniforms_.mask, Color(source), kSampleClamp);
}

void Executor::SetFactor(float factor) const
{
    const float value[4] = {factor, factor, factor, factor};
    bgfx::setUniform(b_->uniforms_.factor, value);
}

// GL-space rectangle -> texture coordinate limits clamped to texel centres.
void Executor::SetRegion(Rml::Rectanglei rect_gl) const
{
    const Rml::Vector2f size{float(Width()), float(Height())};
    const Rml::Vector2f min = (Rml::Vector2f(rect_gl.p0) + Rml::Vector2f(0.5f)) / size;
    const Rml::Vector2f max = (Rml::Vector2f(rect_gl.p1) - Rml::Vector2f(0.5f)) / size;
    const float region[4] = {min.x, min.y, max.x, max.y};
    bgfx::setUniform(b_->uniforms_.region, region);
}

// Live runs only; the caller has set textures and uniforms for this draw.
void Executor::Geometry(int slot, const Command &command, ProgramId program, uint64_t state, uint32_t stencil)
{
    if (!SetScissor(slot, command.state.scissor))
    {
        bgfx::discard();
        return;
    }
    bgfx::setVertexBuffer(0, command.geometry->vb);
    bgfx::setIndexBuffer(command.geometry->ib);
    bgfx::setUniform(b_->uniforms_.transform, (projection_ ? *projection_ : command.state.transform).data());
    const float translate[4] = {command.translation.x, command.translation.y, 0.f, 0.f};
    bgfx::setUniform(b_->uniforms_.translate, translate);
    SetClip(slot);
    bgfx::setState(state);
    bgfx::setStencil(stencil);
    bgfx::submit(View(slot), b_->programs_[size_t(program)]);
}

void Executor::Quad(int slot, ProgramId program, uint64_t state, Rml::Vector2f ndc0, Rml::Vector2f ndc1, Rml::Vector2f uv0,
                    Rml::Vector2f uv1, const Rml::Rectanglei &scissor, uint32_t stencil)
{
    if (!SetScissor(slot, scissor) || bgfx::getAvailTransientVertexBuffer(6, Layout()) < 6)
    {
        bgfx::discard();
        return;
    }
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, 6, Layout());
    auto *v = reinterpret_cast<Rml::Vertex *>(buffer.data);
    const Rml::ColourbPremultiplied none(0, 0);
    const Rml::Vertex corners[4] = {{{ndc0.x, ndc0.y}, none, {uv0.x, uv0.y}},
                                    {{ndc1.x, ndc0.y}, none, {uv1.x, uv0.y}},
                                    {{ndc1.x, ndc1.y}, none, {uv1.x, uv1.y}},
                                    {{ndc0.x, ndc1.y}, none, {uv0.x, uv1.y}}};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i)
        v[i] = corners[order[i]];
    bgfx::setVertexBuffer(0, &buffer);
    SetClip(slot);
    bgfx::setState(state);
    bgfx::setStencil(stencil);
    bgfx::submit(View(slot), b_->programs_[size_t(program)]);
}

void Executor::CopyLayerToPost(int layer, const Rml::Rectanglei &scissor)
{
    const Region window = Clip(scissor);
    const int slot = Begin(Post(0), Full(), {{Post(0), window}}, {{Layer(layer), window}});
    if (!live_)
        return;
    BindTexture(0, Layer(layer));
    SetFactor(1.f);
    FullQuad(slot, ProgramId::Passthrough, kWriteColor, scissor);
}

void Executor::Blur(float sigma, int source_destination, int temp, Rml::Rectanglei window_gl, std::optional<Target> first_source)
{
    const Target src = PostSlot(source_destination), tmp = PostSlot(temp);
    const Target src_read = first_source.value_or(src);
    const int w = Width(), h = Height();

    int pass_level = 0;
    SigmaToParameters(sigma, pass_level, sigma);
    Rml::Rectanglei scissor = window_gl;

    // Downscale by halving with bilinear filtering into the lower-left quarter (GL viewport 0,0,w/2,h/2).
    const ViewRect half{0, h - h / 2, w / 2, h / 2};
    const Rml::Vector2f uv_scaling = {(w % 2 == 1) ? (1.f - 1.f / float(w)) : 1.f, (h % 2 == 1) ? (1.f - 1.f / float(h)) : 1.f};
    for (int i = 0; i < pass_level; i++)
    {
        const Rml::Rectanglei previous = scissor;
        scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
        scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
        const bool from_source = (i % 2 == 0);
        const Target into = from_source ? tmp : src;
        const Target from = from_source ? (i == 0 ? src_read : src) : tmp;
        const int slot = Begin(into, half, {{into, FromGl(scissor)}}, {{from, FromGl(previous.Extend(1))}});
        if (live_)
        {
            BindTexture(0, from);
            SetFactor(1.f);
            FullQuad(slot, ProgramId::Passthrough, kWriteColor, Flipped(scissor, h), {}, uv_scaling);
        }
    }

    // The data must end up in the temp buffer.
    if (pass_level % 2 == 0)
    {
        const Target from = pass_level == 0 ? src_read : src;
        const int slot = Begin(tmp, Full(), {{tmp, FromGl(scissor)}}, {{from, FromGl(scissor.Extend(1))}});
        if (live_)
        {
            BindTexture(0, from);
            SetFactor(1.f);
            FullQuad(slot, ProgramId::Passthrough, kWriteColor, Flipped(scissor, h));
        }
    }

    float weights[4];
    float normalization = 0.0f;
    for (int i = 0; i < 4; i++)
    {
        weights[i] = Rml::Math::Absolute(sigma) < 0.1f ? float(i == 0)
                                                       : Rml::Math::Exp(-float(i * i) / (2.0f * sigma * sigma)) /
                                                             (Rml::Math::SquareRoot(2.f * Rml::Math::RMLUI_PI) * sigma);
        normalization += (i == 0 ? 1.f : 2.0f) * weights[i];
    }
    for (float &weight : weights)
        weight /= normalization;
    const auto blur_pass = [&](int slot, Target from, Rml::Vector2f direction, int dimension) {
        BindTexture(0, from);
        bgfx::setUniform(b_->uniforms_.blur_weights, weights);
        const float offset[4] = {direction.x / float(dimension), direction.y / float(dimension), 0.f, 0.f};
        bgfx::setUniform(b_->uniforms_.texel_offset, offset);
        SetRegion(scissor);
        FullQuad(slot, ProgramId::Blur, kWriteColor, Flipped(scissor, h));
    };

    int slot = Begin(src, Full(), {{src, FromGl(scissor)}}, {{tmp, FromGl(scissor)}});
    if (live_)
        blur_pass(slot, tmp, {0.f, 1.f}, h);

    // A 1 px transparent border around the region guards the upscale below against bleeding.
    slot = Begin(tmp, Full(), {{tmp, FromGl(scissor.Extend(1))}}, {{src, FromGl(scissor)}});
    if (live_)
    {
        Quad(slot, ProgramId::QuadColor, kWriteColor, Rml::Vector2f(-1.f), Rml::Vector2f(1.f), {}, {}, Flipped(scissor.Extend(1), h));
        blur_pass(slot, src, {1.f, 0.f}, w);
    }

    // Upscale into the window, then once more by an exact power of two (as the GL renderer).
    slot = Begin(src, Full(), {{src, FromGl(window_gl)}}, {{tmp, FromGl(scissor.Extend(1))}});
    if (!live_)
        return;
    const auto upscale = [&](Rml::Rectanglei from, Rml::Rectanglei to) {
        BindTexture(0, tmp);
        SetFactor(1.f);
        const auto ndc = [&](Rml::Vector2i p) { return Rml::Vector2f(2.f * float(p.x) / float(w) - 1.f, 2.f * float(p.y) / float(h) - 1.f); };
        const auto uv = [&](Rml::Vector2i p) { return Rml::Vector2f(float(p.x) / float(w), float(p.y) / float(h)); };
        Quad(slot, ProgramId::Passthrough, kWriteColor, ndc(to.p0), ndc(to.p1), uv(from.p0), uv(from.p1), Flipped(window_gl, h));
    };
    upscale(scissor, window_gl);
    const Rml::Rectanglei power_of_two = Rml::Rectanglei::FromCorners(scissor.p0 * (1 << pass_level), scissor.p1 * (1 << pass_level));
    if (power_of_two.p0 != window_gl.p0 || power_of_two.p1 != window_gl.p1)
        upscale(scissor, power_of_two);
}

void Executor::Filters(const std::vector<Ref<const Filter>> &filters, const Rml::Rectanglei &scissor, std::optional<Target> source)
{
    const Region window = Clip(scissor);
    const Rml::Rectanglei window_rect = RectOf(window);
    const int h = Height();
    for (const Ref<const Filter> &filter : filters)
    {
        const Target input = source.value_or(Post(0)); // the primary buffer, or the layer for the first filter
        source.reset();
        switch (filter->type)
        {
        case FilterType::Passthrough: {
            const int slot = Begin(Post(1), Full(), {{Post(1), window}}, {{input, window}});
            if (live_)
            {
                BindTexture(0, input);
                SetFactor(filter->blend_factor);
                FullQuad(slot, ProgramId::Passthrough, kWriteColor, scissor);
            }
            SwapPost();
            break;
        }
        case FilterType::Blur:
            Blur(filter->sigma, post_[0], post_[1], Flipped(window_rect, h), input == Post(0) ? std::optional<Target>{} : std::optional<Target>{input});
            break;
        case FilterType::DropShadow: {
            const Rml::Rectanglei window_gl = Flipped(window_rect, h);
            int slot = Begin(Post(1), Full(), {{Post(1), window}}, {{input, window}});
            if (live_)
            {
                BindTexture(0, input);
                const Rml::Colourf color = ToColourf(filter->color);
                const float rgba[4] = {color[0], color[1], color[2], color[3]};
                bgfx::setUniform(b_->uniforms_.shadow_color, rgba);
                SetRegion(window_gl);
                FullQuad(slot, ProgramId::DropShadow, kWriteColor, scissor, filter->offset / Rml::Vector2f(-float(Width()), float(h)));
            }
            if (filter->sigma >= 0.5f)
                Blur(filter->sigma, post_[1], post_[2], window_gl);
            slot = Begin(Post(1), Full(), {{Post(1), window}}, {{input, window}});
            if (live_)
            {
                BindTexture(0, input);
                SetFactor(1.f);
                FullQuad(slot, ProgramId::Passthrough, kWriteColor | kBlendPremultiplied, scissor);
            }
            SwapPost();
            break;
        }
        case FilterType::ColorMatrix: {
            const int slot = Begin(Post(1), Full(), {{Post(1), window}}, {{input, window}});
            if (live_)
            {
                BindTexture(0, input);
                bgfx::setUniform(b_->uniforms_.color_matrix, filter->color_matrix.data());
                FullQuad(slot, ProgramId::ColorMatrix, kWriteColor, scissor);
            }
            SwapPost();
            break;
        }
        case FilterType::MaskImage: {
            const int slot = Begin(Post(1), Full(), {{Post(1), window}}, {{input, window}, {PostSlot(3), window}});
            if (live_)
            {
                BindTexture(0, input);
                BindTexture(1, PostSlot(3));
                FullQuad(slot, ProgramId::BlendMask, kWriteColor, scissor);
            }
            SwapPost();
            break;
        }
        }
    }
}

void Executor::ClipMask(const Command &command)
{
    const DrawState &state = command.state;
    const Target layer = Layer(layers_ - 1);
    int write = 1;
    uint32_t op = BGFX_STENCIL_OP_PASS_Z_REPLACE;
    if (command.clip == Rml::ClipMaskOperation::Intersect)
        op = BGFX_STENCIL_OP_PASS_Z_INCR;
    else
    {
        // Clear the stencil inside the scissor, as glClear does.
        const int clear_to = command.clip == Rml::ClipMaskOperation::Set ? 0 : 1;
        write = 1 - clear_to;
        const int slot = Begin(layer, Full(), {{Stencil(), Clip(state.scissor)}}, {});
        if (live_)
            Quad(slot, ProgramId::QuadColor, BGFX_STATE_MSAA, Rml::Vector2f(-1.f), Rml::Vector2f(1.f), {}, {}, state.scissor,
                 BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(clear_to) | kStencilKeep | BGFX_STENCIL_OP_PASS_Z_REPLACE);
    }
    const Region region = DrawRegion(command);
    if (region.Empty())
        return;
    const int slot = Begin(layer, Full(), {{Stencil(), region}}, {});
    if (live_ && command.geometry->Ensure())
        Geometry(slot, command, ProgramId::Color, BGFX_STATE_MSAA,
                 BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(write) | kStencilKeep | op);
}

// The layer region is copied upside down into post 0 (textures run top row first), then
// blitted into the texture at the start of the next view.
void Executor::SaveTexture(const Command &command)
{
    Texture &texture = *command.texture;
    const Region region = Clip(command.state.scissor);
    if (texture.released || region.Empty())
        return;
    const Target layer = Layer(layers_ - 1), post = Post(0);
    const int slot = Begin(post, Full(), {{post, region}}, {{layer, region}});
    const Target output{TargetKind::Texture, 0, 0, &texture};
    if (!live_)
    {
        schedule_.AddEarly({post, region}, slot + 1, false);
        schedule_.AddEarly({output, {0, 0, 1, 1}}, slot + 1, true);
    }
    filled_.insert(&texture);
    if (!live_ || !texture.Ensure())
        return;
    const int w = Width(), h = Height();
    const auto ndc = [&](int x, int y) { return Rml::Vector2f(2.f * float(x) / float(w) - 1.f, 2.f * float(y) / float(h) - 1.f); };
    BindTexture(0, layer);
    SetFactor(1.f);
    Quad(slot, ProgramId::Passthrough, kWriteColor, ndc(region.x0, h - region.y1), ndc(region.x1, h - region.y0),
         {float(region.x0) / float(w), float(h - region.y0) / float(h)}, {float(region.x1) / float(w), float(h - region.y1) / float(h)},
         Rml::Rectanglei::MakeInvalid());
    bgfx::TextureRegion destination, source;
    const int copy_w = std::min(region.x1 - region.x0, texture.width), copy_h = std::min(region.y1 - region.y0, texture.height);
    destination.init(texture.handle, 0, 0, uint16_t(copy_w), uint16_t(copy_h));
    source.init(Color(post), uint16_t(region.x0), uint16_t(h - region.y1), uint16_t(copy_w), uint16_t(copy_h));
    bgfx::blit(View(slot + 1), destination, source);
    texture.filled = true;
}

uint32_t Executor::StencilTest(const DrawState &state)
{
    return state.stencil_test ? BGFX_STENCIL_TEST_EQUAL | BGFX_STENCIL_FUNC_REF(state.stencil_ref) | kStencilKeep | BGFX_STENCIL_OP_PASS_Z_KEEP
                              : BGFX_STENCIL_NONE;
}

void Executor::Composite(const Command &command, bool unfiltered)
{
    const DrawState &state = command.state;
    const uint32_t stencil = StencilTest(state);
    const Region window = Clip(state.scissor);
    if (window.Empty())
        return;
    // Post buffers carry nothing from one composite to the next: always start from the
    // same roles, so the passes of different elements land in the same views.
    post_ = {0, 1, 2};
    const std::vector<Ref<const Filter>> none;
    const std::vector<Ref<const Filter>> &filters = unfiltered ? none : command.filters;
    if (!filters.empty() && filters.front()->type == FilterType::Blur)
    {
        // A leading blur reads the layer directly.
        Blur(filters.front()->sigma, post_[0], post_[1], Flipped(RectOf(window), Height()), Layer(command.source));
        Filters({filters.begin() + 1, filters.end()}, state.scissor);
    }
    else if (!filters.empty() && filters.front()->type != FilterType::MaskImage)
        Filters(filters, state.scissor, Layer(command.source));
    else if (!filters.empty())
    {
        CopyLayerToPost(command.source, state.scissor);
        Filters(filters, state.scissor);
    }
    // Without filters the layer itself is composited.
    const Target result = filters.empty() ? Layer(command.source) : Post(0);
    const Target destination = Layer(command.destination);
    Accesses reads{{result, window}};
    if (state.stencil_test)
        reads.Add({Stencil(), window});
    const int slot = Begin(destination, Full(), {{destination, window}}, reads);
    if (live_)
    {
        BindTexture(0, result);
        SetFactor(1.f);
        FullQuad(slot, ProgramId::Passthrough, kWriteColor | (command.blend == Rml::BlendMode::Blend ? kBlendPremultiplied : 0), state.scissor,
                 {}, Rml::Vector2f(1.f), stencil);
    }
}

void Executor::Run(const Command &command, bool degraded)
{
    const DrawState &state = command.state;
    const uint32_t stencil = StencilTest(state);
    switch (command.op)
    {
    case Op::Draw: {
        const Region region = DrawRegion(command);
        if (region.Empty())
            return;
        const Target layer = Layer(layers_ - 1);
        ProgramId program = ProgramId::Color;
        Accesses reads;
        if (state.stencil_test)
            reads.Add({Stencil(), region});
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        if (command.shader)
            program = ProgramId::Gradient;
        else if (command.texture)
        {
            Texture &texture = *command.texture;
            program = ProgramId::Texture;
            switch (texture.kind)
            {
            case Texture::Kind::Pixels:
                if (texture.pixels.empty() && !texture.Current())
                    return;
                break;
            case Texture::Kind::Target:
                if (!filled_.count(&texture) && !(texture.filled && texture.Current()))
                {
                    if (!live_ && b_->Awaited(texture))
                        ++starved_;
                    return; // its job has not run yet
                }
                reads.Add({{TargetKind::Texture, 0, 0, &texture}, {0, 0, 1, 1}});
                break;
            case Texture::Kind::Picture:
                handle = b_->PictureHandle(texture);
                if (!bgfx::isValid(handle))
                    return;
                program = ProgramId::TextureStraight;
                break;
            }
        }
        const int slot = Begin(layer, Full(), {{layer, region}}, reads);
        if (!live_ || !command.geometry->Ensure())
            return;
        if (command.texture && command.texture->kind != Texture::Kind::Picture)
        {
            if (!command.texture->Ensure())
                return;
            handle = command.texture->handle;
        }
        if (command.shader)
        {
            const Shader &shader = *command.shader;
            const float gradient[4] = {float(int(shader.function)), float(shader.stop_positions.size()), 0.f, 0.f};
            const float pv[4] = {shader.p.x, shader.p.y, shader.v.x, shader.v.y};
            float positions[kMaxStops * 4] = {};
            for (size_t i = 0; i < shader.stop_positions.size(); ++i)
                positions[i * 4] = shader.stop_positions[i];
            bgfx::setUniform(b_->uniforms_.gradient, gradient);
            bgfx::setUniform(b_->uniforms_.gradient_pv, pv);
            bgfx::setUniform(b_->uniforms_.stop_colors, &shader.stop_colors[0][0], uint16_t(shader.stop_colors.size()));
            bgfx::setUniform(b_->uniforms_.stop_positions, positions, uint16_t(shader.stop_positions.size()));
        }
        if (bgfx::isValid(handle))
            bgfx::setTexture(0, b_->uniforms_.tex, handle);
        Geometry(slot, command, program, kWriteColor | kBlendPremultiplied, stencil);
        break;
    }
    case Op::ClipMask:
        ClipMask(command);
        break;
    case Op::PushLayer: {
        if (degraded)
            break;
        ++layers_;
        // Clear inside the scissor, as glClear does.
        const Target layer = Layer(layers_ - 1);
        Region clear = Clip(state.scissor);
        if (!command.layer_read_whole)
            clear = command.layer_reads.Valid() ? clear.Intersect(Clip(command.layer_reads)) : Region{};
        if (clear.Empty())
            break;
        const int slot = Begin(layer, Full(), {{layer, clear}}, {});
        if (live_)
            Quad(slot, ProgramId::QuadColor, kWriteColor, Rml::Vector2f(-1.f), Rml::Vector2f(1.f), {}, {}, RectOf(clear));
        break;
    }
    case Op::Composite:
        if (degraded)
            break;
        if (!live_ && budget_ > 0 && !command.filters.empty() && !unfiltered_.count(&command))
        {
            // Filters that would overflow the view range are dropped for this element only.
            Executor trial = *this;
            trial.Composite(command, false);
            if (trial.Views() + 1 <= budget_)
            {
                *this = std::move(trial);
                break;
            }
            unfiltered_.insert(&command);
            b_->ReportOverflow("an element drawn without its filters");
        }
        Composite(command, unfiltered_.count(&command) > 0);
        break;
    case Op::PopLayer:
        if (!degraded)
            --layers_;
        break;
    case Op::SaveTexture:
        if (!degraded)
            SaveTexture(command);
        break;
    case Op::SaveMask: {
        if (degraded)
            break;
        const Region window = Clip(state.scissor);
        CopyLayerToPost(layers_ - 1, state.scissor);
        const int slot = Begin(PostSlot(3), Full(), {{PostSlot(3), window}}, {{Post(0), window}});
        if (live_)
        {
            BindTexture(0, Post(0));
            SetFactor(1.f);
            FullQuad(slot, ProgramId::Passthrough, kWriteColor, state.scissor);
        }
        break;
    }
    }
}

void Executor::Main(const Commands &commands, bool clear_final, bool degraded)
{
    set_ = 0;
    projection_.reset();
    layers_ = 1;
    post_ = {0, 1, 2};
    // Depth too: it shares the stencil's attachment (D24S8), and a tile GPU loads a combined buffer that is not
    // cleared whole from memory.
    Begin(Layer(0), Full(), {{Layer(0), Whole()}, {Stencil(), Whole()}}, {}, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL);
    for (const Command &command : commands)
        Run(command, degraded);
    // Composite the base layer over the target (the game picture), premultiplied.
    const Target final_target{TargetKind::Final};
    const bool own_target = bgfx::isValid(b_->target_);
    const int slot = Begin(final_target, Full(), {{final_target, Whole()}}, {{Layer(0), Whole()}},
                           clear_final && own_target ? BGFX_CLEAR_COLOR : BGFX_CLEAR_NONE);
    if (!live_)
        return;
    BindTexture(0, Layer(0));
    SetFactor(1.f);
    FullQuad(slot, ProgramId::Passthrough, kWriteColor | kBlendPremultiplied, Rml::Rectanglei::MakeInvalid());
}

void Executor::BeginJob(const Job &job)
{
    set_ = 1;
    const TargetSet &set = Set();
    projection_ = Rml::Matrix4f::ProjectOrtho(0, float(set.width), float(set.height), 0, -10000, 10000);
    layers_ = job.cursor == 0 ? job.depth : job.layers;
    post_ = job.cursor == 0 ? std::array<int, 3>{0, 1, 2} : job.post;
}

} // namespace

std::unique_ptr<BgfxBackend> BgfxBackend::Create(const BgfxBackendOptions &options, std::string *error)
{
    auto backend = std::make_unique<BgfxBackendImpl>(options);
    if (!backend->Start(error))
        return nullptr;
    return backend;
}

} // namespace storm::ui
