#ifndef effekseer_bgfx_renderer_h
#define effekseer_bgfx_renderer_h

#include <bgfx/c99/bgfx.h>
#include <Effekseer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>

#if defined(_MSC_VER) && defined(EFXBGFX_DYNAMIC_LIB)
#	if EFXBGFX_EXPORTS 
#		define EFXBGFX_API __declspec(dllexport)
#	else
#		define EFXBGFX_API __declspec(dllimport)
#	endif
#else //!_MSC_VER
#	define EFXBGFX_API
#endif //_MSC_VER

namespace EffekseerRendererBGFX {
	struct InitArgs {
		int squareMaxCount;
		bgfx_interface_vtbl_t *bgfx;
		// Render targets with a bottom-left origin are drawn upside down (the projection flips Y),
		// which mirrors the triangle winding: culling swaps CW/CCW.
		bool mirror_winding;
		void (*log)(const char *message);	// errors (missing shaders, unsupported texture formats)
	};

	// Shaders come from the storm build (embedded blobs, shaders.h); textures from the host's
	// loader, which wraps its bgfx textures with CreateTextureFromHandle. Materials (.efkmat) are
	// not supported; CreateTextureLoader() returns nullptr.
	EFXBGFX_API EffekseerRenderer::RendererRef CreateRenderer(struct InitArgs *init);
	EFXBGFX_API Effekseer::ModelRendererRef CreateModelRenderer(EffekseerRenderer::RendererRef renderer, struct InitArgs *init);
	// Takes ownership of `handle` (destroyed with the texture).
	EFXBGFX_API Effekseer::Backend::TextureRef CreateTextureFromHandle(const EffekseerRenderer::RendererRef &renderer,
		bgfx_texture_handle_t handle, int width, int height, int levels);
	// Swaps the texture's bgfx handle (e.g. RGBA8 -> ETC2 once transcoded); destroys the old one.
	EFXBGFX_API void ReplaceTextureHandle(const Effekseer::Backend::TextureRef &texture, bgfx_texture_handle_t handle);
	// The bgfx view the next BeginRendering() .. EndRendering() submits to.
	EFXBGFX_API void SetViewId(const EffekseerRenderer::RendererRef &renderer, bgfx_view_id_t viewid);
}

#endif
