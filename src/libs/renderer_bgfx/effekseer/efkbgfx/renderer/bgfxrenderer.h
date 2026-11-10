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

	// Shaders and textures come from the storm build: embedded shader blobs (shaders.h) and
	// Effekseer's own texture loader (CreateTextureLoader). Materials (.efkmat) are not supported.
	EFXBGFX_API EffekseerRenderer::RendererRef CreateRenderer(struct InitArgs *init);
	EFXBGFX_API Effekseer::ModelRendererRef CreateModelRenderer(EffekseerRenderer::RendererRef renderer, struct InitArgs *init);
	// The bgfx view the next BeginRendering() .. EndRendering() submits to.
	EFXBGFX_API void SetViewId(const EffekseerRenderer::RendererRef &renderer, bgfx_view_id_t viewid);
}

#endif
