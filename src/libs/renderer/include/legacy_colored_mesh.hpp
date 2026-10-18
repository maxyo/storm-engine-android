#pragma once
#include "colored_mesh.hpp"
class VDX9RENDER;
namespace storm::render {
std::unique_ptr<ColoredMesh> makeLegacyColoredMesh(VDX9RENDER& device);
}
