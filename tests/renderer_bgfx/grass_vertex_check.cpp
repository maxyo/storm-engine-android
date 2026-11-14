// Raster regression for the grass's packed SHORT4 input. Exercises the actual grass shader and both the first
// transient upload and retained vertex buffer. Integer bytes must not be bound to a float shader attribute.
#include "device.hpp"
#include "ports.hpp"
#include "check_backend.hpp"
#include "../../src/libs/location/src/grass_pack.hpp"
#include <SDL2/SDL.h>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

using namespace storm;
namespace {
constexpr unsigned size = 64;
void check(HRESULT value) { if (FAILED(value)) { std::fprintf(stderr, "D3D failure %x\n", unsigned(value)); std::exit(2); } }
}
int main()
{
    if (SDL_Init(SDL_INIT_VIDEO)) return 2;
    host_check::UseBackendFromEnvironment();
    auto *window = SDL_CreateWindow("grass_vertex_check", 0, 0, size, size, 0);
    if (!window || !shim::Context::instance().init(window, size, size, false)) return 2;
    bool ok = true;
    {
        D3DPRESENT_PARAMETERS params{}; params.BackBufferWidth = params.BackBufferHeight = size;
        auto *d = new shim::Device(window, params);
        shim::Surface *target = nullptr, *readback = nullptr, *back = nullptr;
        check(d->CreateRenderTarget(size, size, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr));
        check(d->CreateOffscreenPlainSurface(size, size, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr));
        check(d->GetRenderTarget(0, &back));
        auto *shader = new shim::VertexShader;
        shader->port = shim::find_port("grass_main", true);
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
        shim::VertexDeclaration *decl = nullptr; check(d->CreateVertexDeclaration(elements, &decl));
        shim::VertexBuffer *vb = nullptr; shim::IndexBuffer *ib = nullptr;
        check(d->CreateVertexBuffer(4 * sizeof(grass_pack::Vertex), D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &vb, nullptr));
        check(d->CreateIndexBuffer(6 * sizeof(uint32_t), D3DUSAGE_DYNAMIC, D3DFMT_INDEX32, D3DPOOL_DEFAULT, &ib, nullptr));
        const std::array<uint32_t, 6> indices{0, 1, 2, 2, 1, 3};
        void *data = nullptr; check(ib->Lock(0, sizeof(indices), &data, D3DLOCK_DISCARD));
        std::memcpy(data, indices.data(), sizeof(indices)); check(ib->Unlock());
        // Signs of x/z encode all four corners; height uses a negative signed short; angle9 makes data negative.
        std::array<grass_pack::Vertex, 4> vertices;
        const grass_pack::Window area{10, 20, 12, 22};
        for (int c = 0; c < 4; ++c)
            vertices[c] = grass_pack::pack(area, 10, 20, 0, 0, 0, 3, grass_pack::pack_data(4*17, 15*17, 15*17, 9*17), c);
        std::printf("input SHORT4 bytes:");
        for (unsigned char b : std::bit_cast<std::array<unsigned char, sizeof(vertices)>>(vertices)) std::printf("%02x", b);
        std::puts("");
        check(vb->Lock(0, sizeof(vertices), &data, D3DLOCK_DISCARD));
        std::memcpy(data, vertices.data(), sizeof(vertices)); check(vb->Unlock());
        std::array<std::array<float, 4>, 82> c{};
        for (int i = 0; i < 16; ++i) { c[i] = {std::sin(i * 6.2831853f/16),std::cos(i * 6.2831853f/16),1,1}; }
        c[32] = {1,0,0,0}; c[33] = {0,1,0,0}; c[34] = {0,0,0,0}; c[35] = {0,-0.5f,0.5f,1};
        c[37] = {1,1,1,1}; c[38] = {0,0,0,1}; c[39] = {0.9f,1,0.245f,-0.245f};
        c[40] = {15,-0.5f,1,0.8f}; c[41] = {0.3f,0.4f,0.7f,0.6f};
        c[46] = {0,0,2,0}; c[47] = {-10,-20,0,0}; c[48] = {10,50,0,0}; c[49] = {1,10,20,0};
        check(d->SetVertexShaderConstantF(0,c[0].data(),82));
        for (int frame = 0; frame < 3; ++frame)
        {
            check(d->BeginScene()); check(d->SetRenderTarget(0,target)); check(d->SetDepthStencilSurface(nullptr));
            check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));
            D3DMATRIX identity{}; for (int i=0;i<4;++i) identity.m[i][i]=1;
            d->SetTransform(D3DTS_WORLD,&identity); d->SetTransform(D3DTS_VIEW,&identity); d->SetTransform(D3DTS_PROJECTION,&identity);
            d->SetRenderState(D3DRS_ZENABLE,FALSE); d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
            d->SetRenderState(D3DRS_LIGHTING,FALSE); d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE); d->SetRenderState(D3DRS_TEXTUREFACTOR,0xff20e040);
            d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1); d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TFACTOR);
            d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1); d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TFACTOR);
            d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE); d->SetTexture(0,nullptr);
            d->SetVertexShader(shader); d->SetVertexDeclaration(decl); d->SetStreamSource(0,vb,0,sizeof(grass_pack::Vertex)); d->SetIndices(ib);
            check(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2)); check(d->EndScene());
            check(d->GetRenderTargetData(target,readback));
            D3DLOCKED_RECT locked{}; check(readback->LockRect(&locked,nullptr,D3DLOCK_READONLY));
            unsigned pixels=0;
            for (unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
                const auto *p=static_cast<const unsigned char*>(locked.pBits)+y*locked.Pitch+4*x;
                pixels += p[1]>180 && p[0]<100 && p[2]<100;
            }
            check(readback->UnlockRect()); check(d->SetRenderTarget(0,back)); check(d->Present());
            // A1m wide/upright blade under this projection covers32x32 pixels. No wind; quality2; all four corners.
            bool pass=pixels>=900 && pixels<=1156;
            std::printf("%s grass packed raster frame%d: %u green pixels (expected900..1156)\n",pass?"PASS":"FAIL",frame,pixels);
            ok &= pass;
        }
        d->SetVertexShader(nullptr); d->SetStreamSource(0,nullptr,0,0); d->SetIndices(nullptr); d->SetVertexDeclaration(nullptr);
        shader->Release(); decl->Release(); vb->Release(); ib->Release(); target->Release(); readback->Release(); back->Release(); d->Release();
    }
    SDL_DestroyWindow(window); SDL_Quit(); return ok?0:1;
}
