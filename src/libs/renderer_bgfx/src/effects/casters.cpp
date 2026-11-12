#include "effects/casters.hpp"

#include "effects/texture_pixels.hpp"
#include "shim/device.hpp"

namespace storm::effects
{

CasterMaterial caster_material(const shim::Device &device, const shim::LayoutInfo &layout)
{
    CasterMaterial material;
    const DWORD *rs = device.s.rs;
    if (rs[D3DRS_ALPHABLENDENABLE] && !rs[D3DRS_ZWRITEENABLE])
    {
        material.casts = false;
        return material;
    }
    const DWORD func = rs[D3DRS_ALPHAFUNC];
    if (!rs[D3DRS_ALPHATESTENABLE] || (func != D3DCMP_GREATER && func != D3DCMP_GREATEREQUAL))
        return material;
    // The alpha chain of the fixed-function stages: which textures' alphas the tested alpha is a product of.
    std::array<float, 3> stage{};
    std::array<float, 3> uv{};
    bool any = false;
    for (int i = 0; i < 3; ++i)
    {
        const DWORD *tss = device.s.tss[i];
        if (tss[D3DTSS_COLOROP] == D3DTOP_DISABLE)
            break;
        const DWORD op = tss[D3DTSS_ALPHAOP];
        if (op == D3DTOP_DISABLE)
            break;
        const DWORD a1 = tss[D3DTSS_ALPHAARG1] & D3DTA_SELECTMASK, a2 = tss[D3DTSS_ALPHAARG2] & D3DTA_SELECTMASK;
        const shim::TexStore *store = shim::store_of(device.s.textures[i]);
        const DWORD set = tss[D3DTSS_TEXCOORDINDEX] & 0xffffu;
        const bool cuts = store && store->kind == shim::TexKind::Tex2D && store->cutout && set < 3 && set < layout.texcoord_count;
        const auto takes = [&](DWORD arg) { return arg == D3DTA_TEXTURE; };
        const auto keeps = [&](DWORD arg) { return arg == D3DTA_CURRENT && i > 0; };
        const DWORD selected = op == D3DTOP_SELECTARG1 ? a1 : op == D3DTOP_SELECTARG2 ? a2 : D3DTA_SELECTMASK + 1;
        const bool modulate = op == D3DTOP_MODULATE || op == D3DTOP_MODULATE2X || op == D3DTOP_MODULATE4X;
        if (op == D3DTOP_SELECTARG1 || op == D3DTOP_SELECTARG2)
        {
            if (keeps(selected))
                continue; // the alpha so far goes on
            stage = {}; // the alpha starts again from this stage's argument
            if (takes(selected) && cuts)
            {
                stage[i] = 1.0f;
                uv[i] = static_cast<float>(set);
            }
        }
        else if (modulate)
        {
            if (!keeps(a1) && !keeps(a2))
                stage = {}; // neither argument is the alpha so far
            if ((takes(a1) || takes(a2)) && cuts)
            {
                stage[i] = 1.0f;
                uv[i] = static_cast<float>(set);
            }
        }
        else
            return material; // an alpha operation the cut does not follow: the caster stays whole
    }
    for (const float s : stage)
        any |= s > 0.5f;
    if (!any)
        return material;
    material.cut = (static_cast<float>(rs[D3DRS_ALPHAREF] & 255u) - (func == D3DCMP_GREATEREQUAL ? 0.5f : 0.0f)) / 255.0f;
    material.stage = stage;
    material.uv = uv;
    return material;
}

CasterMaterial location_caster_material(const shim::TexStore *texture)
{
    CasterMaterial material;
    if (texture && texture->kind == shim::TexKind::Tex2D && texture->cutout)
    {
        material.cut = kGameAlphaReference;
        material.stage = {1.0f, 0.0f, 0.0f};
    }
    return material;
}

void Casters::find_shaders()
{
    if (vs_[0][0])
        return;
    vs_[0][0] = shim::shader_id_by_name("vs_ff_caster");
    vs_[0][1] = shim::shader_id_by_name("vs_ff_caster_skin");
    vs_[1][0] = shim::shader_id_by_name("vs_ff_caster_cut");
    vs_[1][1] = shim::shader_id_by_name("vs_ff_caster_cut_skin");
    fs_[0][0] = shim::shader_id_by_name("fs_ff_caster");
    fs_[0][1] = shim::shader_id_by_name("fs_ff_caster_cut");
    fs_[1][0] = shim::shader_id_by_name("fs_ff_lamp_caster");
    fs_[1][1] = shim::shader_id_by_name("fs_ff_lamp_caster_cut");
}

void Casters::ensure_uniforms()
{
    if (bgfx::isValid(u_cut_))
        return;
    u_cut_ = bgfx::createUniform("u_fxCasterCut", bgfx::UniformType::Vec4);
    u_uv_ = bgfx::createUniform("u_fxCasterUv", bgfx::UniformType::Vec4);
    s_tex0_ = bgfx::createUniform("s_tex0", bgfx::UniformType::Sampler);
}

bgfx::UniformHandle Casters::sampler()
{
    ensure_uniforms();
    return s_tex0_;
}

bgfx::ProgramHandle Casters::program(shim::Device &device, Pass pass, bool skinned, const CasterMaterial &material)
{
    find_shaders();
    const int cut = material.cut > 0.0f ? 1 : 0;
    if (cut)
    {
        ensure_uniforms();
        const float reference[4] = {material.cut, material.stage[0], material.stage[1], material.stage[2]};
        const float sets[4] = {material.uv[0], material.uv[1], material.uv[2], 0.0f};
        bgfx::setUniform(u_cut_, reference);
        bgfx::setUniform(u_uv_, sets);
    }
    return device.program(vs_[cut][skinned ? 1 : 0], fs_[pass == Pass::Lamp ? 1 : 0][cut]);
}

bool Casters::prepare_programs(shim::Device &device, Pass pass, int &budget)
{
    find_shaders();
    const int p = pass == Pass::Lamp ? 1 : 0;
    for (int cut = 0; cut < 2; ++cut)
        for (int skinned = 0; skinned < 2; ++skinned)
            if (!device.prepare_program(vs_[cut][skinned], fs_[p][cut], budget))
                return false;
    return true;
}

void Casters::shutdown()
{
    for (bgfx::UniformHandle *u : {&u_cut_, &u_uv_, &s_tex0_})
        if (bgfx::isValid(*u))
            bgfx::destroy(*u);
    on_device_lost();
}

void Casters::on_device_lost()
{
    u_cut_ = u_uv_ = s_tex0_ = BGFX_INVALID_HANDLE;
}

} // namespace storm::effects
