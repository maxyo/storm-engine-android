#pragma once
#include "textured_mesh.hpp"
class VRenderer;
namespace storm::render {
std::unique_ptr<TexturedMesh> makeLegacyTexturedMesh(VRenderer&);
}
