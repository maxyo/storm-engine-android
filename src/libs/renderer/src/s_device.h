#pragma once

#include "game_pictures.hpp"
#include "technique.h"
#include "font.h"
#include "video_texture.h"
#include "dx9render.h"
#include "vma.hpp"
#include "platform/platform.hpp"

#include "shim/device.hpp"
#include "storm/render/perf_stats.hpp"
#include "script_libriary.h"

#include <stack>
#include <vector>
#include <unordered_map>

#define MAX_STEXTURES 10240
#define MAX_BUFFERS 10240
#define MAX_FONTS 256

struct D3DERRORS
{
    HRESULT err;
    char *descrition;
};

struct texpaths_t
{
    char str[MAX_PATH];
};

struct STEXTURE
{
    storm::shim::BaseTexture *d3dtex;
    char *name;
    uint32_t hash;
    int32_t ref;
    uint32_t dwSize;
    bool isCubeMap;
    bool loaded;
};

//-----------buffers-----------
struct VERTEX_BUFFER
{
    uint32_t dwNumLocks;
    uint32_t dwUsage;
    int32_t type;
    int32_t size;
    storm::shim::VertexBuffer *buff;
};

struct INDEX_BUFFER
{
    uint32_t dwNumLocks;
    uint32_t dwUsage;
    int32_t size;
    storm::shim::IndexBuffer *buff;
};

struct FONTEntity
{
    char *name;
    uint32_t hash;
    FONT *font;
    int32_t ref;
};

struct VideoTextureEntity
{
    char *name;
    uint32_t hash;
    int32_t ref;
    entid_t videoTexture_id;
    CVideoTexture *VideoTexture;
    VideoTextureEntity *next;
};

// ----- Sound statistics -----
extern uint32_t dwSoundBuffersCount;
extern uint32_t dwSoundBytes;
extern uint32_t dwSoundBytesCached;

class DX9RENDER_SCRIPT_LIBRIARY : public SCRIPT_LIBRIARY
{
  public:
    DX9RENDER_SCRIPT_LIBRIARY(){};

    ~DX9RENDER_SCRIPT_LIBRIARY() override{};
    bool Init() override;
};

//-----------SDEVICE-----------
class DX9RENDER : public VRenderer
{
#define RS_RECT_VERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

  public:
    static DX9RENDER *pRS;

    DX9RENDER();
    ~DX9RENDER() override;

    // DX9Render: Init/Release
    bool InitDevice(bool windowed, HWND hwnd, int32_t width, int32_t height) override;
    bool ReleaseDevice() override;

    // DX9Render: Animation
    void RenderAnimation(int32_t ib, void *src, int32_t numVrts, int32_t minv, int32_t numv, int32_t startidx, int32_t numtrg,
                         bool isUpdateVB) override;

    // DX9Render: Return d3d9 device
    virtual void *GetDevice()
    {
        return d3d9;
    };

    storm::shim::Device *GetD3DDevice() const
    {
        return d3d9;
    }
    uint32_t GetMaxActiveLights() override;

    // DX9Render: Render Target/Begin/End/Clear
    bool DX9Clear(int32_t type) override; // D3DCLEAR_STENCIL | D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER
    bool DX9BeginScene() override;
    bool DX9EndScene() override;

    // DX9Render: Materials/Lights Section
    bool SetLight(uint32_t dwIndex, const D3DLIGHT9 *pLight) override;
    bool LightEnable(uint32_t dwIndex, bool bOn) override;
    bool SetMaterial(D3DMATERIAL9 &material) override;
    bool GetLightEnable(uint32_t dwIndex, BOOL *pEnable) override;
    bool GetLight(uint32_t dwIndex, D3DLIGHT9 *pLight) override;

    // DX9Render: Clip Planes Section
    HRESULT SetClipPlane(uint32_t Index, const float *pPlane) override;
    PLANE *GetPlanes() override;

    // DX9Render: Camera Section
    void SetTransform(int32_t type, D3DMATRIX *mtx) override;
    void GetTransform(int32_t type, D3DMATRIX *mtx) override;

    bool SetCamera(const glm::vec3 &pos, const glm::vec3 &ang, float perspective) override;
    bool SetCamera(const glm::vec3 &pos, const glm::vec3 &ang) override;
    bool SetCamera(glm::vec3 lookFrom, glm::vec3 lookTo, glm::vec3 up) override;
    bool SetPerspective(float perspective, float fAspectRatio = -1.0f) override;
    void GetCamera(glm::vec3 &pos, glm::vec3 &ang, float &perspective) override;

    bool SetCurrentMatrix(D3DMATRIX *mtx) override;

    // DX9Render: Textures Section
    int32_t TextureCreate(const char *fname) override;
    int32_t TextureCreate(UINT width, UINT height, UINT levels, uint32_t usage, D3DFORMAT format, D3DPOOL pool) override;
    int32_t CubeTextureCreate(UINT edgeLength, UINT levels, uint32_t usage, D3DFORMAT format, D3DPOOL pool) override;
    int32_t VolumeTextureCreate(UINT width, UINT height, UINT depth, UINT levels, uint32_t usage, D3DFORMAT format,
                                D3DPOOL pool) override;
    bool TextureLockBox(int32_t texid, uint32_t level, D3DLOCKED_BOX &locked, uint32_t flags) override;
    void TextureUnlockBox(int32_t texid, uint32_t level) override;
    bool TextureLock(int32_t texid, uint32_t level, D3DLOCKED_RECT &locked, uint32_t flags,
                     uint32_t cubeFace = 0) override;
    void TextureUnlock(int32_t texid, uint32_t level, uint32_t cubeFace = 0) override;
    bool TextureLevelDesc(int32_t texid, uint32_t level, D3DSURFACE_DESC &desc) override;
    storm::render::DepthSurfaceId DepthSurfaceCreate(UINT width, UINT height, D3DFORMAT format) override;
    void DepthSurfaceRelease(storm::render::DepthSurfaceId surface) override;
    bool SetRenderTarget(int32_t texid, uint32_t level, uint32_t cubeFace,
                         storm::render::DepthSurfaceId depth) override;
    POINT GetRenderTargetSize() override;
    bool TextureSet(int32_t stage, int32_t texid) override;
    bool TextureRelease(int32_t texid) override;
    bool TextureIncReference(int32_t texid) override;

    // DX9Render: Fonts Section
    int32_t Print(int32_t x, int32_t y, const char *format, ...) override;
    int32_t Print(int32_t nFontNum, uint32_t color, int32_t x, int32_t y, const char *format, ...) override;
    int32_t ExtPrint(int32_t nFontNum, uint32_t foreColor, uint32_t backColor, int wAlignment, bool bShadow, float fScale,
                  int32_t scrWidth, int32_t scrHeight, int32_t x, int32_t y, const char *format, ...) override;
    int32_t StringWidth(const char *string, int32_t nFontNum = 0, float fScale = 1.f, int32_t scrWidth = 0) override;
    int32_t StringWidth(const std::string_view &string, int32_t nFontNum = 0, float fScale = 1.f, int32_t scrWidth = 0) override;
    int32_t CharWidth(utf8::u8_char, int32_t nFontNum = 0, float fScale = 1.f, int32_t scrWidth = 0) override;
    int32_t CharHeight(int32_t fontID) override;
    int32_t LoadFont(const char *fontName) override;   // returns the number \ font id, or -1 on error
    bool UnloadFont(const char *fontName) override; // returns true if the font is still in use
    bool UnloadFont(int32_t fontID) override;          // returns true if the font is still in use
    bool IncRefCounter(int32_t fontID) override;       // increase reference counter if object is being copied
    bool SetCurFont(const char *fontName) override; // returns true if the given font is installed
    bool SetCurFont(int32_t fontID) override;          // returns true if the given font is installed
    int32_t GetCurFont() override;
    char *GetFontIniFileName() override;
    bool SetFontIniFileName(const char *iniName) override;

    // DX9Render: Techniques Section
    bool TechniqueExecuteStart(const char *cBlockName) override;
    bool TechniqueExecuteNext() override;

    // DX9Render: Draw Section
    void DrawRects(RS_RECT *pRSR, uint32_t dwRectsNum, const char *cBlockName = nullptr, uint32_t dwSubTexturesX = 1,
                   uint32_t dwSubTexturesY = 1, float fScaleX = 1.0f, float fScaleY = 1.0f) override;
    void DrawSprites(RS_SPRITE *pRSS, uint32_t dwSpritesNum, const char *cBlockName = nullptr) override;
    void DrawLines(RS_LINE *pRSL, uint32_t dwLinesNum, const char *cBlockName = nullptr) override;
    void DrawLines2D(RS_LINE2D *pRSL2D, size_t dwLinesNum, const char *cBlockName = nullptr) override;

    void DrawBuffer(int32_t vbuff, int32_t stride, int32_t ibuff, int32_t minv, size_t numv, size_t startidx, size_t numtrg,
                    const char *cBlockName = nullptr) override;
    void DrawIndexedPrimitiveNoVShader(D3DPRIMITIVETYPE dwPrimitiveType, int32_t iVBuff, int32_t iStride, int32_t iIBuff,
                                       int32_t iMinV, int32_t iNumV, int32_t iStartIdx, int32_t iNumTrg,
                                       const char *cBlockName = nullptr) override;
    void DrawPrimitive(D3DPRIMITIVETYPE dwPrimitiveType, int32_t iVBuff, int32_t iStride, int32_t iStartV, int32_t iNumPT,
                       const char *cBlockName = nullptr) override;
    void DrawPrimitiveUP(D3DPRIMITIVETYPE dwPrimitiveType, uint32_t dwVertexBufferFormat, uint32_t dwNumPT,
                         const void *pVerts, uint32_t dwStride, const char *cBlockName = nullptr) override;
    void DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE dwPrimitiveType, uint32_t dwMinIndex, uint32_t dwNumVertices,
                                uint32_t dwPrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
                                const void *pVertexData, uint32_t dwVertexStride,
                                const char *cBlockName = nullptr) override;

    // DX9Render: Video Section
    void PlayToTexture() override;
    CVideoTexture *GetVideoTexture(const char *sVideoName) override;
    void ReleaseVideoTexture(CVideoTexture *pVTexture) override;

    // DX9Render: Vertex/Index Buffers Section
    int32_t CreateVertexBuffer(int32_t type, size_t nverts, uint32_t usage, uint32_t dwPool) override;
    int32_t CreateIndexBuffer(size_t ntrgs, uint32_t dwUsage) override;

    int32_t GetVertexBufferFVF(int32_t id) override;
    void *LockVertexBuffer(int32_t id, uint32_t dwFlags = 0) override;
    void UnLockVertexBuffer(int32_t id) override;
    int32_t GetVertexBufferSize(int32_t id) override;
    void *LockIndexBuffer(int32_t id, uint32_t dwFlags = 0) override;
    void UnLockIndexBuffer(int32_t id) override;
    void ReleaseVertexBuffer(int32_t id) override;
    void ReleaseIndexBuffer(int32_t id) override;

    // DX9Render: Render/Texture States Section
    uint32_t SetRenderState(uint32_t State, uint32_t Value) override;
    uint32_t GetRenderState(uint32_t State, uint32_t *pValue) override;
    uint32_t GetSamplerState(uint32_t Sampler, D3DSAMPLERSTATETYPE Type, uint32_t *pValue) override;
    uint32_t SetSamplerState(uint32_t Sampler, D3DSAMPLERSTATETYPE Type, uint32_t Value) override;
    uint32_t SetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t Value) override;
    uint32_t GetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t *pValue) override;

    // aspect ratio section
    float GetHeightDeformator() override
    {
        return m_fHeightDeformator;
    }

    POINT GetScreenSize() override
    {
        return screen_size;
    }

    // ===============================================================================================
    // --------------------===================== D3D SECTION =====================--------------------
    // ===============================================================================================

    // D3D Device/Viewport Section
    HRESULT GetViewport(D3DVIEWPORT9 *pViewport) override;
    HRESULT SetViewport(const D3DVIEWPORT9 *pViewport) override;

    // D3D
    void SetStreamSource(UINT StreamNumber, int32_t vertexBuffer, UINT Stride) override;
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override;
    HRESULT Release(storm::shim::Object *pSurface);

    // Vertex/Index Buffers Section
    HRESULT CreateVertexBuffer(UINT Length, uint32_t Usage, uint32_t fvf, D3DPOOL Pool,
                               storm::shim::VertexBuffer **ppVertexBuffer);

    HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool,
                          storm::shim::Texture2D **ppTexture);
    HRESULT CreateCubeTexture(UINT EdgeLength, UINT Levels, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool,
                              storm::shim::CubeTexture **ppCubeTexture);
    HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                      storm::shim::Surface **ppSurface);
    HRESULT SetTexture(uint32_t Stage, storm::shim::BaseTexture *pTexture);
    HRESULT GetRenderTargetData(storm::shim::Surface *pRenderTarget, storm::shim::Surface *pDestSurface);

    // D3D Pixel/Vertex Shaders Section
    storm::render::VertexDeclarationId CreateVertexDeclaration(const D3DVERTEXELEMENT9 *pVertexElements) override;
    void SetVertexDeclaration(storm::render::VertexDeclarationId declaration) override;
    HRESULT CreatePixelShader(const uint32_t *pFunction, storm::shim::PixelShader **ppShader);
    HRESULT CreateVertexShader(const uint32_t *pFunction, storm::shim::VertexShader **ppShader);
    HRESULT SetVertexShader(storm::shim::VertexShader *pShader);
    HRESULT SetPixelShader(storm::shim::PixelShader *pShader);
    /*virtual HRESULT SetFVFConstant(DWORD Register, const void* pConstantData, DWORD  ConstantCount );*/
    HRESULT SetVertexShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4iCount) override;
    HRESULT SetPixelShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4iCount) override;
    HRESULT SetFVF(uint32_t handle) override;
    HRESULT GetVertexShader(storm::shim::VertexShader **ppShader);
    HRESULT GetPixelShader(storm::shim::PixelShader **ppShader);

    // D3D Render Target/Begin/End/Clear
    HRESULT GetRenderTarget(storm::shim::Surface **ppRenderTarget);
    HRESULT SetRenderTarget(storm::shim::Surface *pRenderTarget, storm::shim::Surface *pNewZStencil);
    HRESULT Clear(uint32_t Count, const D3DRECT *pRects, uint32_t Flags, D3DCOLOR Color, float Z,
                  uint32_t Stencil) override;
    HRESULT BeginScene() override;
    HRESULT EndScene() override;

    HRESULT ImageBlt(const char *pName, RECT *pDstRect, RECT *pSrcRect) override;
    HRESULT ImageBlt(int32_t nTextureId, RECT *pDstRect, RECT *pSrcRect) override;

    bool LoadTextureSurface(std::fstream &fileS, storm::shim::Surface *suface, uint32_t mipSize, uint32_t width,
                            uint32_t height, bool isSwizzled);
    uint32_t LoadCubmapSide(std::fstream &fileS, storm::shim::CubeTexture *tex, D3DCUBEMAP_FACES face, uint32_t numMips,
                            uint32_t mipSize, uint32_t size, bool isSwizzled);

    // core interface
    bool Init() override;
    void RunStart() override;
    void RunEnd() override;

    uint32_t RunSection() override
    {
        return SECTION_REALIZE;
    };
    bool LoadState(ENTITY_STATE *state) override;
    bool CreateState(ENTITY_STATE_GEN *state_gen) override;

    void ProcessScriptPosAng(const glm::vec3 &vPos, const glm::vec3 &vAng);
    void FindPlanes(storm::shim::Device *d3dDevice);

    void SetCommonStates();
    void SetProgressImage(const char *image) override;
    void SetProgressBackImage(const char *image) override;
    void SetTipsImage(const char *image) override;
    void StartProgressView() override;
    void ProgressView() override;
    void EndProgressView() override;

    static const uint32_t rectsVBuffer_SizeInRects;
    storm::shim::VertexBuffer *rectsVBuffer;

    char *progressImage;
    int32_t progressImageSize;
    int32_t backTexture;
    char *progressBackImage;
    int32_t progressBackImageSize;
    int32_t back0Texture;
    int32_t progressTexture;
    char *progressTipsImage;
    int32_t progressTipsImageSize;
    int32_t progressTipsTexture;

    int32_t loadFrame;
    int32_t progressSafeCounter;
    bool isInPViewProcess;
    uint32_t progressUpdateTime;
    float progressFramesPosX;
    float progressFramesPosY;
    float progressFramesWidth;
    float progressFramesHeight;
    int32_t progressFramesCountX;
    int32_t progressFramesCountY;

    // new renderer settings
    bool vSyncEnabled;
    uint32_t msaa;

    D3DPRESENT_PARAMETERS d3dpp;

    glm::mat4 mView, mWorld, mProjection;

    glm::vec3 vWordRelationPos;
    glm::vec3 vViewRelationPos;

    bool bUseLargeBackBuffer;

    bool resourcesReleased = false;

    bool IsInsideScene() override
    {
        return bInsideScene;
    }

    char *GetTipsImage() override;

    void SetColorParameters(float fGamma, float fBrightness, float fContrast) override;
    void DrawSphere(const glm::vec3 &vPos, float fRadius, uint32_t dwColor) override;
    void DrawEllipsoid(const glm::vec3 &vPos, float a, float b, float c, float ay, uint32_t dwColor) override;

    void GetNearFarPlane(float &fNear, float &fFar) override;
    void SetNearFarPlane(float fNear, float fFar) override;

    void SetLoadTextureEnable(bool bEnable = true) override;
    bool ResetDevice();

    void MakeDrawVector(RS_LINE *pLines, uint32_t dwNumSubLines, const glm::mat4 &mMatrix, glm::vec3 vUp, glm::vec3 v1,
                        glm::vec3 v2, float fScale, uint32_t dwColor);
    void DrawVector(const glm::vec3 &v1, const glm::vec3 &v2, uint32_t dwColor,
                    const char *pTechniqueName = "DXVector") override;

    bool PushRenderTarget() override;
    bool PopRenderTarget() override;
    bool SetRenderTarget(storm::shim::CubeTexture *pCubeTex, uint32_t dwFaceType, uint32_t dwLevel,
                         storm::shim::Surface *pNewZStencil);
    void SetView(const glm::mat4 &mView) override;
    void SetWorld(const glm::mat4 &mView) override;
    void SetProjection(const glm::mat4 &mView) override;
    const glm::mat4 &GetView() override;
    const glm::mat4 &GetWorld() override;
    const glm::mat4 &GetProjection() override;


    void MakePostProcess() override;
    void SetGLOWParams(float _fBlurBrushSize, int32_t _GlowIntensity, int32_t _GlowPasses) override;

    storm::shim::BaseTexture *GetTextureFromID(int32_t nTextureID);

  private:
    std::unique_ptr<storm::GamePictures> gamePictures_;
    int appEventsSubscription_ = -1;

    struct VertexDeclaration
    {
        std::vector<D3DVERTEXELEMENT9> elements; // with D3DDECL_END
        storm::shim::VertexDeclaration *declaration;
    };
    std::vector<VertexDeclaration> vertexDeclarations_; // indexed by VertexDeclarationId
    std::vector<storm::shim::Surface *> depthSurfaces_;     // indexed by DepthSurfaceId, null when released

    // Puts a code-made texture into a free slot of Textures; -1 (logged, texture released) when full.
    int32_t AddCodeTexture(storm::shim::BaseTexture *texture, uint32_t size, bool cubeMap);
    // The texture of a valid id, or null (logged) for an invalid, released or file-less id.
    storm::shim::BaseTexture *CodeTexture(int32_t texid, const char *operation);

  public:
    bool keepTexturePixels(int32_t texture) override;
    bool sampleTextureArgb(int32_t texture,float u,float v,uint32_t& result) override;

    int32_t GetRenderTargetAsTexture() override;

    void LostRender();
    void RestoreRender();


private:
    struct RECT_VERTEX
    {
        glm::vec3 pos;
        uint32_t color;
        float u, v;
    };

    struct RenderTarget
    {
        storm::shim::Surface *pRenderTarget;
        storm::shim::Surface *pDepthSurface;
        D3DVIEWPORT9 ViewPort;
    };

    storm::shim::Device *d3d9;
    storm::shim::Direct3D *d3d;
    HWND hwnd;

    glm::vec3 Pos, Ang;
    float Fov;

    float FovMultiplier{ 1.0f };

    std::unique_ptr<CTechnique> pTechnique;

    char *fontIniFileName;
    int32_t nFontQuantity;
    FONTEntity FontList[MAX_FONTS]{};
    int32_t idFontCurrent;

    VideoTextureEntity *pVTL;

    int32_t nTextureDegradation;
    float aspectRatio;
    float m_fHeightDeformator;

    bool bShowFps, bShowExInfo;
    // Opt-in presentation cadence diagnostics; these are not GPU timings.
    bool renderMetricsEnabled = false;
    double metricsPreviousPresent = 0.0;
    double metricsElapsed = 0.0;
    double metricsPresentTime = 0.0;
    uint64_t metricsDrawCalls = 0;
    std::vector<double> metricsFrameTimes;
    storm::render::PerfCounters metricsPerfStart; // shim counters at the start of the metrics window
    bool bInsideScene;

    PLANE viewplane[4];

    STEXTURE Textures[MAX_STEXTURES]{};
    struct KeptPixels
    {
        std::vector<uint8_t> bytes;
        uint32_t width = 0, height = 0;
        bool opaque = false; // X8R8G8B8
    };
    std::unordered_map<int32_t, KeptPixels> keptPixels_; // keepTexturePixels, by texture ID
    // The buffer handle table (LiveBuffer). -1 is "no buffer"; the table is never indexed with it.
    bool LiveBuffer(int32_t id, bool index, const char *call) const;
    INDEX_BUFFER IndexBuffers[MAX_BUFFERS]{};
    VERTEX_BUFFER VertexBuffers[MAX_BUFFERS]{};

    bool MakeAvi;
    storm::shim::Surface *ImageBuffer;

    //-------- post process

    struct QuadVertex
    {
        glm::vec4 vPos;

        float u0;
        float v0;

        float u1;
        float v1;

        float u2;
        float v2;

        float u3;
        float v3;
    };

    QuadVertex PostProcessQuad[4];
    QuadVertex qv[32 * 32];
    uint16_t qi[31 * 31 * 2 * 3];

    float fSmallWidth;
    float fSmallHeight;
    storm::shim::Texture2D *pPostProcessTexture;
    storm::shim::Surface *pPostProcessSurface;

    storm::shim::Texture2D *pSmallPostProcessTexture;
    storm::shim::Surface *pSmallPostProcessSurface;

    storm::shim::Texture2D *pSmallPostProcessTexture2;
    storm::shim::Surface *pSmallPostProcessSurface2;

    storm::shim::Surface *pOriginalScreenSurface;
    storm::shim::Surface *pOriginalDepthSurface;

    D3DVIEWPORT9 OriginalViewPort;

    void CreateRenderQuad(float fWidth, float fHeight, float fSrcWidth, float fSrcHeight, float fMulU = 1.0f,
                          float fMulV = 1.0f);

    void ClearPostProcessSurface(storm::shim::Surface *pSurf);
    void BlurGlowTexture();
    void CopyGlowToScreen();
    void CopyPostProcessToScreen();

    void SetPostProcessTextureAsRenderTarget();
    void SetScreenAsRenderTarget();

    float fBlurSize;
    int GlowIntensity;
    int iBlurPasses;

    bool bNeedCopyToScreen;

    bool bPostProcessEnabled;
    bool bPostProcessError;

    bool bSeaEffect;
    float fSeaEffectSize;
    float fSeaEffectSpeed;
    uint32_t dwBackColor;

    //-------- post process

    // state save/load ex
    POINT screen_size;
    D3DFORMAT screen_bpp;
    D3DFORMAT stencil_format;

    bool bWindow;
    bool bBackBufferCanLock;

    storm::shim::VertexBuffer *aniVBuffer;
    int32_t numAniVerteces;

    storm::shim::VertexBuffer *pDropConveyorVBuffer;

    uint32_t dwNumDrawPrimitive, dwNumLV, dwNumLI;
    float fG, fB, fC;

    float fNearClipPlane, fFarClipPlane;

    bool bLoadTextureEnabled;

    bool bTrace;
    int32_t iSetupPath;
    uint64_t dwSetupNumber;
    texpaths_t TexPaths[4]{};

    bool bDropVideoConveyor;

    std::stack<RenderTarget> stRenderTarget;


    bool TextureLoad(int32_t texid);
};
