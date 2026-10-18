#pragma once
#include "dynamic_vertex_stream.hpp"
class VDX9RENDER;
namespace storm::render {
std::unique_ptr<DynamicVertexStream> makeLegacyDynamicVertexStream(VDX9RENDER&);
}
