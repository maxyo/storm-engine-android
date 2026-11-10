#include <cstdint>
#include <cassert>
#include <cstring>
#include <EffekseerRendererCommon/EffekseerRenderer.ShaderBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.RenderStateBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer_Impl.h>
#include <EffekseerRendererCommon/EffekseerRenderer.RibbonRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.RingRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.SpriteRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.TrackRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.ModelRendererBase.h>
#include <EffekseerRendererCommon/ModelLoader.h>
#include "bgfxrenderer.h"
#include "shaders.h"

#include <vector>

#define BGFX(api) m_bgfx->api

#define MAX_PATH 2048
// Effekseer's GL renderer uses 10 on GLES; the shader uniform arrays are sized to match.
#define MaxInstanced 10

#define LAYOUT_LIGHTING 0
#define LAYOUT_SIMPLE 1
#define LAYOUT_ADVLIGHTING 2
#define LAYOUT_ADVSIMPLE 3
#define LAYOUT_COUNT 4

namespace EffekseerRendererBGFX {

static const int SHADERCOUNT = (int)EffekseerRenderer::RendererShaderType::Material;

// Renderer

class VertexLayout;
using VertexLayoutRef = Effekseer::RefPtr<VertexLayout>;

class VertexLayout : public Effekseer::Backend::VertexLayout {
private:
	Effekseer::CustomVector<Effekseer::Backend::VertexLayoutElement> elements_;
public:
	VertexLayout(const Effekseer::Backend::VertexLayoutElement* elements, int32_t elementCount) {
		elements_.resize(elementCount);
		for (int32_t i = 0; i < elementCount; i++) {
			elements_[i] = elements[i];
		}
	}
	~VertexLayout() = default;
	const Effekseer::CustomVector<Effekseer::Backend::VertexLayoutElement>& GetElements() const	{
		return elements_;
	}
};

class RendererImplemented;
using RendererImplementedRef = ::Effekseer::RefPtr<RendererImplemented>;

class Texture : public Effekseer::Backend::Texture {
private:
	const RendererImplemented *m_render;
	bgfx_texture_handle_t m_handle;
public:
	Texture(const RendererImplemented *render, bgfx_texture_handle_t handle, int width, int height, int levels)
		: m_render(render), m_handle(handle) {
		param_.Format = Effekseer::Backend::TextureFormatType::R8G8B8A8_UNORM;
		param_.Dimension = 2;
		param_.Size = {width, height, 1};
		param_.MipLevelCount = levels;
	}
	~Texture() override;
	bgfx_texture_handle_t GetInterface() const {
		return m_handle;
	}
	// The texture keeps its identity for the effects that hold it; the old handle is destroyed.
	void ReplaceInterface(bgfx_texture_handle_t handle);
	bgfx_texture_handle_t RemoveInterface() {
		bgfx_texture_handle_t ret = m_handle;
		m_handle.idx = UINT16_MAX;
		return ret;
	}
};

class GraphicsDevice;
using GraphicsDeviceRef = Effekseer::RefPtr<GraphicsDevice>;

class GraphicsDevice : public Effekseer::Backend::GraphicsDevice {
private:
	RendererImplemented * m_render;
public:
	GraphicsDevice(RendererImplemented *render) : m_render(render) {};
	~GraphicsDevice() override = default;

	// For Renderer::Impl::CreateProxyTextures
	Effekseer::Backend::TextureRef CreateTexture(const Effekseer::Backend::TextureParameter& param, const Effekseer::CustomVector<uint8_t>& initialData) override;
	Effekseer::Backend::VertexLayoutRef CreateVertexLayout(const Effekseer::Backend::VertexLayoutElement* elements, int32_t elementCount) override {
		return Effekseer::MakeRefPtr<VertexLayout>(elements, elementCount);
	}
	// For ModelRenderer
	Effekseer::Backend::VertexBufferRef CreateVertexBuffer(int32_t size, const void* initialData, bool isDynamic) override;
	Effekseer::Backend::IndexBufferRef CreateIndexBuffer(int32_t elementCount, const void* initialData, Effekseer::Backend::IndexBufferStrideType stride) override;

	std::string GetDeviceName() const override {
		return "BGFX";
	}
};

class RenderState : public EffekseerRenderer::RenderStateBase {
private:
	RendererImplemented* m_renderer;
	bool m_mirror;
public:
	RenderState(RendererImplemented* renderer, bool mirror) : m_renderer(renderer), m_mirror(mirror) {}
	virtual ~RenderState() override = default;
	void Update(bool forced);
};

class Renderer : public EffekseerRenderer::Renderer {
public:
	Renderer() = default;
	virtual ~Renderer() = default;
	virtual int32_t GetSquareMaxCount() const = 0;
	virtual void SetSquareMaxCount(int32_t count) = 0;
};

class RendererImplemented : public Renderer, public Effekseer::ReferenceObject {
private:
	// Shader
	class Shader : public EffekseerRenderer::ShaderBase {
		friend class RendererImplemented;
	private:
		static const int maxUniform = 64;
		static const int maxSamplers = 8;
		int m_vcbSize = 0;
		int m_pcbSize = 0;
		int m_vsSize = 0;
		int m_fsSize = 0;
		uint8_t * m_vcbBuffer = nullptr;
		uint8_t * m_pcbBuffer = nullptr;
		struct {
			bgfx_uniform_handle_t handle;
			int count;
			void * ptr;
		} m_uniform[maxUniform];
		bgfx_uniform_handle_t m_samplers[maxSamplers];
		bgfx_program_handle_t m_program;
		const RendererImplemented *m_render;
	public:
		enum UniformType {
			Vertex,
			Pixel,
			Texture,
		};
		Shader(const RendererImplemented * render)
			: m_render(render) {}
		~Shader() override {
			delete[] m_vcbBuffer;
			delete[] m_pcbBuffer;
			if (m_render)
				m_render->ReleaseShader(this);
		}
		virtual void SetVertexConstantBufferSize(int32_t size) override {
			if (size > 0) {
				assert(m_vcbSize == 0);
				m_vcbSize = size;
				m_vcbBuffer = new uint8_t[size];
			}
		}
		virtual void SetPixelConstantBufferSize(int32_t size) override {
			if (size > 0) {
				assert(m_pcbSize == 0);
				m_pcbSize = size;
				m_pcbBuffer = new uint8_t[size];
			}
		}
		virtual void* GetVertexConstantBuffer() override {
			return m_vcbBuffer;
		}
		virtual void* GetPixelConstantBuffer() override {
			return m_pcbBuffer;
		}
		virtual void SetConstantBuffer() override {
			m_render->SumbitUniforms(this);
		}
		bool isValid() const {
			return m_render != nullptr;
		}
	};
	class StaticIndexBuffer : public Effekseer::Backend::IndexBuffer {
	private:
		const RendererImplemented * m_render;
		bgfx_index_buffer_handle_t m_buffer;
	public:
		StaticIndexBuffer(
			const RendererImplemented *render,
			bgfx_index_buffer_handle_t buffer,
			int stride,
			int count ) : m_render(render) , m_buffer(buffer) {
			strideType_ = stride == 4 ? Effekseer::Backend::IndexBufferStrideType::Stride4 : Effekseer::Backend::IndexBufferStrideType::Stride2;
			elementCount_ = count;
		}
		virtual ~StaticIndexBuffer() override {
			m_render->ReleaseIndexBuffer(this);
		}
		void UpdateData(const void* src, int32_t size, int32_t offset) override { assert(false); }	// Can't Update
		bgfx_index_buffer_handle_t GetInterface() const { return m_buffer; }
	};
	// For ModelRenderer
	class StaticVertexBuffer : public Effekseer::Backend::VertexBuffer {
	private:
		const RendererImplemented * m_render;
		bgfx_vertex_buffer_handle_t m_buffer;
	public:
		StaticVertexBuffer(
			const RendererImplemented *render,
			bgfx_vertex_buffer_handle_t buffer ) : m_render(render) , m_buffer(buffer) {}
		virtual ~StaticVertexBuffer() override {
			m_render->ReleaseVertexBuffer(this);
		}
		void UpdateData(const void* src, int32_t size, int32_t offset) override { assert(false); }	// Can't Update
		bgfx_vertex_buffer_handle_t GetInterface() const { return m_buffer; }
	};
	class BGFXStandardRenderer : public EffekseerRenderer::StandardRenderer<RendererImplemented, Shader> {
		RendererImplemented *m_renderer;
		EffekseerRenderer::StandardRendererState m_state;
	public:
		BGFXStandardRenderer(RendererImplemented* renderer) : StandardRenderer(renderer) , m_renderer(renderer) {}
		void BeginRenderingAndRenderingIfRequired(const EffekseerRenderer::StandardRendererState& state, int32_t count, int& stride, void*& data) {
			if (state != m_state) {
				DoRendering();
				m_state = state;
				m_renderer->SwitchLayout(state.Collector.ShaderType);
			}
			if (!m_renderer->AppendSprites(count, stride, data)) {
				DoRendering();
				m_renderer->AllocVertexBuffer();
				m_renderer->AppendSprites(count, stride, data);
			}
			if (state.Collector.IsBackgroundRequiredOnFirstPass && m_renderer->GetDistortingCallback() != nullptr) {
				DoRendering();
			}
		}
		void ResetAndRenderingIfRequired() {
			DoRendering();
		}
		void DoRendering() {
			if (!m_renderer->NeedDraw())
				return;

			const auto& mProj = m_renderer->GetProjectionMatrix();
			const auto& mCamera = m_renderer->GetCameraMatrix();
			int32_t passNum = 1;
			if (m_state.Collector.ShaderType == EffekseerRenderer::RendererShaderType::Material) {
				if (m_state.Collector.MaterialDataPtr->RefractionUserPtr != nullptr) {
					// refraction and standard
					passNum = 2;
				}
			}
			for (int32_t passInd = 0; passInd < passNum; passInd++) {
				RenderingInternal(
					mCamera,
					mProj,
					std::tuple<Effekseer::Backend::VertexBufferRef, int>(nullptr, 0),
					0,
					1,
					passInd,
					m_state);
			}
			m_renderer->ResetDraw();
		}

		EffekseerRenderer::StandardRendererState& GetState(){
			return m_state;
		}

		void Reset() {
			m_state = EffekseerRenderer::StandardRendererState();
		} 
	};
public:
	class ModelRenderer : public EffekseerRenderer::ModelRendererBase {
	private:
		RendererImplemented* m_render;
		Shader * m_shaders[SHADERCOUNT];
	public:
		ModelRenderer(RendererImplemented* renderer) : m_render(renderer) {
			int i;
			for (i=0;i<SHADERCOUNT;i++) {
				m_shaders[i] = nullptr;
			}

			VertexType = EffekseerRenderer::ModelRendererVertexType::Instancing;
		}
		virtual ~ModelRenderer() override {
			for (auto shader : m_shaders) {
				ES_SAFE_DELETE(shader);
			}
		}
		bool Initialize(struct InitArgs *init) {
//			const uint32_t depthSlot[(int)EffekseerRenderer::RendererShaderType::Material] = {1, 2, 2, 6, 7, 7,};
			for (auto t : {
				EffekseerRenderer::RendererShaderType::Unlit,
				EffekseerRenderer::RendererShaderType::Lit,
				EffekseerRenderer::RendererShaderType::BackDistortion,
				EffekseerRenderer::RendererShaderType::AdvancedUnlit,
				EffekseerRenderer::RendererShaderType::AdvancedLit,
				EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
			}) {
				Shader * s = m_render->CreateShader();
				int id = (int)t;
				m_shaders[id] = s;
				const char *shadername = NULL;
				switch (t) {
				case EffekseerRenderer::RendererShaderType::Unlit :
					shadername = "model_unlit";
					break;
				case EffekseerRenderer::RendererShaderType::Lit :
					shadername = "model_lit";
					break;
				case EffekseerRenderer::RendererShaderType::BackDistortion :
					shadername = "model_distortion";
					break;
				case EffekseerRenderer::RendererShaderType::AdvancedUnlit :
					shadername = "model_adv_unlit";
					break;
				case EffekseerRenderer::RendererShaderType::AdvancedLit :
					shadername = "model_adv_lit";
					break;
				case EffekseerRenderer::RendererShaderType::AdvancedBackDistortion :
					shadername = "model_adv_distortion";
					break;
				default:
					assert(false);
					break;
				}
				if (!m_render->InitShader(s, shadername)){
					return false;
				}
			}
			for (auto t : {
				EffekseerRenderer::RendererShaderType::Unlit,
				EffekseerRenderer::RendererShaderType::Lit,
				EffekseerRenderer::RendererShaderType::BackDistortion,
			}) {
				Shader * s = m_shaders[(int)t];
				typedef EffekseerRenderer::ModelRendererVertexConstantBuffer<MaxInstanced> VCB;
				s->SetVertexConstantBufferSize(sizeof(VCB));
#define VUNIFORM(uname, fname) m_render->AddUniform(s, #uname, Shader::UniformType::Vertex, offsetof(VCB, fname));
					VUNIFORM(u_mCameraProj, 	CameraMatrix)
					VUNIFORM(u_mModel_Inst, 	ModelMatrix)
					VUNIFORM(u_fUV, 			ModelUV)
					VUNIFORM(u_fModelColor, 	ModelColor)
					VUNIFORM(u_fLightDirection,	LightDirection)
					VUNIFORM(u_fLightColor, 	LightColor)
					VUNIFORM(u_fLightAmbient, 	LightAmbientColor)
					VUNIFORM(u_mUVInversed, 	UVInversed)
#undef VUNIFORM
			}
			for (auto t : {
				EffekseerRenderer::RendererShaderType::AdvancedUnlit,
				EffekseerRenderer::RendererShaderType::AdvancedLit,
				EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
			}) {
				Shader * s = m_shaders[(int)t];
				typedef EffekseerRenderer::ModelRendererAdvancedVertexConstantBuffer<MaxInstanced> VCB;
				s->SetVertexConstantBufferSize(sizeof(VCB));
#define VUNIFORM(uname, fname) m_render->AddUniform(s, #uname, Shader::UniformType::Vertex, offsetof(VCB, fname));
					VUNIFORM(u_mCameraProj, 		CameraMatrix)
					VUNIFORM(u_mModel_Inst, 		ModelMatrix)
					VUNIFORM(u_fUV, 				ModelUV)
					VUNIFORM(u_fAlphaUV, 			ModelAlphaUV)
					VUNIFORM(u_fUVDistortionUV, 	ModelUVDistortionUV)
					VUNIFORM(u_fBlendUV, 			ModelBlendUV)
					VUNIFORM(u_fBlendAlphaUV, 		ModelBlendAlphaUV)
					VUNIFORM(u_fBlendUVDistortionUV,ModelBlendUVDistortionUV)
					VUNIFORM(u_fFlipbookParameter, 	ModelFlipbookParameter)
					VUNIFORM(u_fFlipbookIndexAndNextRate, ModelFlipbookIndexAndNextRate)
					VUNIFORM(u_fModelAlphaThreshold,ModelAlphaThreshold)
					VUNIFORM(u_fModelColor, 		ModelColor)
					VUNIFORM(u_fLightDirection, 	LightDirection)
					VUNIFORM(u_fLightColor, 		LightColor)
					VUNIFORM(u_fLightAmbient, 	LightAmbientColor)
					VUNIFORM(u_mUVInversed, 		UVInversed)
#undef VUNIFORM
			}
			m_render->SetPixelConstantBuffer(m_shaders);
			m_render->SetSamplers(m_shaders);
			return true;
		}
		void BeginRendering(const Effekseer::ModelRenderer::NodeParameter& parameter, int32_t count, void* userData) override {
			BeginRendering_(m_render, parameter, count, userData);
		}
		virtual void Rendering(const Effekseer::ModelRenderer::NodeParameter& parameter, const Effekseer::ModelRenderer::InstanceParameter& instanceParameter, void* userData) override {
			Rendering_<RendererImplemented>(m_render, parameter, instanceParameter, userData);
		}
		void EndRendering(const Effekseer::ModelRenderer::NodeParameter& parameter, void* userData) override {
			Effekseer::ModelRef model = nullptr;

			if (parameter.IsProceduralMode)
				model = parameter.EffectPointer->GetProceduralModel(parameter.ModelIndex);
			else
				model = parameter.EffectPointer->GetModel(parameter.ModelIndex);
			if (!m_render->StoreModelToGPU(model)) {
				return;
			}
			Shader * shader_ad_lit_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedLit];
			Shader * shader_ad_unlit_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedUnlit];
			Shader * shader_ad_distortion_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedBackDistortion];
			Shader * shader_lit_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::Lit];
			Shader * shader_unlit_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::Unlit];
			Shader * shader_distortion_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::BackDistortion];
			EndRendering_<RendererImplemented, Shader, Effekseer::Model, true, MaxInstanced>(
				m_render, shader_ad_lit_, shader_ad_unlit_, shader_ad_distortion_, shader_lit_, shader_unlit_, shader_distortion_, parameter, userData);
		}
	};
private:
	GraphicsDeviceRef m_device = nullptr;
	bgfx_interface_vtbl_t * m_bgfx = nullptr;
	EffekseerRenderer::RenderStateBase* m_renderState = nullptr;
	bool m_restorationOfStates = true;
	BGFXStandardRenderer * m_standardRenderer = nullptr;
	EffekseerRenderer::DistortingCallback* m_distortingCallback = nullptr;
	StaticIndexBuffer* m_indexBuffer = nullptr;
	Shader* m_currentShader = nullptr;
	int32_t m_squareMaxCount = 0;
	int32_t m_indexBufferStride = 2;
	bgfx_view_id_t m_viewid = 0;
	Effekseer::Backend::TextureRef m_noTexture = nullptr;
	bgfx_vertex_layout_t m_modellayout;

	struct VertexLayoutInfo {
		bgfx_vertex_layout_t			layout = {0};
		bgfx_transient_vertex_buffer_t	tvb = {0};
		int offset = 0;
		int count = 0;
		int cap = 0;
	};
	VertexLayoutInfo m_layouts[LAYOUT_COUNT];// = { {0}, {0}, 0, 0, 0};
	int m_current_layout = 0;
	InitArgs m_initArgs;
	bgfx_encoder_t *m_encoder = nullptr;

	//! because gleDrawElements has only index offset
	int32_t GetIndexSpriteCount() const {
		int vsSize = EffekseerRenderer::GetMaximumVertexSizeInAllTypes() * m_squareMaxCount * 4;

		size_t size = sizeof(EffekseerRenderer::SimpleVertex);
		size = (std::min)(size, sizeof(EffekseerRenderer::DynamicVertex));
		size = (std::min)(size, sizeof(EffekseerRenderer::LightingVertex));

		return (int32_t)(vsSize / size / 4 + 1);
	}
	StaticIndexBuffer * CreateIndexBuffer(const bgfx_memory_t *mem, int stride) {
		bgfx_index_buffer_handle_t handle = BGFX(create_index_buffer)(mem, stride == 4 ? BGFX_BUFFER_INDEX32 : BGFX_BUFFER_NONE);
		return new StaticIndexBuffer(this, handle, stride, mem->size / stride);
	}
	void InitIndexBuffer() {
		int n = GetIndexSpriteCount();
		int i,j;
		const bgfx_memory_t *mem = BGFX(alloc)(n * 6 * m_indexBufferStride);
		uint8_t * ptr = mem->data;
		for (i=0;i<n;i++) {
			int buf[6] = {
				3 + 4 * i,
				1 + 4 * i,
				0 + 4 * i,
				3 + 4 * i,
				0 + 4 * i,
				2 + 4 * i,
			};
			if (m_indexBufferStride == 2) {
				uint16_t * dst = (uint16_t *)ptr;
				for (j=0;j<6;j++)
					dst[j] = (uint16_t)buf[j];
			} else {
				memcpy(ptr, buf, sizeof(buf));
			}
			ptr += 6 * m_indexBufferStride;
		}
		if (m_indexBuffer)
			delete m_indexBuffer;

		m_indexBuffer = CreateIndexBuffer(mem, m_indexBufferStride);
	}
	void GenVertexLayout(bgfx_vertex_layout_t *layout, EffekseerRenderer::RendererShaderType t) const {
		VertexLayoutRef v = EffekseerRenderer::GetVertexLayout(m_device, t).DownCast<VertexLayout>();
		const auto &elements = v->GetElements();
		BGFX(vertex_layout_begin)(layout, BGFX_RENDERER_TYPE_NOOP);
		for (int i = 0; i < elements.size(); i++) {
			const auto &e = elements[i];
			bgfx_attrib_t attrib = BGFX_ATTRIB_POSITION;
			uint8_t num = 0;
			bgfx_attrib_type_t type = BGFX_ATTRIB_TYPE_FLOAT;
			bool normalized = false;
			bool asInt = false;
			switch (e.Format) {
			case Effekseer::Backend::VertexLayoutFormat::R32_FLOAT :
				num = 1;
				type = BGFX_ATTRIB_TYPE_FLOAT;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R32G32_FLOAT :
				num = 2;
				type = BGFX_ATTRIB_TYPE_FLOAT;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R32G32B32_FLOAT :
				num = 3;
				type = BGFX_ATTRIB_TYPE_FLOAT;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R32G32B32A32_FLOAT :
				num = 4;
				type = BGFX_ATTRIB_TYPE_FLOAT;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R8G8B8A8_UNORM :
				num = 4;
				type = BGFX_ATTRIB_TYPE_UINT8;
				normalized = true;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R8G8B8A8_UINT :
				num = 4;
				type = BGFX_ATTRIB_TYPE_UINT8;
				break;
			}
			if (e.SemanticName == "POSITION") {
				attrib = BGFX_ATTRIB_POSITION;
			} else if (e.SemanticName == "NORMAL") {
				switch (e.SemanticIndex) {
				case 0:
					attrib = BGFX_ATTRIB_COLOR0;
					break;
				case 1:
					attrib = BGFX_ATTRIB_NORMAL;
					break;
				case 2:
					attrib = BGFX_ATTRIB_TANGENT;
					break;
				case 3:
					attrib = BGFX_ATTRIB_BITANGENT;
					break;
				case 4:
					attrib = BGFX_ATTRIB_COLOR1;
					break;
				case 5:
					attrib = BGFX_ATTRIB_COLOR2;
					break;
				default:
					attrib = BGFX_ATTRIB_COLOR3;
					break;
				}
			} else if (e.SemanticName == "TEXCOORD") {
				attrib = (bgfx_attrib_t)((int)BGFX_ATTRIB_TEXCOORD0 + e.SemanticIndex);
			}
			BGFX(vertex_layout_add)(layout, attrib, num, type, normalized, asInt);
		}
		BGFX(vertex_layout_end)(layout);
	}

	void InitVertexLayout() {
		bgfx_vertex_layout_t *layout = &m_modellayout;
		BGFX(vertex_layout_begin)(layout, BGFX_RENDERER_TYPE_NOOP);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_NORMAL, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_BITANGENT, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_TANGENT, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_TEXCOORD1, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);	// Model::Vertex::UV2 (1.80)
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
		BGFX(vertex_layout_end)(layout);
		
		GenVertexLayout(&m_layouts[LAYOUT_LIGHTING].layout, 	EffekseerRenderer::RendererShaderType::Lit);
		GenVertexLayout(&m_layouts[LAYOUT_SIMPLE].layout, 		EffekseerRenderer::RendererShaderType::Unlit);
		GenVertexLayout(&m_layouts[LAYOUT_ADVLIGHTING].layout, 	EffekseerRenderer::RendererShaderType::AdvancedLit);
		GenVertexLayout(&m_layouts[LAYOUT_ADVSIMPLE].layout, 	EffekseerRenderer::RendererShaderType::AdvancedUnlit);

	}
	void SetPixelConstantBuffer(Shader *shaders[]) const {
		for (auto t: {
			EffekseerRenderer::RendererShaderType::Unlit,
			EffekseerRenderer::RendererShaderType::Lit,
			EffekseerRenderer::RendererShaderType::AdvancedUnlit,
			EffekseerRenderer::RendererShaderType::AdvancedLit,
		}) {
			int id = (int)t;
			Shader * s = shaders[id];
			s->SetPixelConstantBufferSize(sizeof(EffekseerRenderer::PixelConstantBuffer));
#define PUNIFORM(uname, fname) AddUniform(s, #uname, Shader::UniformType::Pixel, offsetof(EffekseerRenderer::PixelConstantBuffer, fname));
			PUNIFORM(u_fsfLightDirection, 		LightDirection)
			PUNIFORM(u_fsfLightColor, 			LightColor)
			PUNIFORM(u_fsfLightAmbient, 		LightAmbientColor)
			PUNIFORM(u_fsfFlipbookParameter, 	FlipbookParam)
			PUNIFORM(u_fsfUVDistortionParameter,UVDistortionParam)
			PUNIFORM(u_fsfBlendTextureParameter,BlendTextureParam)
			PUNIFORM(u_fsfCameraFrontDirection, CameraFrontDirection)
			PUNIFORM(u_fsfFalloffParameter, 	FalloffParam.Buffer)
			PUNIFORM(u_fsfFalloffBeginColor,	FalloffParam.BeginColor)
			PUNIFORM(u_fsfFalloffEndColor,  	FalloffParam.EndColor)
			PUNIFORM(u_fsfEmissiveScaling, 		EmmisiveParam)
			PUNIFORM(u_fsfEdgeColor,			EdgeParam.EdgeColor)
			PUNIFORM(u_fsfEdgeParameter, 		EdgeParam.Buffer)
			PUNIFORM(u_fssoftParticleParam, 	SoftParticleParam.softParticleParams)
			PUNIFORM(u_fsreconstructionParam1, 	SoftParticleParam.reconstructionParam1)
			PUNIFORM(u_fsreconstructionParam2, 	SoftParticleParam.reconstructionParam2)
			PUNIFORM(u_fsmUVInversedBack, 		UVInversedBack)
			PUNIFORM(u_fsmiscFlags, 			MiscFlags)
#undef PUNIFORM
		}
		for (auto t: {
			EffekseerRenderer::RendererShaderType::BackDistortion,
			EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
		}) {
			int id = (int)t;
			Shader * s = shaders[id];
			s->SetPixelConstantBufferSize(sizeof(EffekseerRenderer::PixelConstantBufferDistortion));
#define PUNIFORM(uname, fname) AddUniform(s, #uname, Shader::UniformType::Pixel, offsetof(EffekseerRenderer::PixelConstantBufferDistortion, fname));
			PUNIFORM(u_fsg_scale, 				DistortionIntencity)
			PUNIFORM(u_fsmUVInversedBack, 		UVInversedBack)
			PUNIFORM(u_fsfFlipbookParameter, 	FlipbookParam)
			PUNIFORM(u_fsfUVDistortionParameter,UVDistortionParam)
			PUNIFORM(u_fsfBlendTextureParameter,BlendTextureParam)
			PUNIFORM(u_fssoftParticleParam, 	SoftParticleParam.softParticleParams)
			PUNIFORM(u_fsreconstructionParam1, 	SoftParticleParam.reconstructionParam1)
			PUNIFORM(u_fsreconstructionParam2, 	SoftParticleParam.reconstructionParam2)
#undef PUNIFORM
		}
	}

	void SetSamplers(Shader *shaders[]){
		const uint32_t shaderCount = (uint32_t)EffekseerRenderer::RendererShaderType::Material;
		
		const int32_t alphaSlot[shaderCount] 			= {-1, -1, -1, 1, 2, 2};
		const int32_t uvDistortionSlot[shaderCount]		= {-1, -1, -1, 2, 3, 3};
		const int32_t blendSlot[shaderCount] 			= {-1, -1, -1, 3, 4, 4};
		const int32_t blendAlphaSlot[shaderCount] 		= {-1, -1, -1, 4, 5, 5};
		const int32_t blendUVDistortionSlot[shaderCount]= {-1, -1, -1, 5, 6, 6};
		const int32_t depthSlot[shaderCount] 			= { 1,  2,  2, 6, 7, 7,};
		for (auto t : {
			EffekseerRenderer::RendererShaderType::Unlit,
			EffekseerRenderer::RendererShaderType::Lit,
			EffekseerRenderer::RendererShaderType::BackDistortion,
			EffekseerRenderer::RendererShaderType::AdvancedUnlit,
			EffekseerRenderer::RendererShaderType::AdvancedLit,
			EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
		}) {
			const int32_t idx = (int)t;
			auto s = shaders[idx];
#define UTEXTURE(uname, slotidx)	AddUniform(s, #uname, Shader::UniformType::Texture, slotidx);
			//AddUniform(s, "s_colorTex", Shader::UniformType::Texture, 0);
			UTEXTURE(s_colorTex, 				0);
			UTEXTURE(s_backTex, 				1);
			UTEXTURE(s_normalTex, 				1);
			UTEXTURE(s_alphaTex, 				alphaSlot[idx]);
			UTEXTURE(s_uvDistortionTex, 		uvDistortionSlot[idx]);
			UTEXTURE(s_blendTex, 				blendSlot[idx]);
			UTEXTURE(s_blendAlphaTex, 			blendAlphaSlot[idx]);
			UTEXTURE(s_blendUVDistortionTex, 	blendUVDistortionSlot[idx]);
			UTEXTURE(s_depthTex, 				depthSlot[idx]);
#undef UTEXTURE
		}
	}
	bool InitShaders(struct InitArgs *init) {
		m_initArgs = *init;
		Shader * shaders[SHADERCOUNT];
		for (auto t : {
			EffekseerRenderer::RendererShaderType::Unlit,
			EffekseerRenderer::RendererShaderType::Lit,
			EffekseerRenderer::RendererShaderType::BackDistortion,
			EffekseerRenderer::RendererShaderType::AdvancedUnlit,
			EffekseerRenderer::RendererShaderType::AdvancedLit,
			EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
		}) {
			Shader * s = new Shader(this);
			int id = (int)t;
			shaders[id] = s;
//			uint32_t depthTexSlot = 1;
			const char *shadername = NULL;
			switch (t) {
			case EffekseerRenderer::RendererShaderType::Unlit :
				shadername = "sprite_unlit";
				break;
			case EffekseerRenderer::RendererShaderType::Lit :
				shadername = "sprite_lit";
				break;
			case EffekseerRenderer::RendererShaderType::BackDistortion :
				shadername = "sprite_distortion";
				break;
			case EffekseerRenderer::RendererShaderType::AdvancedUnlit :
				shadername = "sprite_adv_unlit";
				break;
			case EffekseerRenderer::RendererShaderType::AdvancedLit :
				shadername = "sprite_adv_lit";
				break;
			case EffekseerRenderer::RendererShaderType::AdvancedBackDistortion :
				shadername = "sprite_adv_distortion";
				break;
			default:
				assert(false);
				break;
			}
			if (!InitShader(s, shadername)){
				return false;
			}
			s->SetVertexConstantBufferSize(sizeof(EffekseerRenderer::StandardRendererVertexBuffer));
			AddUniform(s, "u_mCamera", Shader::UniformType::Vertex,
				offsetof(EffekseerRenderer::StandardRendererVertexBuffer, constantVSBuffer[0]));
			AddUniform(s, "u_mCameraProj", Shader::UniformType::Vertex,
				offsetof(EffekseerRenderer::StandardRendererVertexBuffer, constantVSBuffer[1]));
			AddUniform(s, "u_mUVInversed", Shader::UniformType::Vertex,
				offsetof(EffekseerRenderer::StandardRendererVertexBuffer, uvInversed));
			AddUniform(s, "u_mflipbookParameter", Shader::UniformType::Vertex,
				offsetof(EffekseerRenderer::StandardRendererVertexBuffer, flipbookParameter));
		}
		SetPixelConstantBuffer(shaders);
		SetSamplers(shaders);
		GetImpl()->ShaderUnlit = std::unique_ptr<EffekseerRenderer::ShaderBase>(shaders[(int)EffekseerRenderer::RendererShaderType::Unlit]);
		GetImpl()->ShaderLit = std::unique_ptr<EffekseerRenderer::ShaderBase>(shaders[(int)EffekseerRenderer::RendererShaderType::Lit]);
		GetImpl()->ShaderDistortion = std::unique_ptr<EffekseerRenderer::ShaderBase>(shaders[(int)EffekseerRenderer::RendererShaderType::BackDistortion]);
		GetImpl()->ShaderAdUnlit = std::unique_ptr<EffekseerRenderer::ShaderBase>(shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedUnlit]);
		GetImpl()->ShaderAdLit = std::unique_ptr<EffekseerRenderer::ShaderBase>(shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedLit]);
		GetImpl()->ShaderAdDistortion = std::unique_ptr<EffekseerRenderer::ShaderBase>(shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedBackDistortion]);
		return true;
	}
public:
	RendererImplemented() {
		m_device = Effekseer::MakeRefPtr<GraphicsDevice>(this);
	}
	~RendererImplemented() {
		GetImpl()->DeleteProxyTextures(this);

		ES_SAFE_DELETE(m_distortingCallback);
		ES_SAFE_DELETE(m_standardRenderer);
		ES_SAFE_DELETE(m_renderState);
		ES_SAFE_DELETE(m_indexBuffer);
	}

	void OnLostDevice() override {}
	void OnResetDevice() override {}

	bool Initialize(struct InitArgs *init) {
		m_bgfx = init->bgfx;
		if (!InitShaders(init)) {
			return false;
		}
		InitVertexLayout();
		m_squareMaxCount = init->squareMaxCount;
		if (GetIndexSpriteCount() * 4 > 65536) {
			m_indexBufferStride = 4;
		}
		InitIndexBuffer();
		m_renderState = new RenderState(this, init->mirror_winding);
		
		m_standardRenderer = new BGFXStandardRenderer(this);

		// No scene depth copy is provided: soft particles are drawn hard, and distortion nodes
		// (which need the background) are skipped by StandardRenderer (GetBackground() is null).
		GetImpl()->isSoftParticleEnabled = false;
		GetImpl()->CreateProxyTextures(this);
		return true;
	}
	void SetRestorationOfStatesFlag(bool flag) override {
		m_restorationOfStates = flag;
	}
	bool BeginRendering() override {
		m_encoder = BGFX(encoder_begin)(false);
		GetImpl()->CalculateCameraProjectionMatrix();

		m_renderState->GetActiveState().Reset();
		m_renderState->GetActiveState().TextureIDs.fill(0);
		
		m_standardRenderer->Reset();

		int i;
		for (i=0; i<LAYOUT_COUNT; ++i){
			m_layouts[i].offset = 0;
			m_layouts[i].count = 0;
			m_layouts[i].cap = 0;
		}
		m_current_layout = 0;

		return true;
	}
	bool EndRendering() override {
		m_standardRenderer->ResetAndRenderingIfRequired();
		BGFX(encoder_end)(m_encoder);
		return true;
	}
	StaticIndexBuffer* GetIndexBuffer() {
		return m_indexBuffer;
	}
	int32_t GetSquareMaxCount() const override {
		return m_squareMaxCount;
	}
	void SetSquareMaxCount(int32_t count) override {
		m_squareMaxCount = count;
		InitIndexBuffer();
	}
	EffekseerRenderer::RenderStateBase* GetRenderState() {
		return m_renderState;
	}

	Effekseer::SpriteRendererRef CreateSpriteRenderer() override {
		return Effekseer::SpriteRendererRef(new EffekseerRenderer::SpriteRendererBase<RendererImplemented, false>(this));
	}
	Effekseer::RibbonRendererRef CreateRibbonRenderer() override {
		return Effekseer::RibbonRendererRef(new EffekseerRenderer::RibbonRendererBase<RendererImplemented, false>(this));
	}
	Effekseer::RingRendererRef CreateRingRenderer() override {
		return Effekseer::RingRendererRef(new EffekseerRenderer::RingRendererBase<RendererImplemented, false>(this));
	}
	Effekseer::ModelRendererRef CreateModelRenderer() override {
		return Effekseer::MakeRefPtr<ModelRenderer>(this);
	}
	Effekseer::TrackRendererRef CreateTrackRenderer() override {
		return Effekseer::TrackRendererRef(new EffekseerRenderer::TrackRendererBase<RendererImplemented, false>(this));
	}
	Effekseer::TextureLoaderRef CreateTextureLoader(Effekseer::FileInterfaceRef fileInterface = nullptr) override {
		return nullptr;	// the host loads textures through its texture cache (CreateTextureFromHandle)
	}
	Effekseer::ModelLoaderRef CreateModelLoader(::Effekseer::FileInterfaceRef fileInterface = nullptr) override {
		return Effekseer::MakeRefPtr<EffekseerRenderer::ModelLoader>(m_device, fileInterface);
	}
	Effekseer::MaterialLoaderRef CreateMaterialLoader(::Effekseer::FileInterfaceRef fileInterface = nullptr) override {
		return nullptr;	// no material shader generator for bgfx; the caller installs a reporting loader
	}
	EffekseerRenderer::DistortingCallback* GetDistortingCallback() override {
		return m_distortingCallback;
	}
	void SetDistortingCallback(EffekseerRenderer::DistortingCallback* callback) override {
		ES_SAFE_DELETE(m_distortingCallback);
		m_distortingCallback = callback;
	}
	const Effekseer::Backend::TextureRef& GetBackground() override {
		return m_noTexture;
	}

	void GetDepth(Effekseer::Backend::TextureRef& texture, EffekseerRenderer::DepthReconstructionParameter& reconstructionParam) override {
		texture = nullptr;
	}
	void SetViewId(bgfx_view_id_t viewid) {
		m_viewid = viewid;
	}

	BGFXStandardRenderer* GetStandardRenderer() {
		return m_standardRenderer;
	}
	// For ModelRenderer, See ModelRendererBase
	void SetVertexBuffer(const Effekseer::Backend::VertexBufferRef& vertexBuffer, int32_t stride) {
		(void)stride;
		//m_currentVertexBuffer = vertexBuffer.DownCast<StaticVertexBuffer>()->GetInterface();
		if (vertexBuffer != nullptr)
			BGFX(encoder_set_vertex_buffer)(m_encoder, 0, vertexBuffer.DownCast<StaticVertexBuffer>()->GetInterface(), 0, UINT32_MAX);
	}
	void SetIndexBuffer(StaticIndexBuffer* indexBuffer) {
		assert(indexBuffer == m_indexBuffer);
	}
	void SetIndexBuffer(const Effekseer::Backend::IndexBufferRef& indexBuffer) {
		BGFX(encoder_set_index_buffer)(m_encoder, indexBuffer.DownCast<StaticIndexBuffer>()->GetInterface(), 0, UINT32_MAX);
	}
	void SetLayout(Shader* shader) {}

	void AllocVertexBuffer() {
		auto &info = m_layouts[m_current_layout];
		info.offset = 0;
		info.count = 0;
		BGFX(alloc_transient_vertex_buffer)(&info.tvb, info.cap, &info.layout);
	}

	void SwitchLayout(EffekseerRenderer::RendererShaderType renderingMode) {
		switch (renderingMode) {
		case EffekseerRenderer::RendererShaderType::Lit :
		case EffekseerRenderer::RendererShaderType::BackDistortion :
			m_current_layout = LAYOUT_LIGHTING;
			break;
		case EffekseerRenderer::RendererShaderType::Unlit :
			m_current_layout = LAYOUT_SIMPLE;
			break;
		case EffekseerRenderer::RendererShaderType::AdvancedLit :
		case EffekseerRenderer::RendererShaderType::AdvancedBackDistortion :
			m_current_layout = LAYOUT_ADVLIGHTING;
			break;
		case EffekseerRenderer::RendererShaderType::AdvancedUnlit :
			m_current_layout = LAYOUT_ADVSIMPLE;
			break;
		default:
			assert(false);	// Material: CreateMaterialLoader() never yields a material
			return;
		}
		if (m_layouts[m_current_layout].cap == 0) {
			m_layouts[m_current_layout].cap = 4 * GetSquareMaxCount();
			AllocVertexBuffer();
		}
	}
	bool AppendSprites(int count, int& stride, void*& data) {
		auto& layout = m_layouts[m_current_layout];
		assert(layout.cap > 0);
		stride = layout.tvb.stride;
		data = layout.tvb.data + layout.count * stride;
		if (count + layout.count > layout.cap) {
			// full
			return false;
		}
		layout.count += count;
		return true;
	}

	bool NeedDraw() {
		return m_layouts[m_current_layout].count > 0;
	}
	void ResetDraw() {
		auto& layout = m_layouts[m_current_layout];
		layout.offset = layout.count;
	}
	void DrawSprites(int32_t spriteCount, int32_t vertexOffset) {
		(void)spriteCount;	// do not use spriteCount, use m_vertex_count[] instead
		(void)vertexOffset;

		const auto& layout = m_layouts[m_current_layout];
		const int offset = layout.offset;
		const int count = layout.count - offset;

		BGFX(encoder_set_transient_vertex_buffer)(m_encoder, 0, &layout.tvb, offset, count);
		const uint32_t indexCount = count / 4 * 6;
		BGFX(encoder_set_index_buffer)(m_encoder, m_indexBuffer->GetInterface(), 0, indexCount);
		BGFX(encoder_submit)(m_encoder, m_viewid, m_currentShader->m_program, 0, BGFX_DISCARD_ALL);
		GetImpl()->drawcallCount++;
		GetImpl()->drawvertexCount += count;
	}
	void DrawPolygon(int32_t vertexCount, int32_t indexCount) {
		// todo:
	}
	void DrawPolygonInstanced(int32_t vertexCount, int32_t indexCount, int32_t instanceCount) {
		BGFX(encoder_set_instance_count)(m_encoder, instanceCount);
		BGFX(encoder_submit)(m_encoder, m_viewid, m_currentShader->m_program, 0, BGFX_DISCARD_ALL);
		GetImpl()->drawcallCount++;
		GetImpl()->drawvertexCount += vertexCount * instanceCount;
	}
	void BeginShader(Shader* shader) {
		assert(m_currentShader == nullptr);
		m_currentShader = shader;
	}
	void EndShader(Shader* shader) {
		assert(m_currentShader == shader);
		m_currentShader = nullptr;
	}
	void SetVertexBufferToShader(const void* data, int32_t size, int32_t dstOffset) {
		assert(m_currentShader != nullptr);
		auto p = static_cast<uint8_t*>(m_currentShader->GetVertexConstantBuffer()) + dstOffset;
		memcpy(p, data, size);
	}
	void SetPixelBufferToShader(const void* data, int32_t size, int32_t dstOffset) {
		assert(m_currentShader != nullptr);
		auto p = static_cast<uint8_t*>(m_currentShader->GetPixelConstantBuffer()) + dstOffset;
		memcpy(p, data, size);
	}
	void SetTextures(Shader* shader, Effekseer::Backend::TextureRef* textures, int32_t count) {
		for (int32_t ii=0; ii<count; ++ii){
			auto sampler = shader->m_samplers[ii];
			if (BGFX_HANDLE_IS_VALID(sampler)){
				auto tex = textures[ii].DownCast<EffekseerRendererBGFX::Texture>();
				const auto &state = m_renderState->GetActiveState();
				uint32_t flags = BGFX_SAMPLER_NONE;	// default min/mag/mip as 'linear' and uv address as 'repeat'
				if (state.TextureFilterTypes[ii] == Effekseer::TextureFilterType::Nearest){
					flags |= BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
				}
				if (state.TextureWrapTypes[ii] == Effekseer::TextureWrapType::Clamp){
					flags |= BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
				}
				BGFX(encoder_set_texture)(m_encoder, ii, sampler, tex->GetInterface(), flags);
			}
		}
	}
	void ResetRenderState() override {
		m_renderState->GetActiveState().Reset();
		m_renderState->Update(true);
	}
	void SetCurrentState(uint64_t state) {
		BGFX(encoder_set_state)(m_encoder, state, 0);
	}
	Effekseer::Backend::GraphicsDeviceRef GetGraphicsDevice() const override {
		return m_device;
	}
	virtual int GetRef() override { return Effekseer::ReferenceObject::GetRef(); }
	virtual int AddRef() override { return Effekseer::ReferenceObject::AddRef(); }
	virtual int Release() override { return Effekseer::ReferenceObject::Release(); }

	Shader * CreateShader() const {
		return new Shader(this);
	}
	// Shader API
	bool InitShader(Shader *s, const char *name) const {
		const bgfx_shader_handle_t vs = LoadShader(name, true);
		const bgfx_shader_handle_t fs = LoadShader(name, false);
		if (!(BGFX_HANDLE_IS_VALID(vs) && BGFX_HANDLE_IS_VALID(fs))){
			Log("missing embedded shader", name);
			if (BGFX_HANDLE_IS_VALID(vs))
				BGFX(destroy_shader)(vs);
			if (BGFX_HANDLE_IS_VALID(fs))
				BGFX(destroy_shader)(fs);
			s->m_render = nullptr;
			return false;
		}
		// The program owns the shaders (destroyShaders = true).
		s->m_program = BGFX(create_program)(vs, fs, true);
		if (s->m_program.idx == UINT16_MAX) {
			Log("cannot link program", name);
			s->m_render = nullptr;
			return false;
		}
		bgfx_uniform_handle_t u[Shader::maxUniform];
		s->m_vsSize = BGFX(get_shader_uniforms)(vs, u, Shader::maxUniform);
		int i;
		for (i=0;i<s->m_vsSize;i++) {
			s->m_uniform[i].handle = u[i];
			s->m_uniform[i].count = 0;
			s->m_uniform[i].ptr = nullptr;
		}
		s->m_fsSize = BGFX(get_shader_uniforms)(fs, u, Shader::maxUniform - s->m_vsSize);
		for (i=0;i<s->m_fsSize;i++) {
			s->m_uniform[i+s->m_vsSize].handle = u[i];
			s->m_uniform[i+s->m_vsSize].count = 0;
			s->m_uniform[i+s->m_vsSize].ptr = nullptr;
		}
		for (i=0;i<Shader::maxSamplers;i++) {
			s->m_samplers[i].idx = UINT16_MAX;
		}
		return true;
	}
	void ReleaseShader(Shader *s) const {
		if (s->isValid()) {
			BGFX(destroy_program)(s->m_program);
			s->m_render = nullptr;
		}
	}
	void SumbitUniforms(Shader *s) const {
		if (!s->isValid())
			return;
		int i;
		for (i=0;i<s->m_vsSize + s->m_fsSize;i++) {
			if (s->m_uniform[i].ptr != nullptr) {
				BGFX(encoder_set_uniform)(m_encoder, s->m_uniform[i].handle, s->m_uniform[i].ptr, s->m_uniform[i].count);
			}
		}
	}
	int AddUniform(Shader *s, const char *name, Shader::UniformType type, int offset) const {
		if (!s->isValid())
			return -1;
		int i;
		int from = 0;
		int	to = s->m_vsSize + s->m_fsSize;
		switch(type) {
		case Shader::UniformType::Vertex:
			to = s->m_vsSize;
			break;
		case Shader::UniformType::Pixel:
			from = s->m_vsSize;
			to = s->m_vsSize + s->m_fsSize;
			break;
		default:
			break;
		}
		bgfx_uniform_info_t info;
		for (i=from;i<to;i++) {
			if (s->m_uniform[i].count == 0) {
				info.name[0] = 0;
				BGFX(get_uniform_info)(s->m_uniform[i].handle, &info);
				if (strcmp(info.name, name) == 0) {
					break;
				}
			}
		}

		if (i >= to) {
			return -1;
		}

		switch(type) {
		case Shader::UniformType::Vertex:
			s->m_uniform[i].ptr = s->m_vcbBuffer + offset;
			s->m_uniform[i].count = info.num;
			break;
		case Shader::UniformType::Pixel:
			s->m_uniform[i].ptr = s->m_pcbBuffer + offset;
			s->m_uniform[i].count = info.num;
			break;
		case Shader::UniformType::Texture:
			assert(info.type == BGFX_UNIFORM_TYPE_SAMPLER);
			assert(0 <= offset && offset < Shader::maxSamplers);
			s->m_uniform[i].count = offset + 1;
			assert(!BGFX_HANDLE_IS_VALID(s->m_samplers[offset]));
			s->m_samplers[offset] = s->m_uniform[i].handle;
			break;
		}

		return i;
	}

	void Log(const char *what, const char *name) const {
		char message[256];
		snprintf(message, sizeof(message), "ERROR: %s %s", what, name);
		m_initArgs.log(message);
	}
	// Only Effekseer's proxy textures (1x1 white/normal, RGBA8 without mips) come through here:
	// effect textures are created by the host (CreateTextureFromHandle).
	Effekseer::Backend::TextureRef CreateTexture(const Effekseer::Backend::TextureParameter& param, const Effekseer::CustomVector<uint8_t>& initialData) const {
		const int width = param.Size[0];
		const int height = param.Size[1];
		if (param.Dimension != 2 || param.Format != Effekseer::Backend::TextureFormatType::R8G8B8A8_UNORM
			|| param.MipLevelCount != 1 || width <= 0 || height <= 0 || initialData.size() < size_t(width) * height * 4) {
			char message[160];
			snprintf(message, sizeof(message), "ERROR: unsupported Effekseer texture (format %d, dimension %d, %dx%d, %d levels)",
				int(param.Format), param.Dimension, width, height, param.MipLevelCount);
			m_initArgs.log(message);
			return nullptr;
		}
		const bgfx_memory_t *mem = BGFX(copy)(initialData.data(), uint32_t(size_t(width) * height * 4));
		bgfx_texture_handle_t handle = BGFX(create_texture_2d)(uint16_t(width), uint16_t(height), false, 1, BGFX_TEXTURE_FORMAT_RGBA8,
			BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, mem, 0);
		return Effekseer::MakeRefPtr<Texture>(this, handle, width, height, 1);
	}
	Effekseer::Backend::TextureRef WrapTexture(bgfx_texture_handle_t handle, int width, int height, int levels) const {
		return Effekseer::MakeRefPtr<Texture>(this, handle, width, height, levels);
	}
	void DestroyHandle(bgfx_texture_handle_t handle) const {
		if (BGFX_HANDLE_IS_VALID(handle))
			BGFX(destroy_texture)(handle);
	}
	void ReleaseTexture(Texture *t) const {
		bgfx_texture_handle_t h = t->RemoveInterface();
		if (BGFX_HANDLE_IS_VALID(h))
			BGFX(destroy_texture)(h);
	}
	Effekseer::Backend::IndexBufferRef CreateIndexBuffer(int32_t elementCount, const void* initialData, Effekseer::Backend::IndexBufferStrideType stride) const {
		int s = (stride == Effekseer::Backend::IndexBufferStrideType::Stride4) ? 4 : 2;
		const bgfx_memory_t *mem = BGFX(copy)(initialData, elementCount * s);
		bgfx_index_buffer_handle_t handle = BGFX(create_index_buffer)(mem, s == 4 ? BGFX_BUFFER_INDEX32 : BGFX_BUFFER_NONE);

		return Effekseer::MakeRefPtr<StaticIndexBuffer>(this, handle, s, elementCount);
	}
	Effekseer::Backend::VertexBufferRef CreateVertexBuffer(int32_t size, const void* initialData) const {
		const bgfx_memory_t *mem = BGFX(copy)(initialData, size);
		bgfx_vertex_buffer_handle_t handle = BGFX(create_vertex_buffer)(mem, &m_modellayout, BGFX_BUFFER_NONE);
		return  Effekseer::MakeRefPtr<StaticVertexBuffer>(this, handle);
	}
	void ReleaseIndexBuffer(StaticIndexBuffer *ib) const {
		BGFX(destroy_index_buffer)(ib->GetInterface());
	}
	void ReleaseVertexBuffer(StaticVertexBuffer *vb) const {
		BGFX(destroy_vertex_buffer)(vb->GetInterface());
	}
	bool StoreModelToGPU(Effekseer::ModelRef model) const {
		if (model == nullptr)
			return false;
		model->StoreBufferToGPU(m_device.Get());
		if (!model->GetIsBufferStoredOnGPU())
			return false;
		return true;
	}
};

void RenderState::Update(bool forced) {
	(void)forced;	// ignore forced
	uint64_t state = 0
		| BGFX_STATE_WRITE_RGB
		| BGFX_STATE_WRITE_A
		| BGFX_STATE_FRONT_CCW
		| BGFX_STATE_MSAA;

	if (next_.DepthTest) {
		state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
	} else {
		state |= BGFX_STATE_DEPTH_TEST_ALWAYS;
	}

	if (next_.DepthWrite) {
		state |= BGFX_STATE_WRITE_Z;
	}

	// isCCW
	if (next_.CullingType == Effekseer::CullingType::Front) {
		state |= m_mirror ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
	}
	else if (next_.CullingType == Effekseer::CullingType::Back) {
		state |= m_mirror ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
	}
	if (next_.AlphaBlend == ::Effekseer::AlphaBlendType::Opacity ||
		m_renderer->GetRenderMode() == ::Effekseer::RenderMode::Wireframe) {
			state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_ADD, BGFX_STATE_BLEND_EQUATION_MAX);
			state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
		}
	else if (next_.AlphaBlend == ::Effekseer::AlphaBlendType::Sub)	{
		state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_REVSUB, BGFX_STATE_BLEND_EQUATION_ADD);
		state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
	} else {
		state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_ADD, BGFX_STATE_BLEND_EQUATION_ADD);
		if (next_.AlphaBlend == ::Effekseer::AlphaBlendType::Blend) {
			state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
		} else if (next_.AlphaBlend == ::Effekseer::AlphaBlendType::Add) {
			state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
		} else if (next_.AlphaBlend == ::Effekseer::AlphaBlendType::Mul) {
			state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_SRC_COLOR, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
		}
	}
	m_renderer->SetCurrentState(state);
	active_ = next_;
}

Effekseer::Backend::TextureRef GraphicsDevice::CreateTexture(const Effekseer::Backend::TextureParameter& param, const Effekseer::CustomVector<uint8_t>& initialData) {
	return m_render->CreateTexture(param, initialData);
}

Effekseer::Backend::IndexBufferRef GraphicsDevice::CreateIndexBuffer(int32_t elementCount, const void* initialData, Effekseer::Backend::IndexBufferStrideType stride) {
	return m_render->CreateIndexBuffer(elementCount, initialData, stride);
}

Effekseer::Backend::VertexBufferRef GraphicsDevice::CreateVertexBuffer(int32_t size, const void* initialData, bool isDynamic) {
	assert(isDynamic == false);	// ModelRenderer use Static VB
	return m_render->CreateVertexBuffer(size, initialData);
}

Texture::~Texture() {
	m_render->ReleaseTexture(this);
}

void Texture::ReplaceInterface(bgfx_texture_handle_t handle) {
	m_render->DestroyHandle(m_handle);
	m_handle = handle;
}

// Create Renderer

EffekseerRenderer::RendererRef CreateRenderer(struct InitArgs *init) {
	auto renderer = Effekseer::MakeRefPtr<RendererImplemented>();
	if (renderer->Initialize(init))	{
		return renderer;
	}
	return nullptr;
}

Effekseer::ModelRendererRef CreateModelRenderer(EffekseerRenderer::RendererRef renderer, struct InitArgs *init){
	auto modelRenderer = renderer->CreateModelRenderer();
	return modelRenderer.DownCast<RendererImplemented::ModelRenderer>()->Initialize(init) ? modelRenderer : nullptr;
}

void SetViewId(const EffekseerRenderer::RendererRef &renderer, bgfx_view_id_t viewid) {
	renderer.DownCast<RendererImplemented>()->SetViewId(viewid);
}

Effekseer::Backend::TextureRef CreateTextureFromHandle(const EffekseerRenderer::RendererRef &renderer,
	bgfx_texture_handle_t handle, int width, int height, int levels) {
	return renderer.DownCast<RendererImplemented>()->WrapTexture(handle, width, height, levels);
}

void ReplaceTextureHandle(const Effekseer::Backend::TextureRef &texture, bgfx_texture_handle_t handle) {
	texture.DownCast<Texture>()->ReplaceInterface(handle);
}

}
