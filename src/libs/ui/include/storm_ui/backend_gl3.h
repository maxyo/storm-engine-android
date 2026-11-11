// GL render backend (target storm_ui_gl3): RmlUi's stock GL3 renderer on a GL context the
// platform host owns. For hosts without the engine: the application screens in their own
// processes (launcher, downloads, gallery) and host captures. The game draws through
// backend_bgfx.h inside the engine frame.
#pragma once

#include "storm_ui/render_backend.h"

#include <memory>
#include <string>

namespace storm::ui
{

// Needs a current GLES 3.0 (Android) or GL 3.3 core (desktop) context.
std::unique_ptr<RenderBackend> CreateGl3Backend(std::string *error = nullptr);

} // namespace storm::ui
