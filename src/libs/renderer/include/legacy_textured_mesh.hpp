#pragma once
#include "textured_mesh.hpp"
class VDX9RENDER;
namespace storm::render {
std::unique_ptr<TexturedMesh> makeLegacyTexturedMesh(VDX9RENDER&);
}
