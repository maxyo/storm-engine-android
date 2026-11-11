#pragma once
#include "colored_mesh.hpp"
class VRenderer;
namespace storm::render {
std::unique_ptr<ColoredMesh> makeLegacyColoredMesh(VRenderer& device);
}
