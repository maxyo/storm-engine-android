#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace storm::weather {
// Shared geometry for the legacy skyfog pass and the bgfx migration. Packed
// color stays D3D ARGB here; each backend must explicitly decode its byte order.
struct FogVertex { float x, y, z; uint32_t argb; };
static_assert(sizeof(FogVertex) == 16, "Legacy XYZ + diffuse vertex layout must stay packed");
inline constexpr uint32_t fogLevels = 64, fogAngles = 8;
inline constexpr float fogRadius = 2000.f;
inline constexpr uint32_t fogVertexCount = fogLevels * fogAngles + 1;
inline constexpr uint32_t fogIndexCount = 3 * (fogAngles + (fogLevels - 1) * fogAngles * 2);
struct FogMesh {
    std::array<FogVertex, fogVertexCount> vertices;
    std::array<uint16_t, fogIndexCount> indices;
};
template<class ColorAtPosition> FogMesh makeFogSphere(ColorAtPosition color, bool buildIndices = true) {
    FogMesh mesh{};
    uint32_t index = 0;
    for (uint32_t y = 0; y < fogLevels; ++y) {
        const float h = y * fogRadius / fogLevels;
        const float radius = std::sqrt(fogRadius * fogRadius - h * h);
        for (uint32_t x = 0; x < fogAngles; ++x) {
            const uint16_t v = uint16_t(y * fogAngles + x);
            const uint16_t next = uint16_t(y * fogAngles + (x + 1) % fogAngles);
            const float angle = float(x) / float(fogAngles) * 6.28318530717958647692f;
            const float px = radius * std::cos(angle), pz = radius * std::sin(angle);
            mesh.vertices[v] = {px, h, pz, color(px, h, pz)};
            if (!buildIndices) continue;
            if (y + 1 == fogLevels) {
                mesh.indices[index++] = uint16_t(fogVertexCount - 1);
                mesh.indices[index++] = v;
                mesh.indices[index++] = next;
            } else {
                mesh.indices[index++] = v;
                mesh.indices[index++] = next;
                mesh.indices[index++] = uint16_t(v + fogAngles);
                mesh.indices[index++] = next;
                mesh.indices[index++] = uint16_t(v + fogAngles);
                mesh.indices[index++] = uint16_t(next + fogAngles);
            }
        }
    }
    mesh.vertices.back() = {0.f, fogRadius, 0.f, color(0.f, fogRadius, 0.f)};
    return mesh;
}
}
