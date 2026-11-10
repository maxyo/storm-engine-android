// The whole runtime without a GL context, shared by the runtime checks: a renderer that
// draws nothing and file helpers for the synthetic packs they derive from tests/pack.
#pragma once

#include "storm_ui/render_backend.h"

#include <RmlUi/Core/RenderInterface.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

class NoRenderer final : public Rml::RenderInterface
{
  public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    {
        return 1;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
    {
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override
    {
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &) override
    {
        return 0;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
    {
        return 1;
    }
    void ReleaseTexture(Rml::TextureHandle) override
    {
    }
    void EnableScissorRegion(bool) override
    {
    }
    void SetScissorRegion(Rml::Rectanglei) override
    {
    }
};

class NoBackend final : public storm::ui::RenderBackend
{
  public:
    Rml::RenderInterface &Interface() override
    {
        return renderer_;
    }
    void SetViewport(int, int) override
    {
    }
    void BeginFrame(bool) override
    {
    }
    void EndFrame() override
    {
    }

  private:
    NoRenderer renderer_;
};

inline std::string Read(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

inline void Write(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream(path, std::ios::binary) << text;
}

