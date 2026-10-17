#include <platform/android_interface_presentation.hpp>
#include <cassert>
#include <cstring>
#include <optional>
#include <thread>

// Compile actual renderer/font methods against a counting GPU to check that
// suppression preserves techniques, font metrics and buffer-update work.
using D3DPRIMITIVETYPE = int;
constexpr int D3DPT_TRIANGLELIST = 0, FONT_CHAR_FVF = 0;
constexpr int D3DRS_SRCBLEND = 0, D3DRS_DESTBLEND = 1;
constexpr int D3DBLEND_ZERO = 0, D3DBLEND_INVSRCALPHA = 1, D3DBLEND_SRCALPHA = 2;
#define CHECKD3DERR(value) (value)
struct FONT_CHAR_VERTEX { int unused; };
struct Device
{
    int draws = 0;
    void SetFVF(int) {}
    void SetStreamSource(int, void *, int, int) {}
    void SetRenderState(int, int) {}
    int DrawPrimitiveUP(int, uint32_t, const void *, uint32_t) { ++draws; return 0; }
    void DrawPrimitive(int, int, int) { ++draws; }
};
struct DX9RENDER
{
    Device *d3d9;
    int dwNumDrawPrimitive = 0, passes = 0, starts = 0, fvfSets = 0;
    int SetFVF(uint32_t) { ++fvfSets; return 0; }
    bool TechniqueExecuteStart(const char *) { ++starts; passes = 0; return true; }
    bool TechniqueExecuteNext() { return ++passes < 3; }
    void TextureSet(int, int) {}
    void DrawPrimitiveUP(D3DPRIMITIVETYPE, uint32_t, uint32_t, const void *, uint32_t, const char *);
};
struct FONT_PRINT_OVERRIDES
{
    std::optional<bool> shadow;
    std::optional<float> scale;
    std::optional<uint32_t> color;
};
namespace utf8 { int Utf8StringLength(const char *s) { return std::strlen(s); } }
struct FONT
{
    DX9RENDER &renderService_;
    Device &device_;
    std::string techniqueName_ = "font";
    int textureHandle_ = 0;
    void *vertexBuffer_ = nullptr;
    bool drawShadows_ = true;
    float scale_ = 1;
    uint32_t color_ = 0;
    int shadowOffsetX_ = 1, shadowOffsetY_ = 1, updates = 0;
    int UpdateVertexBuffer(int, int, char *, int count, float, uint32_t) { ++updates; return count * 9; }
    int32_t Print(int32_t, int32_t, char *, const FONT_PRINT_OVERRIDES &);
};
#include "renderer.inc"
#include "font.inc"

int main()
{
    using namespace storm::android;
    InterfacePresentation state;
    state.begin(1, true, false); // Existing single-argument Java apps keep native UI.
    assert(!state.suppressed() && !state.offer("1:mobile"));
    state.begin(2, true, true);
    assert(state.suppressed()); // No Android ack/snapshot has arrived yet.
    assert(!state.offer("1:original") && state.suppressed());
    for (const auto *bad : {"2:", "2:original:extra", "2:original\n", "02:original",
                            "+2:original", "18446744073709551618:original", "2:unknown"})
        assert(!state.offer(bad) && state.suppressed());
    assert(state.offer("2:original") && !state.suppressed());
    state.begin(3, true, true); // Reopening even the same screen revokes debug mode.
    assert(state.suppressed() && !state.offer("2:original"));
    assert(state.offer("3:fallback") && !state.suppressed());
    assert(state.offer("3:mobile") && state.suppressed());
    state.begin(4, false, true); // Gameplay never remains suppressed.
    assert(!state.suppressed() && !state.offer("4:original"));
    state.begin(5, true, true);
    assert(state.suppressed());

    Device device;
    DX9RENDER renderer{&device};
    FONT font{renderer, device};
    char text[] = "hello";
    auto frame = [&] {
        renderer.DrawPrimitiveUP(0, 0, 1, nullptr, 0, "interface");
        assert(renderer.passes == 3); // Technique cleanup still occurs.
        assert(font.Print(0, 0, text, {}) == 45); // Layout measurements survive.
        assert(renderer.passes == 3);
    };
    frame();
    assert(device.draws == 5 && renderer.dwNumDrawPrimitive == 3 && font.updates == 2);
    {
        NativeInterfaceDrawScope scope(state.suppressed());
        assert(NativeInterfaceDrawSuppressed());
        frame();
        assert(device.draws == 5 && renderer.dwNumDrawPrimitive == 3 && font.updates == 4);
        assert(renderer.fvfSets == 2 && renderer.starts == 4);
        {
            NativeInterfaceDrawScope nested(false);
            assert(NativeInterfaceDrawSuppressed());
        }
        assert(NativeInterfaceDrawSuppressed());
        std::thread other([] { assert(!NativeInterfaceDrawSuppressed()); });
        other.join();
    }
    assert(!NativeInterfaceDrawSuppressed());
    frame(); // Subsequent world rendering is unaffected.
    assert(device.draws == 10 && renderer.dwNumDrawPrimitive == 6 && font.updates == 6);
    try
    {
        NativeInterfaceDrawScope scope(true);
        throw 1;
    }
    catch (int) {}
    assert(!NativeInterfaceDrawSuppressed());
}
