// GL backend: the stock RmlUi GL3 renderer (GLES 3.2 on Android, GL 3.3 core on desktop).
#include "storm_ui/render_backend.h"

#include <RmlUi/Core.h>
#include <RmlUi_Renderer_GL3.h>

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <RmlUi_Include_GL3.h>
#endif

namespace storm::ui
{

namespace
{

class Gl3Renderer final : public RenderInterface_GL3
{
  public:
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override
    {
        std::vector<unsigned char> rgba;
        if (!DecodePackImage(source, dimensions.x, dimensions.y, rgba))
            return 0;
        return GenerateTexture({rgba.data(), rgba.size()}, dimensions);
    }
};

class Gl3Backend final : public RenderBackend
{
  public:
    ~Gl3Backend() override
    {
        renderer_.reset();
        if (ready_)
            RmlGL3::Shutdown();
    }

    bool Start(std::string *error)
    {
        Rml::String message;
        if (!RmlGL3::Initialize(&message))
        {
            if (error)
                *error = message;
            return false;
        }
        ready_ = true;
        renderer_ = std::make_unique<Gl3Renderer>();
        if (!*renderer_)
        {
            if (error)
                *error = "GL renderer: shader setup failed";
            return false;
        }
        return true;
    }

    Rml::RenderInterface &Interface() override
    {
        return *renderer_;
    }
    void SetViewport(int width, int height) override
    {
        renderer_->SetViewport(width, height);
    }
    void BeginFrame(bool clear) override
    {
        if (clear)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        renderer_->BeginFrame();
    }
    void EndFrame() override
    {
        renderer_->EndFrame();
    }

  private:
    std::unique_ptr<Gl3Renderer> renderer_;
    bool ready_ = false;
};

} // namespace

std::unique_ptr<RenderBackend> CreateGl3Backend(std::string *error)
{
    auto backend = std::make_unique<Gl3Backend>();
    if (!backend->Start(error))
        return nullptr;
    return backend;
}

} // namespace storm::ui
