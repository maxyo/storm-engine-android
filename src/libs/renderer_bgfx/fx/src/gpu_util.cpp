#include "gpu_util.hpp"
#include "storm/fx/log.hpp"
#include "storm/render/shader_blob.hpp"

#include <cstring>

#include "storm_fx_shaders/includes.inc"

namespace storm::fx::detail
{
namespace
{
struct Blob
{
    const char *name;
    render::ShaderBlob blob;
};

constexpr Blob kBlobs[] = {
#define FX_SHADER(name) {#name, STORM_SHADER_BLOB(name)},
#include "storm_fx_shaders/table.inc"
#undef FX_SHADER
};

bgfx::ShaderHandle LoadShader(const char *name)
{
    for (const Blob &blob : kBlobs)
        if (std::strcmp(blob.name, name) == 0)
            return render::CreateShader(blob.blob, name);
    return BGFX_INVALID_HANDLE;
}

struct ScreenVertex
{
    float pos[3];
    float data[3];
};
} // namespace

bgfx::ProgramHandle LoadProgram(const char *vertex, const char *fragment)
{
    const bgfx::ShaderHandle vs = LoadShader(vertex);
    const bgfx::ShaderHandle fs = LoadShader(fragment);
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs))
    {
        Log(LogLevel::Error, "no shader blob %s: program %s + %s not created", bgfx::isValid(vs) ? fragment : vertex, vertex,
            fragment);
        if (bgfx::isValid(vs))
            bgfx::destroy(vs);
        if (bgfx::isValid(fs))
            bgfx::destroy(fs);
        return BGFX_INVALID_HANDLE;
    }
    // bgfx refuses a pair whose vertex outputs differ from the fragment inputs ($output / $input).
    const bgfx::ProgramHandle program = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(program))
        Log(LogLevel::Error, "program %s + %s not created (vertex outputs do not match the fragment inputs?)", vertex, fragment);
    return program;
}

void SubmitScreenTriangle(bgfx::ViewId view, bgfx::ProgramHandle program, uint64_t state, bool flip_v,
                          const std::array<std::array<float, 3>, 3> *corner_data)
{
    static const bgfx::VertexLayout layout = [] {
        bgfx::VertexLayout l;
        l.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float).end();
        return l;
    }();
    if (bgfx::getAvailTransientVertexBuffer(3, layout) < 3)
        return;
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, 3, layout);
    auto *v = reinterpret_cast<ScreenVertex *>(buffer.data);
    // Render-target textures have their origin at the bottom on GL and at the top elsewhere.
    const bool v_up = bgfx::getCaps()->originBottomLeft != flip_v;
    const float sign = flip_v && corner_data ? -1.0f : 1.0f;
    const float xs[3] = {-1.0f, 3.0f, -1.0f};
    const float ys[3] = {-1.0f, -1.0f, 3.0f};
    for (int i = 0; i < 3; ++i)
    {
        v[i].pos[0] = xs[i];
        v[i].pos[1] = ys[i] * sign;
        v[i].pos[2] = 0.0f;
        if (corner_data)
        {
            // Extrapolate from the three view corners to the oversized triangle.
            const auto &bl = (*corner_data)[0], &br = (*corner_data)[1], &tl = (*corner_data)[2];
            const float s = (xs[i] + 1.0f) * 0.5f, t = (ys[i] + 1.0f) * 0.5f;
            for (int c = 0; c < 3; ++c)
                v[i].data[c] = bl[c] + (br[c] - bl[c]) * s + (tl[c] - bl[c]) * t;
        }
        else
        {
            const float t = (ys[i] + 1.0f) * 0.5f;
            v[i].data[0] = (xs[i] + 1.0f) * 0.5f;
            v[i].data[1] = v_up ? t : 1.0f - t;
            v[i].data[2] = 0.0f;
        }
    }
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::setState(state);
    bgfx::submit(view, program);
}

namespace
{
bool Format(bgfx::TextureFormat::Enum format, uint32_t flags)
{
    return (bgfx::getCaps()->formats[format] & flags) == flags;
}
} // namespace

bool SupportsLut3d()
{
    return Format(bgfx::TextureFormat::RGBA8, BGFX_CAPS_FORMAT_TEXTURE_3D);
}

bool SupportsAoVolume()
{
    return Format(bgfx::TextureFormat::RGBA8, BGFX_CAPS_FORMAT_TEXTURE_3D);
}

bool SupportsShadowMap()
{
    // Compare samplers have no capability bit of their own: every backend bgfx offers for this engine has them.
    return Format(bgfx::TextureFormat::D16, BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
}

bool SupportsNormalMap(bgfx::TextureFormat::Enum format)
{
    return Format(format, BGFX_CAPS_FORMAT_TEXTURE_2D);
}

uint8_t MsaaSamples()
{
    // The scene target is RGBA8 colour + D24S8 depth (src/shim): both must render with the sample count.
    const auto both = [](uint32_t flag) {
        return Format(bgfx::TextureFormat::RGBA8, flag) && Format(bgfx::TextureFormat::D24S8, flag);
    };
    return both(BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X4) ? 4 : both(BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X2) ? 2 : 1;
}

bool SupportsSmaa()
{
    return Format(bgfx::TextureFormat::R8, BGFX_CAPS_FORMAT_TEXTURE_2D) && Format(bgfx::TextureFormat::RG8, BGFX_CAPS_FORMAT_TEXTURE_2D);
}

bool SupportsRenderTarget(bgfx::TextureFormat::Enum format)
{
    return Format(format, BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
}

bgfx::TextureFormat::Enum SmaaEdgeFormat()
{
    return Format(bgfx::TextureFormat::RG8, BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ? bgfx::TextureFormat::RG8 : bgfx::TextureFormat::RGBA8;
}

void SetupView(bgfx::ViewId view, const char *name, bgfx::FrameBufferHandle target, uint16_t width, uint16_t height, bool clear)
{
    bgfx::setViewName(view, name);
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(view, target);
    bgfx::setViewRect(view, 0, 0, width, height);
    bgfx::setViewClear(view, clear ? BGFX_CLEAR_COLOR : BGFX_CLEAR_NONE, 0x00000000);
}

} // namespace storm::fx::detail
