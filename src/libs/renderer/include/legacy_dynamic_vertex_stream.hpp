#pragma once
#include "dynamic_vertex_stream.hpp"
class VRenderer;
namespace storm::render {
std::unique_ptr<DynamicVertexStream> makeLegacyDynamicVertexStream(VRenderer&);
}
