#include "device.hpp"
#include "effects/effects.hpp"
#include "perf.hpp"
#include "ports.hpp"
#include "util.hpp"
#include <cfloat>


namespace storm::shim
{

// ---------------------------------------------------------------------------
// vertex layouts
namespace
{
bgfx::Attrib::Enum texcoord_attrib(uint32_t index)
{
    return static_cast<bgfx::Attrib::Enum>(bgfx::Attrib::TexCoord0 + std::min(index, 7u));
}

void finish_layout(LayoutInfo &info, uint32_t stream, uint32_t stride)
{
    bgfx::VertexLayout &layout = info.layout[stream];
    layout.end();
    info.extent[stream] = layout.m_stride;
    if (stride > layout.m_stride)
        layout.m_stride = static_cast<uint16_t>(stride);
    // The hash covers attributes and offsets only; make it unique per stride.
    layout.m_hash ^= static_cast<uint32_t>(layout.m_stride) * 0x9E3779B1u;
    info.stride[stream] = layout.m_stride;
    info.handle[stream] = bgfx::createVertexLayout(layout);
}
} // namespace

const LayoutInfo *Device::fvf_layout(DWORD fvf, uint32_t stride)
{
    const uint64_t key = fvf | (static_cast<uint64_t>(stride) << 32);
    auto &slot = fvf_layouts[key];
    if (slot)
        return slot.get();
    slot = std::make_unique<LayoutInfo>();
    LayoutInfo &info = *slot;
    bgfx::VertexLayout &layout = info.layout[0];
    layout.begin();
    const DWORD position = fvf & D3DFVF_POSITION_MASK;
    if (position == D3DFVF_XYZRHW || position == D3DFVF_XYZW)
    {
        layout.add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float);
        info.rhw = position == D3DFVF_XYZRHW;
    }
    else
    {
        layout.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
        if (position == D3DFVF_XYZB2 && (fvf & D3DFVF_LASTBETA_UBYTE4))
        {
            // GPU skinning: one blend weight, bone indices in the last beta (GEOS::AVERTEX0).
            layout.add(bgfx::Attrib::Weight, 1, bgfx::AttribType::Float);
            layout.add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8);
            info.skinned = true;
        }
        else if (position >= D3DFVF_XYZB1 && position <= D3DFVF_XYZB5)
        {
            const uint32_t weights = (position - D3DFVF_XYZB1) / 2 + 1;
            layout.skip(static_cast<uint8_t>(weights * 4));
            trap("FVF with blend weights other than XYZB2 | LASTBETA_UBYTE4");
        }
    }
    if (fvf & D3DFVF_NORMAL)
    {
        layout.add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float);
        info.has_normal = true;
    }
    if (fvf & D3DFVF_PSIZE)
        layout.skip(4);
    if (fvf & D3DFVF_DIFFUSE)
    {
        layout.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true);
        info.has_color0 = true;
    }
    if (fvf & D3DFVF_SPECULAR)
    {
        layout.add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Uint8, true);
        info.has_color1 = true;
    }
    info.texcoord_count = static_cast<uint8_t>((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
    for (uint32_t i = 0; i < info.texcoord_count; ++i)
    {
        static const uint8_t sizes[4] = {2, 3, 4, 1};
        const uint8_t size = sizes[(fvf >> (16 + i * 2)) & 3];
        info.texcoord_size[i] = size;
        layout.add(texcoord_attrib(i), size, bgfx::AttribType::Float);
    }
    info.stream_count = 1;
    finish_layout(info, 0, stride);
    return &info;
}

const LayoutInfo *Device::build_decl_layout(VertexDeclaration &decl)
{
    // A stream's stride is the one bound with SetStreamSource, or the attributes' extent when that is smaller (0).
    for (const auto &cached : decl.layouts)
    {
        bool same = true;
        for (uint32_t i = 0; same && i < cached->stream_count; ++i)
            same = cached->stride[i] == std::max<uint32_t>(s.stream_stride[i], cached->extent[i]);
        if (same)
            return cached.get();
    }
    decl.layouts.push_back(std::make_unique<LayoutInfo>());
    LayoutInfo &info = *decl.layouts.back();
    uint32_t streams = 0;
    for (const auto &element : decl.elements)
        streams = std::max<uint32_t>(streams, element.Stream + 1u);
    if (streams > kMaxStreams)
    {
        decl.layouts.pop_back();
        trap("vertex declaration with more than 4 streams");
        return nullptr;
    }
    for (uint32_t stream = 0; stream < streams; ++stream)
    {
        std::vector<D3DVERTEXELEMENT9> elements;
        for (const auto &element : decl.elements)
            if (element.Stream == stream)
                elements.push_back(element);
        std::sort(elements.begin(), elements.end(),
                  [](const D3DVERTEXELEMENT9 &a, const D3DVERTEXELEMENT9 &b) { return a.Offset < b.Offset; });
        bgfx::VertexLayout &layout = info.layout[stream];
        layout.begin();
        uint32_t offset = 0;
        for (const auto &element : elements)
        {
            if (element.Offset > offset)
                layout.skip(static_cast<uint8_t>(element.Offset - offset));
            uint8_t count = 0;
            uint32_t bytes = 0;
            bgfx::AttribType::Enum type = bgfx::AttribType::Float;
            bool normalized = false;
            switch (element.Type)
            {
            case D3DDECLTYPE_FLOAT1:
            case D3DDECLTYPE_FLOAT2:
            case D3DDECLTYPE_FLOAT3:
            case D3DDECLTYPE_FLOAT4:
                count = static_cast<uint8_t>(element.Type - D3DDECLTYPE_FLOAT1 + 1);
                bytes = count * 4u;
                break;
            case D3DDECLTYPE_D3DCOLOR:
            case D3DDECLTYPE_UBYTE4N:
                count = 4, bytes = 4, type = bgfx::AttribType::Uint8, normalized = true;
                break;
            case D3DDECLTYPE_UBYTE4:
                count = 4, bytes = 4, type = bgfx::AttribType::Uint8;
                break;
            case D3DDECLTYPE_SHORT2:
            case D3DDECLTYPE_SHORT2N:
                count = 2, bytes = 4, type = bgfx::AttribType::Int16, normalized = element.Type == D3DDECLTYPE_SHORT2N;
                break;
            case D3DDECLTYPE_SHORT4:
            case D3DDECLTYPE_SHORT4N:
                count = 4, bytes = 8, type = bgfx::AttribType::Int16, normalized = element.Type == D3DDECLTYPE_SHORT4N;
                break;
            default:
                decl.layouts.pop_back();
                trap("vertex declaration element type");
                return nullptr;
            }
            bgfx::Attrib::Enum attrib = bgfx::Attrib::Count;
            switch (element.Usage)
            {
            case D3DDECLUSAGE_POSITION:
                attrib = bgfx::Attrib::Position;
                break;
            case D3DDECLUSAGE_POSITIONT:
                attrib = bgfx::Attrib::Position;
                info.rhw = true;
                break;
            case D3DDECLUSAGE_NORMAL:
                attrib = bgfx::Attrib::Normal;
                info.has_normal = true;
                break;
            case D3DDECLUSAGE_COLOR:
                attrib = element.UsageIndex == 0 ? bgfx::Attrib::Color0 : bgfx::Attrib::Color1;
                (element.UsageIndex == 0 ? info.has_color0 : info.has_color1) = true;
                break;
            case D3DDECLUSAGE_TEXCOORD:
                attrib = texcoord_attrib(element.UsageIndex);
                if (element.UsageIndex < 8)
                    info.texcoord_size[element.UsageIndex] = count;
                info.texcoord_count = std::max<uint8_t>(info.texcoord_count, element.UsageIndex + 1);
                break;
            case D3DDECLUSAGE_BLENDWEIGHT:
                attrib = bgfx::Attrib::Weight;
                break;
            case D3DDECLUSAGE_BLENDINDICES:
                attrib = bgfx::Attrib::Indices;
                break;
            case D3DDECLUSAGE_TANGENT:
                attrib = bgfx::Attrib::Tangent;
                break;
            case D3DDECLUSAGE_BINORMAL:
                attrib = bgfx::Attrib::Bitangent;
                break;
            default:
                break;
            }
            if (attrib == bgfx::Attrib::Count || layout.has(attrib))
                layout.skip(static_cast<uint8_t>(bytes));
            else
                layout.add(attrib, count, type, normalized);
            offset = element.Offset + bytes;
        }
        finish_layout(info, stream, s.stream_stride[stream]);
    }
    info.stream_count = static_cast<uint8_t>(streams);
    return &info;
}

const LayoutInfo *Device::current_layout()
{
    if (s.decl)
        return build_decl_layout(*s.decl);
    return fvf_layout(s.fvf, s.stream_stride[0]);
}

// ---------------------------------------------------------------------------
// uniforms
void Device::set_uniform(UniformId id, const float *data, uint16_t count)
{
    Uniform &uniform = uniforms[id];
    const size_t per = (id == U_PROJ || id == U_TEXMTX) ? 16 : 4;
    const size_t floats = per * count;
    if (uniform.sent && uniform.last.size() == floats && memcmp(uniform.last.data(), data, floats * sizeof(float)) == 0)
        return;
    uniform.last.assign(data, data + floats);
    uniform.sent = true;
    bgfx::setUniform(uniform.handle, data, count);
    ++perf().uniform_updates;
    perf().uniform_bytes += floats * sizeof(float);
    uniforms_changed = true;
}

// Buffers drawn in a later frame than they were written in: a range of bgfx's shared dynamic buffers, so creating one
// makes no GL buffer (one GL buffer each made opening a scene or a screen create hundreds in a frame; bgfx frees a
// shared buffer once its last range is gone). A vertex buffer's range is created in the layout it is drawn with, so it
// starts at a whole vertex of that stride; drawn with another stride, it is created again. Rewrites update the written
// range in place.
void Device::upload_buffer(VertexBuffer &vb, const bgfx::VertexLayout &layout)
{
    const uint32_t size = static_cast<uint32_t>(vb.shadow.size());
    const uint32_t stride = layout.getStride();
    const uint32_t vertices = size / stride; // a trailing partial vertex is never drawn
    if (bgfx::isValid(vb.handle) && vb.stride != stride)
    {
        bgfx::destroy(vb.handle);
        vb.handle = BGFX_INVALID_HANDLE;
    }
    if (!bgfx::isValid(vb.handle))
    {
        vb.handle = bgfx::createDynamicVertexBuffer(bgfx::copy(vb.shadow.data(), vertices * stride), layout);
        vb.stride = stride;
        ++perf().buffer_creates;
        perf().buffer_upload_bytes += vertices * stride;
    }
    else if (vb.dirty_begin < vb.dirty_end)
    {
        const uint32_t first = vb.dirty_begin / stride;
        const uint32_t end = std::min((std::min(vb.dirty_end, size) + stride - 1) / stride, vertices);
        if (end > first)
            bgfx::update(vb.handle, first, bgfx::copy(vb.shadow.data() + first * stride, (end - first) * stride));
        perf().buffer_upload_bytes += (end - first) * stride;
    }
    else
        return;
    ++perf().buffer_uploads;
    perf_resize(&render::PerfCounters::buffer_gpu_bytes, vb.gpu_bytes, size);
    vb.gpu_bytes = size;
    vb.dirty_begin = UINT32_MAX;
    vb.dirty_end = 0;
}

void Device::upload_buffer(IndexBuffer &ib, bool index32)
{
    const uint32_t size = static_cast<uint32_t>(ib.shadow.size());
    const uint32_t index_size = index32 ? 4 : 2;
    const uint32_t indices = size / index_size; // a trailing partial index is never drawn
    if (!bgfx::isValid(ib.handle))
    {
        ib.handle = bgfx::createDynamicIndexBuffer(bgfx::copy(ib.shadow.data(), indices * index_size),
                                                   index32 ? BGFX_BUFFER_INDEX32 : BGFX_BUFFER_NONE);
        ++perf().buffer_creates;
        perf().buffer_upload_bytes += indices * index_size;
    }
    else if (ib.dirty_begin < ib.dirty_end)
    {
        const uint32_t first = ib.dirty_begin / index_size;
        const uint32_t end = std::min((std::min(ib.dirty_end, size) + index_size - 1) / index_size, indices);
        if (end > first)
            bgfx::update(ib.handle, first, bgfx::copy(ib.shadow.data() + first * index_size, (end - first) * index_size));
        perf().buffer_upload_bytes += (end - first) * index_size;
    }
    else
        return;
    ++perf().buffer_uploads;
    perf_resize(&render::PerfCounters::buffer_gpu_bytes, ib.gpu_bytes, size);
    ib.gpu_bytes = size;
    ib.dirty_begin = UINT32_MAX;
    ib.dirty_end = 0;
}

void Device::forget_transient(const VertexBuffer *vb)
{
    std::erase_if(transient_ranges, [vb](const TransientRange &r) { return r.vb == vb; });
}

void Device::invalidate_uniforms()
{
    for (auto &uniform : uniforms)
        uniform.sent = false;
}

namespace
{
// Bit mask of the arguments (0, 1, 2) a texture operation reads.
uint32_t op_args(DWORD op)
{
    switch (op)
    {
    case D3DTOP_SELECTARG1:
    case D3DTOP_PREMODULATE:
        return 2;
    case D3DTOP_SELECTARG2:
        return 4;
    case D3DTOP_MULTIPLYADD:
    case D3DTOP_LERP:
        return 7;
    default:
        return 6;
    }
}

bool uses_texture(DWORD op, DWORD arg0, DWORD arg1, DWORD arg2)
{
    if (op == D3DTOP_DISABLE)
        return false;
    if (op == D3DTOP_BLENDTEXTUREALPHA || op == D3DTOP_BLENDTEXTUREALPHAPM)
        return true;
    const uint32_t mask = op_args(op);
    return ((mask & 1) && (arg0 & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ||
           ((mask & 2) && (arg1 & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ||
           ((mask & 4) && (arg2 & D3DTA_SELECTMASK) == D3DTA_TEXTURE);
}

float encode_arg(DWORD arg)
{
    return static_cast<float>((arg & D3DTA_SELECTMASK) + 16 * ((arg >> 4) & 3));
}

float material_source(DWORD source, bool color_vertex, const LayoutInfo &layout)
{
    if (!color_vertex)
        return 0.0f;
    if (source == D3DMCS_COLOR1)
        return layout.has_color0 ? 1.0f : 0.0f;
    if (source == D3DMCS_COLOR2)
        return layout.has_color1 ? 2.0f : 0.0f;
    return 0.0f;
}
} // namespace

void Device::apply_ff_uniforms(const LayoutInfo &layout, bool vs_port, bool ps_port, const effects::FfDraw &fx)
{
    const bgfx::Caps *caps = Context::instance().caps();
    const bool flip = Context::instance().flip_y();
    const DWORD *rs = s.rs;
    const float vw = static_cast<float>(std::max<DWORD>(1, s.viewport.Width));
    const float vh = static_cast<float>(std::max<DWORD>(1, s.viewport.Height));
    // Pre-transformed vertices are in the pixels the game addresses; the half-pixel shift of D3D9 is a half pixel
    // of the store (fewer pixels on a scaled scene store), like point sizes.
    const std::array<float, 2> scale = target_scale();

    // D3D clip space -> backend clip space (row-vector matrix, applied last).
    D3DMATRIX fix = identity();
    const float fy = flip ? -1.0f : 1.0f;
    const float bias = as_float(rs[D3DRS_DEPTHBIAS]);
    fix.m[1][1] = fy;
    fix.m[3][0] = 1.0f / (vw * scale[0]);
    fix.m[3][1] = -fy / (vh * scale[1]);
    if (caps->homogeneousDepth)
    {
        // D3D keeps z == 0 and z == w; after the remap to [-1, 1] rounding would clip them,
        // so the range is shrunk by a few depth-buffer steps.
        const float keep = 1.0f - 2e-6f;
        fix.m[2][2] = 2.0f * keep;
        fix.m[3][2] = (-1.0f + 2.0f * bias) * keep;
    }
    else
        fix.m[3][2] = bias;

    D3DMATRIX world_view, proj;
    multiply(s.world, s.view, world_view);
    if (layout.rhw)
    {
        D3DMATRIX screen = identity();
        screen.m[0][0] = 2.0f / vw;
        screen.m[1][1] = -2.0f / vh;
        screen.m[3][0] = -1.0f - 2.0f * static_cast<float>(s.viewport.X) / vw;
        screen.m[3][1] = 1.0f + 2.0f * static_cast<float>(s.viewport.Y) / vh;
        multiply(screen, fix, proj);
    }
    else
        multiply(s.proj, fix, proj);
    if (!vs_port)
        bgfx::setTransform(&world_view.m[0][0]);
    set_uniform(U_PROJ, &proj.m[0][0], 1);

    if (vs_port)
    {
        // vs_1_1 programs output D3D clip space; they apply the fix-up matrix themselves.
        set_uniform(U_PROJ, &fix.m[0][0], 1);
        set_uniform(U_VSC, &s.vs_const[0][0], s.vs->port->vsc_count); // the registers the port declares
    }
    if (ps_port)
    {
        set_uniform(U_PSC, &s.ps_const[0][0], kPsConstants);
        // texbem/texbeml read the stage's bump environment matrix: (M00, M01, M10, M11) per stage.
        float bem[kFfStages][4];
        for (uint32_t stage = 0; stage < kFfStages; ++stage)
            for (uint32_t i = 0; i < 4; ++i)
                bem[stage][i] = as_float(s.tss[stage][D3DTSS_BUMPENVMAT00 + i]);
        set_uniform(U_BEM, &bem[0][0], kFfStages);
    }

    const bool lighting = rs[D3DRS_LIGHTING] != 0 && !layout.rhw;
    const bool color_vertex = rs[D3DRS_COLORVERTEX] != 0;
    const bool fog = rs[D3DRS_FOGENABLE] != 0 && !layout.rhw;
    // D3D9 fog: a table (pixel) mode wins and is evaluated from depth; otherwise the fixed-function
    // vertex mode is used, and with no mode at all the factor comes from the vertex (the vertex
    // shader's oFog, or the specular alpha of fixed-function vertices).
    enum : uint32_t { kFogOff = 0, kFogFactor = 4, kFogAerial = 5 };
    uint32_t fog_mode = kFogOff;
    bool fog_from_clip_depth = false, fog_from_specular = false;
    if (fog)
    {
        if (rs[D3DRS_FOGTABLEMODE] != D3DFOG_NONE)
        {
            fog_mode = rs[D3DRS_FOGTABLEMODE];
            fog_from_clip_depth = vs_port;
        }
        else if (vs_port)
            fog_mode = kFogFactor;
        else if (rs[D3DRS_FOGVERTEXMODE] != D3DFOG_NONE)
            fog_mode = rs[D3DRS_FOGVERTEXMODE];
        else
        {
            fog_mode = kFogFactor;
            fog_from_specular = true;
        }
    }
    // Effects: the game's exp fog with the aerial perspective (effects/lighting.hpp, ff_finish.sh mode 5).
    std::array<float, 2> aerial_haze{};
    if (fog_mode == D3DFOG_EXP && effects::Host::instance().lighting().aerial_fog(s.view, vs_port, aerial_haze))
        fog_mode = kFogAerial;

    if (!vs_port)
    {
        float light_pos[kMaxLights][4] = {}, light_dir[kMaxLights][4] = {}, light_diffuse[kMaxLights][4] = {};
        float light_ambient[kMaxLights][4] = {}, light_specular[kMaxLights][4] = {}, light_att[kMaxLights][4] = {};
        uint32_t light_count = 0;
        if (lighting)
        {
            for (const Light &entry : lights)
            {
                if (!entry.enabled || light_count >= kMaxLights)
                    continue;
                const D3DLIGHT9 &l = entry.light;
                const D3DMATRIX &v = s.view;
                float *p = light_pos[light_count], *d = light_dir[light_count];
                p[0] = l.Position.x * v.m[0][0] + l.Position.y * v.m[1][0] + l.Position.z * v.m[2][0] + v.m[3][0];
                p[1] = l.Position.x * v.m[0][1] + l.Position.y * v.m[1][1] + l.Position.z * v.m[2][1] + v.m[3][1];
                p[2] = l.Position.x * v.m[0][2] + l.Position.y * v.m[1][2] + l.Position.z * v.m[2][2] + v.m[3][2];
                p[3] = static_cast<float>(l.Type);
                d[0] = l.Direction.x * v.m[0][0] + l.Direction.y * v.m[1][0] + l.Direction.z * v.m[2][0];
                d[1] = l.Direction.x * v.m[0][1] + l.Direction.y * v.m[1][1] + l.Direction.z * v.m[2][1];
                d[2] = l.Direction.x * v.m[0][2] + l.Direction.y * v.m[1][2] + l.Direction.z * v.m[2][2];
                const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (length > 0.0f)
                    d[0] /= length, d[1] /= length, d[2] /= length;
                d[3] = l.Range;
                // Effects: terms the fragment shader evaluates per pixel leave the vertex sum.
                if (!(fx.vertex_diffuse_off & (1u << light_count)))
                    memcpy(light_diffuse[light_count], &l.Diffuse, sizeof(float) * 4);
                memcpy(light_ambient[light_count], &l.Ambient, sizeof(float) * 4);
                if (!(fx.vertex_specular_off & (1u << light_count)))
                    memcpy(light_specular[light_count], &l.Specular, sizeof(float) * 4);
                light_att[light_count][0] = l.Attenuation0;
                light_att[light_count][1] = l.Attenuation1;
                light_att[light_count][2] = l.Attenuation2;
                ++light_count;
            }
            set_uniform(U_LIGHTPOS, &light_pos[0][0], kMaxLights);
            set_uniform(U_LIGHTDIR, &light_dir[0][0], kMaxLights);
            set_uniform(U_LIGHTDIFFUSE, &light_diffuse[0][0], kMaxLights);
            set_uniform(U_LIGHTAMBIENT, &light_ambient[0][0], kMaxLights);
            set_uniform(U_LIGHTATT, &light_att[0][0], kMaxLights);
            if (rs[D3DRS_SPECULARENABLE])
                set_uniform(U_LIGHTSPECULAR, &light_specular[0][0], kMaxLights);
            float material[4][4];
            memcpy(material[0], &s.material.Diffuse, sizeof(float) * 4);
            memcpy(material[1], &s.material.Ambient, sizeof(float) * 4);
            memcpy(material[2], &s.material.Specular, sizeof(float) * 4);
            memcpy(material[3], &s.material.Emissive, sizeof(float) * 4);
            set_uniform(U_MATERIAL, &material[0][0], 4);
            float ambient[4] = {};
            if (!fx.vertex_ambient_off)
                color_to_float(rs[D3DRS_AMBIENT], ambient);
            set_uniform(U_AMBIENT, ambient, 1);
        }
        const float vs[4][4] = {
            {layout.rhw ? 1.0f : 0.0f, lighting ? 1.0f : 0.0f, layout.has_color0 ? 1.0f : 0.0f,
             layout.has_color1 ? 1.0f : 0.0f},
            {rs[D3DRS_NORMALIZENORMALS] ? 1.0f : 0.0f, static_cast<float>(light_count),
             rs[D3DRS_RANGEFOGENABLE] ? 1.0f : 0.0f, layout.has_normal ? 1.0f : 0.0f},
            {material_source(rs[D3DRS_DIFFUSEMATERIALSOURCE], color_vertex, layout),
             material_source(rs[D3DRS_AMBIENTMATERIALSOURCE], color_vertex, layout),
             material_source(rs[D3DRS_EMISSIVEMATERIALSOURCE], color_vertex, layout),
             material_source(rs[D3DRS_SPECULARMATERIALSOURCE], color_vertex, layout)},
            {rs[D3DRS_SPECULARENABLE] ? 1.0f : 0.0f, s.material.Power, fog_from_specular ? 1.0f : 0.0f,
             as_float(rs[D3DRS_POINTSIZE]) * scale[1]},
        };
        set_uniform(U_VS, &vs[0][0], 4);

        float clip[4] = {};
        if (rs[D3DRS_CLIPPLANEENABLE] & 1)
        {
            // World-space plane -> view space: p_view = p_world * inverse-transpose(view).
            // The view matrix is rigid, so rotate the normal and re-derive the distance.
            const float *p = s.clip_plane[0];
            const D3DMATRIX &v = s.view;
            clip[0] = p[0] * v.m[0][0] + p[1] * v.m[1][0] + p[2] * v.m[2][0];
            clip[1] = p[0] * v.m[0][1] + p[1] * v.m[1][1] + p[2] * v.m[2][1];
            clip[2] = p[0] * v.m[0][2] + p[1] * v.m[1][2] + p[2] * v.m[2][2];
            clip[3] = p[3] - (clip[0] * v.m[3][0] + clip[1] * v.m[3][1] + clip[2] * v.m[3][2]);
        }
        set_uniform(U_CLIP, clip, 1);

        float tex_cfg[kFfStages][4] = {};
        float tex_mtx[kFfStages][16];
        bool any_matrix = false;
        for (uint32_t stage = 0; stage < kFfStages; ++stage)
        {
            const DWORD index = s.tss[stage][D3DTSS_TEXCOORDINDEX];
            const DWORD flags = s.tss[stage][D3DTSS_TEXTURETRANSFORMFLAGS];
            const uint32_t coord = std::min<uint32_t>(index & 0xFFFF, 3);
            const uint32_t texgen = (index >> 16) & 7;
            const uint32_t count = flags & 0xFF;
            tex_cfg[stage][0] = static_cast<float>(coord);
            tex_cfg[stage][1] = static_cast<float>(std::min(texgen, 3u));
            tex_cfg[stage][2] = layout.rhw ? 0.0f : static_cast<float>(count);
            tex_cfg[stage][3] = (flags & D3DTTFF_PROJECTED) && !layout.rhw ? 1.0f : 0.0f;
            D3DMATRIX m = s.texture[stage];
            if (count != 0 && texgen == 0)
            {
                // D3D pads short coordinates with 1 in the next component; the shader sees (u, v, 0, 1).
                const uint32_t size = layout.texcoord_size[coord] ? layout.texcoord_size[coord] : 2;
                if (size < 3)
                    for (int c = 0; c < 4; ++c)
                        m.m[3][c] = m.m[size][c];
            }
            memcpy(tex_mtx[stage], &m.m[0][0], sizeof(float) * 16);
            any_matrix |= count != 0;
        }
        set_uniform(U_TEXCFG, &tex_cfg[0][0], kFfStages);
        if (any_matrix)
            set_uniform(U_TEXMTX, &tex_mtx[0][0], kFfStages);
    }

    // fragment side
    float stage_data[kFfStages * 3][4] = {};
    uint32_t stage_count = 0;
    if (!ps_port)
    {
        for (uint32_t stage = 0; stage < kFfStages; ++stage)
        {
            const DWORD *t = s.tss[stage];
            if (t[D3DTSS_COLOROP] == D3DTOP_DISABLE)
                break;
            const bool bound = s.textures[stage] != nullptr;
            if (!bound && (uses_texture(t[D3DTSS_COLOROP], t[D3DTSS_COLORARG0], t[D3DTSS_COLORARG1], t[D3DTSS_COLORARG2]) ||
                           uses_texture(t[D3DTSS_ALPHAOP], t[D3DTSS_ALPHAARG0], t[D3DTSS_ALPHAARG1], t[D3DTSS_ALPHAARG2])))
                break;
            float *c = stage_data[stage * 3], *a = stage_data[stage * 3 + 1], *m = stage_data[stage * 3 + 2];
            c[0] = static_cast<float>(t[D3DTSS_COLOROP]);
            c[1] = encode_arg(t[D3DTSS_COLORARG1]);
            c[2] = encode_arg(t[D3DTSS_COLORARG2]);
            c[3] = encode_arg(t[D3DTSS_COLORARG0]);
            a[0] = static_cast<float>(t[D3DTSS_ALPHAOP]);
            a[1] = encode_arg(t[D3DTSS_ALPHAARG1]);
            a[2] = encode_arg(t[D3DTSS_ALPHAARG2]);
            a[3] = encode_arg(t[D3DTSS_ALPHAARG0]);
            m[0] = (t[D3DTSS_RESULTARG] & D3DTA_SELECTMASK) == D3DTA_TEMP ? 1.0f : 0.0f;
            m[1] = (t[D3DTSS_TEXTURETRANSFORMFLAGS] & D3DTTFF_PROJECTED) && !layout.rhw ? 1.0f : 0.0f;
            m[2] = bound ? 1.0f : 0.0f;
            stage_count = stage + 1;
        }
        set_uniform(U_STAGE, &stage_data[0][0], kFfStages * 3);
    }
    float fs[4][4] = {};
    color_to_float(rs[D3DRS_TEXTUREFACTOR], fs[0]);
    color_to_float(rs[D3DRS_FOGCOLOR], fs[1]);
    fs[1][3] = static_cast<float>(fog_mode);
    fs[3][3] = fog_from_clip_depth ? 1.0f : 0.0f;
    fs[2][0] = fog_mode == kFogAerial ? aerial_haze[0] : as_float(rs[D3DRS_FOGSTART]);
    fs[2][1] = fog_mode == kFogAerial ? aerial_haze[1] : as_float(rs[D3DRS_FOGEND]);
    fs[2][2] = as_float(rs[D3DRS_FOGDENSITY]);
    fs[2][3] = static_cast<float>(rs[D3DRS_ALPHAREF] & 255) / 255.0f;
    fs[3][0] = rs[D3DRS_ALPHATESTENABLE] && rs[D3DRS_ALPHAFUNC] != D3DCMP_ALWAYS ? static_cast<float>(rs[D3DRS_ALPHAFUNC])
                                                                               : 0.0f;
    fs[3][1] = rs[D3DRS_SPECULARENABLE] && !ps_port ? 1.0f : 0.0f;
    fs[3][2] = static_cast<float>(stage_count);
    set_uniform(U_FS, &fs[0][0], 4);
}

// ---------------------------------------------------------------------------
// render state
uint64_t Device::render_state_bits(D3DPRIMITIVETYPE type, bool *stencil, uint32_t *fstencil)
{
    const DWORD *rs = s.rs;
    uint64_t bits = 0;
    const DWORD mask = rs[D3DRS_COLORWRITEENABLE];
    if (mask & D3DCOLORWRITEENABLE_RED)
        bits |= BGFX_STATE_WRITE_R;
    if (mask & D3DCOLORWRITEENABLE_GREEN)
        bits |= BGFX_STATE_WRITE_G;
    if (mask & D3DCOLORWRITEENABLE_BLUE)
        bits |= BGFX_STATE_WRITE_B;
    if (mask & D3DCOLORWRITEENABLE_ALPHA)
        bits |= BGFX_STATE_WRITE_A;
    if (rs[D3DRS_ZENABLE] != D3DZB_FALSE && ds)
    {
        bits |= depth_func(rs[D3DRS_ZFUNC]);
        if (rs[D3DRS_ZWRITEENABLE])
            bits |= BGFX_STATE_WRITE_Z;
    }
    if (rs[D3DRS_ALPHABLENDENABLE])
    {
        if (rs[D3DRS_SEPARATEALPHABLENDENABLE])
            bits |= BGFX_STATE_BLEND_FUNC_SEPARATE(blend_factor(rs[D3DRS_SRCBLEND]), blend_factor(rs[D3DRS_DESTBLEND]),
                                                   blend_factor(rs[D3DRS_SRCBLENDALPHA]),
                                                   blend_factor(rs[D3DRS_DESTBLENDALPHA]));
        else
            bits |= BGFX_STATE_BLEND_FUNC(blend_factor(rs[D3DRS_SRCBLEND]), blend_factor(rs[D3DRS_DESTBLEND]));
        switch (rs[D3DRS_BLENDOP])
        {
        case D3DBLENDOP_SUBTRACT:
            bits |= BGFX_STATE_BLEND_EQUATION_SUB;
            break;
        case D3DBLENDOP_REVSUBTRACT:
            bits |= BGFX_STATE_BLEND_EQUATION_REVSUB;
            break;
        case D3DBLENDOP_MIN:
            bits |= BGFX_STATE_BLEND_EQUATION_MIN;
            break;
        case D3DBLENDOP_MAX:
            bits |= BGFX_STATE_BLEND_EQUATION_MAX;
            break;
        default:
            break;
        }
    }
    // Rendering upside down into GL render targets mirrors the winding.
    const bool swap_cull = Context::instance().flip_y();
    if (type >= D3DPT_TRIANGLELIST)
    {
        const DWORD cull = rs[D3DRS_CULLMODE];
        if (cull == D3DCULL_CW)
            bits |= swap_cull ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
        else if (cull == D3DCULL_CCW)
            bits |= swap_cull ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
    }
    switch (type)
    {
    case D3DPT_POINTLIST:
        bits |= BGFX_STATE_PT_POINTS;
        break;
    case D3DPT_LINELIST:
        bits |= BGFX_STATE_PT_LINES;
        break;
    case D3DPT_LINESTRIP:
        bits |= BGFX_STATE_PT_LINESTRIP;
        break;
    case D3DPT_TRIANGLESTRIP:
        bits |= BGFX_STATE_PT_TRISTRIP;
        break;
    default:
        break;
    }
    *stencil = rs[D3DRS_STENCILENABLE] != 0 && ds;
    if (*stencil)
        *fstencil = stencil_func(rs[D3DRS_STENCILFUNC]) | BGFX_STENCIL_FUNC_REF(rs[D3DRS_STENCILREF] & 255) |
                    BGFX_STENCIL_FUNC_RMASK(rs[D3DRS_STENCILMASK] & 255) |
                    stencil_op(rs[D3DRS_STENCILFAIL], BGFX_STENCIL_OP_FAIL_S_SHIFT) |
                    stencil_op(rs[D3DRS_STENCILZFAIL], BGFX_STENCIL_OP_FAIL_Z_SHIFT) |
                    stencil_op(rs[D3DRS_STENCILPASS], BGFX_STENCIL_OP_PASS_Z_SHIFT);
    return bits;
}

uint32_t Device::sampler_flags(uint32_t stage, const TexStore &store) const
{
    const DWORD *sm = s.sampler[stage];
    uint32_t flags = 0;
    const auto address = [](DWORD mode, uint32_t mirror, uint32_t clamp, uint32_t border) -> uint32_t {
        switch (mode)
        {
        case D3DTADDRESS_MIRROR:
            return mirror;
        case D3DTADDRESS_CLAMP:
            return clamp;
        case D3DTADDRESS_BORDER:
            return border;
        default:
            return 0;
        }
    };
    flags |= address(sm[D3DSAMP_ADDRESSU], BGFX_SAMPLER_U_MIRROR, BGFX_SAMPLER_U_CLAMP, BGFX_SAMPLER_U_BORDER);
    flags |= address(sm[D3DSAMP_ADDRESSV], BGFX_SAMPLER_V_MIRROR, BGFX_SAMPLER_V_CLAMP, BGFX_SAMPLER_V_BORDER);
    flags |= address(sm[D3DSAMP_ADDRESSW], BGFX_SAMPLER_W_MIRROR, BGFX_SAMPLER_W_CLAMP, BGFX_SAMPLER_W_BORDER);
    if (sm[D3DSAMP_ADDRESSU] == D3DTADDRESS_BORDER || sm[D3DSAMP_ADDRESSV] == D3DTADDRESS_BORDER ||
        sm[D3DSAMP_ADDRESSW] == D3DTADDRESS_BORDER)
    {
        if (!Context::instance().border_clamp())
        {
            // Without the extension the GPU has no border colour: bgfx clamps to the edge, said once here.
            static bool logged = false;
            if (!logged)
                logf("ERROR: D3DTADDRESS_BORDER sampled on a GPU without texture border clamp: clamped to the edge "
                     "(texels beyond the edge repeat the last row instead of the border colour)");
            logged = true;
        }
        else if (!Context::instance().border_colors())
        {
            // Vulkan without VK_EXT_custom_border_color has three border colours (D3DCOLOR ARGB): the game's own
            // (D3D's default, transparent black) is one of them.
            const DWORD color = sm[D3DSAMP_BORDERCOLOR];
            static bool logged = false;
            if (color != 0x00000000 && color != 0xff000000 && color != 0xffffffff && !logged)
            {
                logf("ERROR: D3DSAMP_BORDERCOLOR 0x%08x sampled on a Vulkan device without custom border colours: "
                     "opaque black is sampled instead",
                     static_cast<unsigned>(color));
                logged = true;
            }
        }
        flags |= BGFX_SAMPLER_BORDER_COLOR(border_color_index(sm[D3DSAMP_BORDERCOLOR]));
    }
    if (sm[D3DSAMP_MINFILTER] == D3DTEXF_ANISOTROPIC)
        flags |= BGFX_SAMPLER_MIN_ANISOTROPIC;
    else if (sm[D3DSAMP_MINFILTER] <= D3DTEXF_POINT)
        flags |= BGFX_SAMPLER_MIN_POINT;
    if (sm[D3DSAMP_MAGFILTER] == D3DTEXF_ANISOTROPIC)
        flags |= BGFX_SAMPLER_MAG_ANISOTROPIC;
    else if (sm[D3DSAMP_MAGFILTER] <= D3DTEXF_POINT)
        flags |= BGFX_SAMPLER_MAG_POINT;
    if (sm[D3DSAMP_MIPFILTER] <= D3DTEXF_POINT || (store.levels <= 1 && !store.full_chain))
        flags |= BGFX_SAMPLER_MIP_POINT;
    // Effects: anisotropic filtering of linearly filtered, mip-mapped world textures (render targets and mip-less
    // interface pictures and fonts keep their filter).
    else if (world_anisotropy && store.kind == TexKind::Tex2D && !store.render_target && !(flags & BGFX_SAMPLER_MIN_POINT))
        flags = (flags & ~(BGFX_SAMPLER_MIN_MASK | BGFX_SAMPLER_MAG_MASK)) | BGFX_SAMPLER_MIN_ANISOTROPIC |
                BGFX_SAMPLER_MAG_ANISOTROPIC;
    return flags;
}

// bgfx samples a border through a palette entry: one entry per distinct D3DSAMP_BORDERCOLOR, kept for the device's
// life (the game uses the default transparent black). Palette entries are bgfx-global; the shim is their only user.
uint32_t Device::border_color_index(DWORD color) const
{
    size_t index = 0;
    while (index < border_colors.size() && border_colors[index] != color)
        ++index;
    constexpr size_t kPaletteEntries = 16; // BGFX_CONFIG_MAX_COLOR_PALETTE (bgfx/src/config.h)
    if (index == border_colors.size())
    {
        if (index >= kPaletteEntries)
        {
            trap("more distinct D3DSAMP_BORDERCOLOR values than bgfx palette entries (first entry used)");
            index = 0;
        }
        else
            border_colors.push_back(color);
    }
    // Set with every use (a few sail draws): the entry survives a bgfx re-initialisation. D3DCOLOR is ARGB, the
    // palette takes RGBA.
    const DWORD c = border_colors[index];
    bgfx::setPaletteColor(static_cast<uint8_t>(index), (c << 8) | (c >> 24));
    return static_cast<uint32_t>(index);
}

// ---------------------------------------------------------------------------
// programmable shaders
namespace
{
// CTechnique passes a reference blob (see ports.hpp) instead of D3D9 bytecode; returns its key
// (empty for anything else) and the blob length in DWORDs including the 0x0000FFFF end token.
std::string_view shader_key(const DWORD *function, size_t *words)
{
    size_t count = 0;
    while (function[count] != 0x0000FFFF && count < 65536)
        ++count;
    *words = count + 1;
    if (count == 0 || function[0] != kShaderRefMagic)
        return {};
    std::string_view key(reinterpret_cast<const char *>(function + 1), (count - 1) * sizeof(DWORD));
    return key.substr(0, key.find('\0'));
}
} // namespace

HRESULT Device::CreateVertexShader(const DWORD *function, VertexShader **shader)
{
    auto *result = new VertexShader();
    size_t words = 0;
    const std::string_view key = shader_key(function, &words);
    result->bytecode.assign(function, function + words);
    result->port = find_port(key, true);
    if (!result->port)
        logf("vertex shader '%.*s' has no bgfx port; its draws are skipped", static_cast<int>(key.size()), key.data());
    *shader = result;
    return D3D_OK;
}

HRESULT Device::CreatePixelShader(const DWORD *function, PixelShader **shader)
{
    auto *result = new PixelShader();
    size_t words = 0;
    const std::string_view key = shader_key(function, &words);
    result->bytecode.assign(function, function + words);
    result->port = find_port(key, false);
    if (!result->port)
        logf("pixel shader '%.*s' has no bgfx port; its draws are skipped", static_cast<int>(key.size()), key.data());
    *shader = result;
    return D3D_OK;
}

HRESULT Device::SetVertexShader(VertexShader *shader)
{
    assign_ref(s.vs, static_cast<VertexShader *>(shader));
    return D3D_OK;
}
HRESULT Device::GetVertexShader(VertexShader **shader)
{
    *shader = s.vs;
    if (s.vs)
        s.vs->AddRef();
    return D3D_OK;
}
HRESULT Device::SetPixelShader(PixelShader *shader)
{
    assign_ref(s.ps, static_cast<PixelShader *>(shader));
    return D3D_OK;
}
HRESULT Device::GetPixelShader(PixelShader **shader)
{
    *shader = s.ps;
    if (s.ps)
        s.ps->AddRef();
    return D3D_OK;
}

bool Device::prepare_program(uint32_t vs_id, uint32_t fs_id, int &budget)
{
    if (!programs.contains({vs_id, fs_id}))
    {
        if (budget <= 0)
            return false;
        program(vs_id, fs_id); // logs a failure
        --budget;
    }
    // The draws of the same programs without clip plane and alpha test use the opaque twin (draw()).
    if (const uint32_t twin = opaque_twin(fs_id); twin != UINT32_MAX && !programs.contains({vs_id, twin}))
        return prepare_program(vs_id, twin, budget);
    return true;
}

bgfx::ProgramHandle Device::program(uint32_t vs_id, uint32_t fs_id)
{
    const auto key = std::make_pair(vs_id, fs_id);
    const auto it = programs.find(key);
    if (it != programs.end())
        return it->second;
    bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;
    if (vs_id < shaders.size() && fs_id < shaders.size())
    {
        for (const uint32_t id : {vs_id, fs_id})
            if (!bgfx::isValid(shaders[id]))
                shaders[id] = create_embedded_shader(id);
        handle = bgfx::createProgram(shaders[vs_id], shaders[fs_id], false);
    }
    if (!bgfx::isValid(handle))
        logf("program creation failed (vs=%u fs=%u)", vs_id, fs_id);
    programs[key] = handle;
    return handle;
}

// ---------------------------------------------------------------------------
// draws
bool Device::draw_bounds(const DrawCall &call, const LayoutInfo &layout, uint32_t element_count, float center[3],
                         float &radius)
{
    const bgfx::VertexLayout &vl = layout.layout[0];
    if (layout.rhw || !vl.has(bgfx::Attrib::Position))
        return false;
    // The vertex range the draw reaches: D3D's MinVertexIndex / NumVertices for indexed draws.
    uint32_t first = call.indexed ? static_cast<uint32_t>(call.base_vertex) + call.min_index : call.start_vertex;
    uint32_t count = call.indexed ? call.index_vertex_count : element_count;
    const uint8_t *data;
    size_t available;
    VertexBuffer *vb = nullptr;
    if (call.up_vertices)
    {
        data = call.up_vertices;
        if (call.indexed)
            first = call.min_index;
        available = (static_cast<size_t>(first) + count) * call.up_stride;
    }
    else
    {
        vb = s.stream[0];
        if (!vb || s.stream_offset[0] > vb->shadow.size())
            return false;
        data = vb->shadow.data() + s.stream_offset[0];
        available = vb->shadow.size() - s.stream_offset[0];
    }
    const uint32_t stride = vl.getStride();
    if (count == 0 || stride == 0 || (static_cast<size_t>(first) + count) * stride > available)
        return false;
    if (vb)
    {
        if (vb->bounds_serial != vb->write_serial)
        {
            vb->bounds.clear();
            vb->bounds_serial = vb->write_serial;
        }
        for (const VertexBuffer::RangeBounds &b : vb->bounds)
            if (b.first == first && b.count == count && b.stride == stride && b.offset == s.stream_offset[0])
            {
                memcpy(center, b.center, sizeof(float) * 3);
                radius = b.radius;
                return true;
            }
    }
    // Box centre, then the farthest vertex from it. Float positions (all the game's layouts) are read in place;
    // bgfx::vertexUnpack (format conversion per vertex) only for the others.
    float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    uint8_t components = 0;
    bgfx::AttribType::Enum type = bgfx::AttribType::Float;
    bool normalized = false, as_int = false;
    vl.decode(bgfx::Attrib::Position, components, type, normalized, as_int);
    const bool floats = type == bgfx::AttribType::Float && components >= 3;
    const uint8_t *position = data + vl.getOffset(bgfx::Attrib::Position) + static_cast<size_t>(first) * stride;
    const auto read = [&](uint32_t i, float p[4]) {
        if (floats)
            memcpy(p, position + static_cast<size_t>(i) * stride, sizeof(float) * 3);
        else
            bgfx::vertexUnpack(p, bgfx::Attrib::Position, vl, data, first + i);
    };
    float p[4];
    for (uint32_t i = 0; i < count; ++i)
    {
        read(i, p);
        for (int k = 0; k < 3; ++k)
        {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
    }
    float r2 = 0.0f;
    for (int k = 0; k < 3; ++k)
        center[k] = 0.5f * (lo[k] + hi[k]);
    for (uint32_t i = 0; i < count; ++i)
    {
        read(i, p);
        const float dx = p[0] - center[0], dy = p[1] - center[1], dz = p[2] - center[2];
        r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
    }
    radius = std::sqrt(r2);
    if (vb && vb->bounds.size() < 256) // static models draw a handful of subsets; dynamic buffers are rewritten
        vb->bounds.push_back({first, count, stride, s.stream_offset[0], {center[0], center[1], center[2]}, radius});
    return true;
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE type, UINT start_vertex, UINT primitive_count)
{
    DrawCall call;
    call.type = type;
    call.primitive_count = primitive_count;
    call.start_vertex = start_vertex;
    return draw(call);
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base_vertex, UINT min_index, UINT vertex_count,
                                     UINT start_index, UINT primitive_count)
{
    DrawCall call;
    call.type = type;
    call.primitive_count = primitive_count;
    call.indexed = true;
    call.base_vertex = base_vertex;
    call.min_index = min_index;
    call.index_vertex_count = vertex_count;
    call.start_index = start_index;
    return draw(call);
}

HRESULT Device::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitive_count, const void *data, UINT stride)
{
    DrawCall call;
    call.type = type;
    call.primitive_count = primitive_count;
    call.up_vertices = static_cast<const uint8_t *>(data);
    call.up_stride = stride;
    return draw(call);
}

HRESULT Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min_index, UINT vertex_count, UINT primitive_count,
                                       const void *index_data, D3DFORMAT index_format, const void *data, UINT stride)
{
    DrawCall call;
    call.type = type;
    call.primitive_count = primitive_count;
    call.indexed = true;
    call.up_vertices = static_cast<const uint8_t *>(data);
    call.up_stride = stride;
    call.up_indices = index_data;
    call.up_index32 = index_format == D3DFMT_INDEX32;
    call.up_vertex_count = min_index + vertex_count;
    call.min_index = min_index;
    call.index_vertex_count = vertex_count;
    return draw(call);
}

namespace
{
// The FF_SKIN variant of a fixed-function vertex shader (GEOS::AVERTEX0 vertices), UINT32_MAX if none.
uint32_t skinned_variant(uint32_t vs_id)
{
    static const std::pair<uint32_t, uint32_t> table[] = {
        {shader_id_by_name("vs_ff"), shader_id_by_name("vs_ff_skin")},
        {shader_id_by_name("vs_ff_unlit"), shader_id_by_name("vs_ff_unlit_skin")},
        {shader_id_by_name("vs_ff_lit"), shader_id_by_name("vs_ff_lit_skin")}, // effects: per-pixel lighting
    };
    for (const auto &[plain, skinned] : table)
        if (plain == vs_id)
            return skinned;
    return UINT32_MAX;
}
} // namespace

// A draw that cannot be issued is never dropped quietly: it is counted and its reason logged once.
void Device::skip_draw(const char *reason)
{
    ++perf().skipped_draws;
    bgfx::discard(); // buffers or textures may already be bound for this draw
    trap(reason);
}

HRESULT Device::draw(const DrawCall &call)
{
    if (call.primitive_count == 0)
        return D3D_OK;
    PerfScope timing(perf().shim_ms);
    Context::instance().poll_surface();
    if (!Context::instance().ready())
        return D3D_OK;
    // Before any state of this draw is set: clearing uses views of its own.
    for (uint32_t stage = 0; stage < kFfStages; ++stage)
        if (TexStore *store = store_of(s.textures[stage]); store && store->needs_clear)
        {
            store->sync();
            clear_if_new(store);
            view_dirty = true; // this draw goes into a view after the clear
        }
    const uint32_t element_count = vertex_count_for(call.type, call.primitive_count);
    if (element_count == 0)
        return D3DERR_INVALIDCALL;

    const LayoutInfo *layout = nullptr;
    if (call.up_vertices)
    {
        if (s.decl)
        {
            // User-pointer draws supply stream 0 only.
            const UINT saved = s.stream_stride[0];
            s.stream_stride[0] = call.up_stride;
            layout = build_decl_layout(*s.decl);
            s.stream_stride[0] = saved;
        }
        else
            layout = fvf_layout(s.fvf, call.up_stride);
    }
    else
        layout = current_layout();
    if (!layout || layout->stream_count == 0)
    {
        skip_draw("no vertex layout");
        return D3D_OK;
    }
    // Effects: a perspective draw into the game picture makes this frame a 3D scene (Device::scene_complete).
    if (rt == back_color && !layout->rhw && s.proj.m[2][3] != 0.0f)
    {
        scene_drawn = true;
        scene_view = s.view;
        scene_proj = s.proj;
    }

    // ---- program ----
    const bool vs_port = s.vs != nullptr, ps_port = s.ps != nullptr;
    if ((vs_port && !s.vs->port) || (ps_port && !s.ps->port))
    {
        skip_draw("shader without bgfx port");
        return D3D_OK;
    }
    // [0] vertex lighting (D3DRS_LIGHTING), [1] unlit: the lights stay out of the program (ff_vertex.sh FF_UNLIT).
    static const uint32_t id_vs_ff[2] = {shader_id_by_name("vs_ff"), shader_id_by_name("vs_ff_unlit")};
    static const uint32_t id_fs_ff[5] = {shader_id_by_name("fs_ff"), shader_id_by_name("fs_ff_cube0"),
                                         shader_id_by_name("fs_ff_cube1"), shader_id_by_name("fs_ff_cube2"),
                                         shader_id_by_name("fs_ff_slopebias")};
    // A texture left bound on a stage after the first disabled one is not sampled (as in D3D9 and the
    // texture binding below), so it does not select a cube variant.
    uint32_t fs_variant = 0;
    for (uint32_t stage = 0; stage < 3 && !ps_port && s.tss[stage][D3DTSS_COLOROP] != D3DTOP_DISABLE; ++stage)
        if (const TexStore *store = store_of(s.textures[stage]); store && store->kind == TexKind::Cube)
        {
            fs_variant = stage + 1;
            break;
        }
    const float slope_bias = as_float(s.rs[D3DRS_SLOPESCALEDEPTHBIAS]);
    const bool port_bias = ps_port && s.ps->port->slope_bias;
    if (slope_bias != 0.0f || port_bias)
    {
        if (ps_port ? !port_bias : fs_variant != 0)
            trap("slope-scale depth bias with this pixel shader or a cube stage is not implemented (no bias applied)");
        else
        {
            if (!ps_port)
                fs_variant = 4;
            const float bias[4] = {slope_bias, 0.0f, 0.0f, 0.0f};
            set_uniform(U_DEPTHBIAS, bias, 1);
        }
    }
    // Effects: per-pixel lighting / alpha to coverage variant of a fixed-function draw (effects/lighting.hpp).
    // Slope-biased draws (projected shadows: unlit decals) keep the depth-writing fixed-function variant.
    effects::Lighting &fx_lighting = effects::Host::instance().lighting();
    effects::FfDraw fx_draw;
    if (!vs_port && !ps_port && fs_variant != 4 && !fx_lighting.idle())
    {
        // The lights are chosen for the part of the world the draw covers (its vertices' sphere in world space),
        // computed only when prepare() needs it (effects::DrawSphereSource).
        struct SphereContext
        {
            Device *device;
            const DrawCall *call;
            const LayoutInfo *layout;
            uint32_t element_count;
        } sphere_context{this, &call, layout, element_count};
        const effects::DrawSphereSource sphere(
            [](const void *context) {
                const auto &c = *static_cast<const SphereContext *>(context);
                effects::DrawSphere result;
                float center[3], radius = 0.0f;
                if (c.device->draw_bounds(*c.call, *c.layout, c.element_count, center, radius))
                {
                    const D3DMATRIX &w = c.device->s.world;
                    for (int k = 0; k < 3; ++k)
                        result.center[k] = center[0] * w.m[0][k] + center[1] * w.m[1][k] + center[2] * w.m[2][k] + w.m[3][k];
                    float scale = 0.0f;
                    for (int r = 0; r < 3; ++r)
                        scale = std::max(scale, std::sqrt(w.m[r][0] * w.m[r][0] + w.m[r][1] * w.m[r][1] + w.m[r][2] * w.m[r][2]));
                    result.radius = radius * scale;
                    result.valid = true;
                }
                return result;
            },
            &sphere_context);
        fx_lighting.prepare(*this, *layout, fs_variant, sphere, fx_draw);
    }
    if (layout->skinned && (vs_port || !store_of(s.vertex_texture)))
    {
        skip_draw(vs_port ? "skinned vertices with a vertex shader port"
                          : "skinned draw without a bone palette in D3DVERTEXTEXTURESAMPLER0");
        return D3D_OK;
    }
    // Without lighting the unlit variant leaves the lights out of the program (see ff_vertex.sh).
    const bool lit = s.rs[D3DRS_LIGHTING] != 0 && !layout->rhw;
    uint32_t vs_id = vs_port ? s.vs->port->shader_id() : fx_draw.active ? fx_draw.vs_id : id_vs_ff[lit ? 0 : 1];
    if (layout->skinned)
    {
        vs_id = skinned_variant(vs_id);
        if (vs_id == UINT32_MAX)
        {
            skip_draw("skinned draw with a vertex shader that has no skinned variant");
            return D3D_OK;
        }
    }
    uint32_t fs_id = ps_port ? s.ps->port->shader_id() : fx_draw.active ? fx_draw.fs_id : id_fs_ff[fs_variant];
    // Nothing of this draw can be discarded (no user clip plane, no alpha test): its shader's opaque twin, which
    // keeps early depth and the tile GPUs' hidden-surface removal (ff_finish.sh FF_OPAQUE).
    if (!ps_port && !(s.rs[D3DRS_CLIPPLANEENABLE] & 1) &&
        !(s.rs[D3DRS_ALPHATESTENABLE] && s.rs[D3DRS_ALPHAFUNC] != D3DCMP_ALWAYS))
        if (const uint32_t twin = opaque_twin(fs_id); twin != UINT32_MAX)
            fs_id = twin;
    const bgfx::ProgramHandle handle = program(vs_id, fs_id);
    if (!bgfx::isValid(handle))
    {
        skip_draw("program creation failed");
        return D3D_OK;
    }

    // Effects: the Sea2 pixel port's improvements; its soft edges may capture the scene depth in a view of their own
    // first, so this comes before the draw's view.
    if (ps_port && s.ps->port->sea_fx)
        effects::Host::instance().prepare_sea(*this);
    const uint16_t view_id = ensure_view();
    const uint64_t frame = Context::instance().frame_index();
    // ---- index source ----
    const uint8_t *index_source = nullptr;
    bool index32 = false;
    IndexBuffer *ib = nullptr;
    if (call.indexed)
    {
        if (call.up_indices)
        {
            index_source = static_cast<const uint8_t *>(call.up_indices);
            index32 = call.up_index32;
        }
        else
        {
            ib = s.indices;
            if (!ib)
            {
                skip_draw("no index buffer bound");
                return D3D_OK;
            }
            index32 = ib->index32;
            const size_t offset = static_cast<size_t>(call.start_index) * (index32 ? 4 : 2);
            if (offset + static_cast<size_t>(element_count) * (index32 ? 4 : 2) > ib->shadow.size())
            {
                skip_draw("index range outside the buffer");
                return D3D_OK;
            }
            index_source = ib->shadow.data() + offset;
        }
    }
    const auto index_at = [&](uint32_t i) -> uint32_t {
        return index32 ? reinterpret_cast<const uint32_t *>(index_source)[i]
                       : reinterpret_cast<const uint16_t *>(index_source)[i];
    };

    // ---- vertex streams ----
    bool transient_vertices = call.up_vertices != nullptr;
    if (!call.up_vertices)
        for (uint32_t i = 0; i < layout->stream_count; ++i)
        {
            VertexBuffer *vb = s.stream[i];
            if (!vb)
            {
                skip_draw("no vertex buffer bound");
                return D3D_OK;
            }
            if (vb->write_frame == frame || (s.stream_offset[i] % layout->stride[i]) != 0)
                transient_vertices = true;
        }

    uint32_t index_bias = 0;         // subtracted from every index
    uint32_t first_vertex = 0;       // first vertex of the range, relative to the stream start
    uint32_t vertex_count = element_count;
    if (call.indexed)
    {
        if (transient_vertices)
        {
            uint32_t lo = UINT32_MAX, hi = 0;
            for (uint32_t i = 0; i < element_count; ++i)
            {
                const uint32_t v = index_at(i);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            index_bias = lo;
            first_vertex = static_cast<uint32_t>(call.base_vertex) + lo;
            vertex_count = hi - lo + 1;
        }
        else
            first_vertex = static_cast<uint32_t>(call.base_vertex);
    }
    else
        first_vertex = call.start_vertex;

    if (transient_vertices)
    {
        for (uint32_t i = 0; i < layout->stream_count; ++i)
        {
            const uint32_t stride = layout->stride[i];
            const uint8_t *source;
            size_t available;
            if (call.up_vertices)
            {
                source = call.up_vertices;
                available = SIZE_MAX;
            }
            else
            {
                VertexBuffer *vb = s.stream[i];
                source = vb->shadow.data() + s.stream_offset[i];
                available = vb->shadow.size() - std::min<size_t>(vb->shadow.size(), s.stream_offset[i]);
            }
            const size_t begin = static_cast<size_t>(first_vertex) * stride;
            const size_t bytes = static_cast<size_t>(vertex_count) * stride;
            if (begin + bytes > available)
            {
                // The caller's indices or vertex range reach past the end of its vertex buffer.
                skip_draw("vertex range outside the vertex buffer");
                return D3D_OK;
            }
            const VertexBuffer *vb = call.up_vertices ? nullptr : s.stream[i];
            if (vb)
            {
                // The same range (passes over one dynamic buffer, several subsets) is copied once per frame.
                const size_t shadow_begin = s.stream_offset[i] + begin;
                const uint32_t bgfx_frame = Context::instance().submitted_frames();
                const auto reuse = std::find_if(transient_ranges.begin(), transient_ranges.end(), [&](const TransientRange &r) {
                    return r.bgfx_frame == bgfx_frame && r.vb == vb && r.serial == vb->write_serial && r.layout == layout->handle[i].idx &&
                           r.stride == stride && r.begin <= shadow_begin && shadow_begin + bytes <= r.begin + r.bytes &&
                           (shadow_begin - r.begin) % stride == 0;
                });
                if (reuse != transient_ranges.end())
                {
                    bgfx::setVertexBuffer(static_cast<uint8_t>(i), &reuse->tvb,
                                          static_cast<uint32_t>((shadow_begin - reuse->begin) / stride), vertex_count);
                    continue;
                }
            }
            if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout->layout[i]) < vertex_count)
            {
                skip_draw("transient vertex budget exhausted");
                return D3D_OK;
            }
            bgfx::TransientVertexBuffer tvb;
            bgfx::allocTransientVertexBuffer(&tvb, vertex_count, layout->layout[i]);
            memcpy(tvb.data, source + begin, bytes);
            perf().transient_vb_bytes += bytes;
            if (vb)
                transient_ranges.push_back({Context::instance().submitted_frames(), vb, vb->write_serial, layout->handle[i].idx, stride,
                                            s.stream_offset[i] + begin, bytes, tvb});
            bgfx::setVertexBuffer(static_cast<uint8_t>(i), &tvb, 0, vertex_count);
        }
    }
    else
    {
        for (uint32_t i = 0; i < layout->stream_count; ++i)
        {
            VertexBuffer *vb = s.stream[i];
            upload_buffer(*vb, layout->layout[i]);
            const uint32_t start = s.stream_offset[i] / layout->stride[i] + first_vertex;
            const uint32_t count = call.indexed ? UINT32_MAX : vertex_count;
            bgfx::setVertexBuffer(static_cast<uint8_t>(i), vb->handle, start, count, layout->handle[i]);
        }
    }

    // ---- indices ----
    const bool fan = call.type == D3DPT_TRIANGLEFAN;
    const bool transient_indices =
        fan || (call.indexed && (call.up_indices || index_bias != 0 || (ib && ib->write_frame == frame)));
    if (transient_indices)
    {
        const uint32_t out_count = fan ? call.primitive_count * 3 : element_count;
        const bool out32 = call.indexed ? index32 : vertex_count > 65535;
        if (bgfx::getAvailTransientIndexBuffer(out_count, out32) < out_count)
        {
            skip_draw("transient index budget exhausted");
            return D3D_OK;
        }
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientIndexBuffer(&tib, out_count, out32);
        const auto put = [&](uint32_t i, uint32_t value) {
            if (out32)
                reinterpret_cast<uint32_t *>(tib.data)[i] = value;
            else
                reinterpret_cast<uint16_t *>(tib.data)[i] = static_cast<uint16_t>(value);
        };
        const auto source = [&](uint32_t i) -> uint32_t { return call.indexed ? index_at(i) - index_bias : i; };
        if (fan)
        {
            for (uint32_t i = 0; i < call.primitive_count; ++i)
            {
                put(i * 3, source(0));
                put(i * 3 + 1, source(i + 1));
                put(i * 3 + 2, source(i + 2));
            }
        }
        else if (call.indexed && index_bias == 0 && out32 == index32)
            memcpy(tib.data, index_source, static_cast<size_t>(out_count) * (out32 ? 4 : 2)); // indices written this frame
        else
            for (uint32_t i = 0; i < out_count; ++i)
                put(i, source(i));
        bgfx::setIndexBuffer(&tib, 0, out_count);
        perf().transient_ib_bytes += static_cast<uint64_t>(out_count) * (out32 ? 4 : 2);
        perf().fan_draws += fan ? 1 : 0;
    }
    else if (call.indexed)
    {
        upload_buffer(*ib, index32);
        bgfx::setIndexBuffer(ib->handle, call.start_index, element_count);
    }

    // ---- textures ----
    const uint32_t stage_limit = ps_port ? s.ps->port->sampler_count : kFfStages;
    for (uint32_t stage = 0; stage < stage_limit; ++stage)
    {
        TexStore *store = store_of(s.textures[stage]);
        if (!store)
            continue;
        if (!ps_port && s.tss[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE)
            break;
        store->sync();
        if (!bgfx::isValid(store->handle))
            continue;
        if (store->render_target && store->levels > 1 && (std::max(store->width, store->height) >> store->levels) != 0)
            // bgfx allocates the full chain for render targets; the levels below the D3D ones are never written.
            trap("sampling a mipped render target with a partial mip chain (lower levels undefined)");
        uint32_t unit = stage;
        if (store->kind != TexKind::Tex2D)
        {
            // cube: stages 0..2 -> units 5..7, stage 3 -> 11; volume: stages 0..2 -> 8..10, stage 4 -> 12
            const bool cube = store->kind == TexKind::Cube;
            if (stage < 3)
                unit = (cube ? 5 : 8) + stage;
            else if (cube && stage == 3)
                unit = 11;
            else if (!cube && stage == 4)
                unit = 12;
            else
                continue;
        }
        const uint32_t flags = sampler_flags(stage, *store);
        bgfx::TextureHandle handle = store->handle;
        if (store->lightmap) // a town's lightmap: ours for the hour's sun in its place (effects/town_light.hpp)
            if (const bgfx::TextureHandle ours = fx_lighting.town_light().replacement(store); bgfx::isValid(ours))
                handle = ours;
        bgfx::setTexture(static_cast<uint8_t>(unit), Context::instance().sampler(unit), handle, flags);
        ++perf().texture_binds;
        if (last_draw.texture[unit] != handle.idx || last_draw.sampler[unit] != flags)
        {
            ++perf().texture_changes;
            last_draw.texture[unit] = handle.idx;
            last_draw.sampler[unit] = flags;
        }
    }

    if (layout->skinned)
    {
        // The frame's palettes share one texture (bone_palettes.hpp): this palette's rows at an offset.
        TexStore *palette = store_of(s.vertex_texture);
        BonePalettes &palettes = Context::instance().bone_palettes();
        const BonePalettes::Slot slot = palettes.place(*palette);
        bgfx::TextureHandle texture = palettes.texture();
        if (!slot.valid())
        {
            // The frame's atlas is full (logged once): the palette's own texture, uploaded as before.
            palette->any_dirty = palette->sub(0, 0).dirty = true;
            palette->sync();
            texture = palette->handle;
        }
        // The first row and the column as instance data of one instance (ff_skin.sh i_data0): a uniform here
        // re-committed the program's whole uniform block for every skinned draw (bgfx GL commits all of a program's
        // uniforms on a change). Kept for the casters submitted after this draw (BGFX_DISCARD_NONE).
        if (bgfx::getAvailInstanceDataBuffer(1, 16) < 1)
        {
            skip_draw("transient budget exhausted for a skinned draw's palette row");
            return D3D_OK;
        }
        bgfx::InstanceDataBuffer instance;
        bgfx::allocInstanceDataBuffer(&instance, 1, 16);
        const float base[4] = {slot.valid() ? static_cast<float>(slot.row) : 0.0f,
                               slot.valid() ? static_cast<float>(slot.column * BonePalettes::kTexelsPerBone) : 0.0f, 0.0f,
                               0.0f};
        memcpy(instance.data, base, sizeof(base));
        bgfx::setInstanceDataBuffer(&instance);
        bgfx::setTexture(static_cast<uint8_t>(kBonePaletteUnit), Context::instance().sampler(kBonePaletteUnit), texture,
                         BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
        ++perf().texture_binds;
    }

    apply_ff_uniforms(*layout, vs_port, ps_port, fx_draw);
    if (fx_draw.active)
        fx_lighting.submit(fx_draw);

    bool stencil = false;
    uint32_t fstencil = 0;
    D3DPRIMITIVETYPE effective = fan ? D3DPT_TRIANGLELIST : call.type;
    const uint64_t bits = render_state_bits(effective, &stencil, &fstencil) | fx_draw.state;
    uint32_t factor = 0;
    if (s.rs[D3DRS_ALPHABLENDENABLE])
    {
        const DWORD c = s.rs[D3DRS_BLENDFACTOR];
        factor = ((c & 0x00FFFFFF) << 8) | (c >> 24);
    }
    bgfx::setState(bits, factor);
    if (stencil)
        bgfx::setStencil(fstencil);
    if (s.rs[D3DRS_SCISSORTESTENABLE])
    {
        // In the pixels the game addresses, like the viewport (new_view).
        const uint32_t target_height = rt->store->level_height(rt->level);
        const std::array<float, 2> scale = target_scale();
        const auto px = [](LONG v, float k) { return static_cast<int32_t>(std::lround(static_cast<float>(v) * k)); };
        const int32_t left = px(s.scissor.left, scale[0]), right = px(s.scissor.right, scale[0]);
        const int32_t top = px(s.scissor.top, scale[1]), bottom = px(s.scissor.bottom, scale[1]);
        const int32_t w = right - left, h = bottom - top;
        const int32_t y = Context::instance().flip_y() ? static_cast<int32_t>(target_height) - bottom : top;
        bgfx::setScissor(static_cast<uint16_t>(std::max<int32_t>(0, left)), static_cast<uint16_t>(std::max(0, y)),
                         static_cast<uint16_t>(std::max(0, w)), static_cast<uint16_t>(std::max(0, h)));
    }
    // Effects: a sun shadow caster keeps its buffers for the depth-only copy into the shadow view (effects/shadows.hpp).
    bgfx::submit(view_id, handle, 0, fx_draw.caster ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
    if (fx_draw.caster)
        fx_lighting.submit_caster(*this, s.world, layout->skinned);
    view_has_content = true;
    render::PerfCounters &counters = perf();
    ++counters.draws;
    const bool program_changed = last_draw.program != handle.idx;
    if (program_changed)
        ++counters.program_changes, last_draw.program = handle.idx;
    const uint32_t stencil_bits = stencil ? fstencil : 0;
    if (last_draw.state != bits || last_draw.stencil != stencil_bits || last_draw.factor != factor)
    {
        ++counters.state_changes;
        last_draw.state = bits, last_draw.stencil = stencil_bits, last_draw.factor = factor;
    }
    counters.uniform_draws += uniforms_changed ? 1 : 0;
    uniforms_changed = false;
    return D3D_OK;
}

} // namespace storm::shim
