// Vulkan pipelines of a scene, created before it is shown (src/shim/pipeline_sets.hpp). Every pipeline the device
// creates is recorded under the scene that was loading or shown then, and kept with the device's pipeline cache;
// when that scene loads again, its recorded pipelines are created on the pipeline threads while it loads, and the
// loading fader stays closed (bounded) until the scene's draws have every pipeline they ask for. Nothing on the
// other backends.
#pragma once

#include <bgfx/bgfx.h>

namespace storm::render
{

// Game thread, when a scene starts loading ("location/<id>", "sea", "worldmap"): from now on created pipelines are
// recorded under `scene`, and the ones recorded under it in earlier runs are created ahead. The next fade-in may
// be held (HoldSceneReveal).
void BeginScenePipelines(const char *scene);

// Game thread, every frame of a fade-in (location/src/fader.cpp): true while the scene begun last still waits for
// pipelines, at most a bounded time; the fader then keeps its picture closed. Once it returned false the scene is
// revealed (logged with what was prepared and how long the fader waited) and it returns false until the next
// BeginScenePipelines.
bool HoldSceneReveal();


// Game thread: a program made outside the shim (the interface's), by the names its shaders were created with
// (storm::render::CreateShader). Pipelines recorded for it wait for it instead of being forgotten and are created
// ahead once it is registered. Unregister it before bgfx::destroy.
void RegisterPipelineProgram(const char *vertex_shader, const char *fragment_shader, bgfx::ProgramHandle program);
void UnregisterPipelineProgram(bgfx::ProgramHandle program);

} // namespace storm::render
