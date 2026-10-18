#pragma once
#include "fog_sphere_mesh.hpp"
#include <array>
#include <string>

namespace storm::render {
struct FogSnapshot {
    weather::FogMesh mesh;
    std::array<float,16> world, view, projection;
};
void writeFogSnapshot(const std::string& path, const FogSnapshot& snapshot);
FogSnapshot readFogSnapshot(const std::string& path);

// Development capture on the engine thread; inert unless explicitly configured.
void configureFogCapture(std::string path);
void requestFogCapture();
void stageFogMesh(const weather::FogMesh& mesh, bool topologyInitialized);
// Returns false when no capture is pending. On an attempted capture, result is
// empty on success, or an error message; I/O failure must not terminate gameplay.
bool captureFogFrame(const float* world, const float* view, const float* projection,
                     std::string& result);
}
