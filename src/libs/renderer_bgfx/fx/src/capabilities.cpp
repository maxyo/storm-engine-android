#include "storm/fx/capabilities.hpp"

#include "gpu_util.hpp"
#include "storm/fx/log.hpp"

#include <bgfx/bgfx.h>

namespace storm::fx
{

Support DetectSupport(const RendererFacts &renderer)
{
    Support support;
    support.lut_3d = detail::SupportsLut3d();
    support.shadow_map = detail::SupportsShadowMap();
    support.scene_snapshot = detail::SupportsSceneSnapshot();
    support.wave_field = detail::SupportsWaveField();
    support.normal_map = detail::SupportsNormalMap();
    support.msaa = detail::SupportsMsaa();
    support.anisotropy = renderer.anisotropy;
    support.scene_depth = renderer.scene_depth;
    support.msaa_depth = renderer.scene_depth && renderer.msaa_depth;
    Log(LogLevel::Info, "%s: %s", bgfx::getRendererName(bgfx::getCaps()->rendererType), support.Describe().c_str());
    return support;
}

} // namespace storm::fx
