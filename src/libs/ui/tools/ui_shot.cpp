// Headless capture of a UI document for design checks, without a device:
//
//   storm_ui_shot --pack DIR --doc gallery/buttons.rml --size 915x412 --density 2 --out shot.png
//                 [--game-root DIR] [--safe L,T,R,B] [--script "down 1 120 80; wait 0.5; shot held.png; up 1 120 80"]
//   script commands: down|move|up <pointer> <x> <y>, wait <s>, shot <png>, toast <text>,
//                    props <element id> <json>, scroll <element id>
//
// A game interface instead of a document: --interface model.json renders a recorded
// interface model through the screen routing (--state dialogue=state.json feeds a state
// channel). Extra script commands: interface <model.json> (a newer revision),
// text <answer> (accepts the pending text editor request). Engine actions are printed as
// "command <engine queue line>".
//
// The HUD: --state channel=file.json, repeated, feeds recorded state channels in the
// order given (gameplay, combat, vitals, worldInfo, contextAction, landCommands). The
// channel "presentation" is the platform host's; without one the capture uses an idle game
// in front ({"screen":"","loading":false,"cinematic":false,"dialogue":false,"live":true})
// and says so. Extra script commands: state <channel> <file.json>, back (the back key),
// cancel (the platform takes the gestures away), wants <x> <y> (prints whether a pointer
// going down there belongs to the UI). HUD input is printed as "command <line>" and, when
// it uses a latest-value slot, "command[<slot>] <line>".
//
// Frame cost: bench <frames> [<channel> <a.json> <b.json>] runs that many frames as a
// platform host does (Host::Prepare, and Host::Frame only for an out-of-date frame), each
// finished on the GPU (glFinish), and prints what they cost (Host::LastFrameStats: CPU of
// update and render, GPU wait, rebuilds, layout, meshes, draw calls, texture uploads).
// With a channel the two states alternate every frame (a HUD that changes each frame);
// the channel "interface" alternates two interface models instead.
//
// Sizes and script coordinates are in dp. The clock is simulated (60 frames a second),
// so captures are repeatable. Reported actions go to stdout as "action id verb value".
#include "storm_ui/render_backend.h"
#include "storm_ui/ui.h"

#include <plutovg.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

// The few EGL entry points needed for an off-screen desktop GL context. Declared here
// because the build host has the EGL runtime but not necessarily its headers.
using EGLDisplay = void *;
using EGLConfig = void *;
using EGLSurface = void *;
using EGLContext = void *;
using EGLint = int;
constexpr EGLint EGL_NONE = 0x3038, EGL_SURFACE_TYPE = 0x3033, EGL_PBUFFER_BIT = 0x0001, EGL_RENDERABLE_TYPE = 0x3040;
constexpr EGLint EGL_OPENGL_BIT = 0x0008, EGL_RED_SIZE = 0x3024, EGL_GREEN_SIZE = 0x3023, EGL_BLUE_SIZE = 0x3022, EGL_ALPHA_SIZE = 0x3021;
constexpr EGLint EGL_STENCIL_SIZE = 0x3026, EGL_WIDTH = 0x3057, EGL_HEIGHT = 0x3056, EGL_OPENGL_API = 0x30A2;
constexpr EGLint EGL_CONTEXT_MAJOR_VERSION = 0x3098, EGL_CONTEXT_MINOR_VERSION = 0x30FB, EGL_CONTEXT_OPENGL_PROFILE_MASK = 0x30FD;
constexpr EGLint EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT = 0x00000001, EGL_PLATFORM_SURFACELESS_MESA = 0x31DD;

struct Egl
{
    EGLDisplay (*GetDisplay)(void *) = nullptr;
    EGLDisplay (*GetPlatformDisplay)(unsigned, void *, const intptr_t *) = nullptr;
    unsigned (*Initialize)(EGLDisplay, EGLint *, EGLint *) = nullptr;
    unsigned (*BindAPI)(unsigned) = nullptr;
    unsigned (*ChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) = nullptr;
    EGLSurface (*CreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint *) = nullptr;
    EGLContext (*CreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *) = nullptr;
    unsigned (*MakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
    void *(*GetProcAddress)(const char *) = nullptr;

    bool Load()
    {
        void *library = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
        if (!library)
            return false;
        const auto find = [library](const char *name) { return dlsym(library, name); };
        GetDisplay = reinterpret_cast<decltype(GetDisplay)>(find("eglGetDisplay"));
        GetPlatformDisplay = reinterpret_cast<decltype(GetPlatformDisplay)>(find("eglGetPlatformDisplay"));
        Initialize = reinterpret_cast<decltype(Initialize)>(find("eglInitialize"));
        BindAPI = reinterpret_cast<decltype(BindAPI)>(find("eglBindAPI"));
        ChooseConfig = reinterpret_cast<decltype(ChooseConfig)>(find("eglChooseConfig"));
        CreatePbufferSurface = reinterpret_cast<decltype(CreatePbufferSurface)>(find("eglCreatePbufferSurface"));
        CreateContext = reinterpret_cast<decltype(CreateContext)>(find("eglCreateContext"));
        MakeCurrent = reinterpret_cast<decltype(MakeCurrent)>(find("eglMakeCurrent"));
        GetProcAddress = reinterpret_cast<decltype(GetProcAddress)>(find("eglGetProcAddress"));
        return GetDisplay && Initialize && BindAPI && ChooseConfig && CreatePbufferSurface && CreateContext && MakeCurrent && GetProcAddress;
    }
};

bool MakeContext(Egl &egl, int width, int height)
{
    EGLDisplay display = egl.GetPlatformDisplay ? egl.GetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr) : nullptr;
    if (!display || !egl.Initialize(display, nullptr, nullptr))
    {
        display = egl.GetDisplay(nullptr);
        if (!display || !egl.Initialize(display, nullptr, nullptr))
            return false;
    }
    if (!egl.BindAPI(EGL_OPENGL_API))
        return false;
    const EGLint config_attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                        EGL_BLUE_SIZE,    8,               EGL_ALPHA_SIZE,      8,              EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!egl.ChooseConfig(display, config_attributes, &config, 1, &count) || count < 1)
        return false;
    const EGLint surface_attributes[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    EGLSurface surface = egl.CreatePbufferSurface(display, config, surface_attributes);
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                         EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext context = egl.CreateContext(display, config, nullptr, context_attributes);
    return surface && context && egl.MakeCurrent(display, surface, surface, context);
}

using ReadPixelsFn = void (*)(int, int, int, int, unsigned, unsigned, void *);

bool SavePng(const Egl &egl, int width, int height, const std::string &path)
{
    const auto read_pixels = reinterpret_cast<ReadPixelsFn>(egl.GetProcAddress("glReadPixels"));
    if (!read_pixels)
        return false;
    std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
    read_pixels(0, 0, width, height, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, rgba.data());
    plutovg_surface_t *surface = plutovg_surface_create(width, height);
    unsigned char *data = plutovg_surface_get_data(surface);
    const int stride = plutovg_surface_get_stride(surface);
    for (int y = 0; y < height; ++y)
    {
        const unsigned char *source = &rgba[static_cast<size_t>(height - 1 - y) * width * 4]; // GL rows run bottom-up
        auto *target = reinterpret_cast<uint32_t *>(data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < width; ++x) // premultiplied RGBA → premultiplied ARGB32
            target[x] = static_cast<uint32_t>(source[x * 4 + 3]) << 24 | static_cast<uint32_t>(source[x * 4]) << 16 |
                        static_cast<uint32_t>(source[x * 4 + 1]) << 8 | source[x * 4 + 2];
    }
    const bool ok = plutovg_surface_write_to_png(surface, path.c_str());
    plutovg_surface_destroy(surface);
    return ok;
}

std::string ReadFile(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char **argv)
{
    std::string pack, doc, out = "shot.png", game_root, script, interface, text_token;
    std::vector<std::string> states;
    int width_dp = 915, height_dp = 412;
    float density = 2.0f;
    float safe[4] = {0, 0, 0, 0}; // safe-area insets in dp
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--pack")
            pack = value;
        else if (key == "--doc")
            doc = value;
        else if (key == "--out")
            out = value;
        else if (key == "--game-root")
            game_root = value;
        else if (key == "--script")
            script = value;
        else if (key == "--interface")
            interface = value;
        else if (key == "--state")
            states.push_back(value);
        else if (key == "--safe" && std::sscanf(value.c_str(), "%f,%f,%f,%f", &safe[0], &safe[1], &safe[2], &safe[3]) == 4)
            continue;
        else if (key == "--density")
            density = std::strtof(value.c_str(), nullptr);
        else if (key == "--size" && std::sscanf(value.c_str(), "%dx%d", &width_dp, &height_dp) == 2)
            continue;
        else
        {
            std::fprintf(stderr, "unknown option %s\n", key.c_str());
            return 2;
        }
    }
    if (pack.empty() || (doc.empty() && interface.empty() && states.empty()))
    {
        std::fprintf(stderr, "usage: storm_ui_shot --pack DIR (--doc FILE | --interface JSON | --state CHANNEL=JSON …) [--size WxH] [--density D] [--out PNG] [--game-root DIR] [--safe L,T,R,B] [--script S]\n");
        return 2;
    }
    const int width = static_cast<int>(static_cast<float>(width_dp) * density), height = static_cast<int>(static_cast<float>(height_dp) * density);

    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0); // same pixels on every machine
    Egl egl;
    if (!egl.Load() || !MakeContext(egl, width, height))
    {
        std::fprintf(stderr, "no off-screen OpenGL 3.3 context (EGL)\n");
        return 3;
    }

    double clock = 0.0;
    const auto files = storm::ui::DirectoryFiles(pack);
    std::string error;
    const auto backend = storm::ui::CreateGl3Backend(&error);
    if (!backend)
    {
        std::fprintf(stderr, "GL backend: %s\n", error.c_str());
        return 4;
    }
    storm::ui::Config config;
    config.files = files.get();
    config.backend = backend.get();
    config.show_errors = true;
    config.verify_unchanged = true;
    config.width = width;
    config.height = height;
    config.density = density;
    config.game_root = game_root;
    config.clock = [&clock] { return clock; };
    config.on_action = [](const storm::ui::Action &action) {
        if (action.screen.empty())
            std::printf("action %s %s %s\n", action.node.c_str(), action.verb.c_str(), action.value.c_str());
        else if (action.screen == "hud" && !action.node.empty())
            std::printf("command[%s] %s\n", action.node.c_str(), storm::ui::EngineCommand(action).c_str());
        else
            std::printf("command %s\n", storm::ui::EngineCommand(action).c_str());
    };
    config.on_text_request = [&text_token](const storm::ui::TextRequest &request) {
        text_token = request.token;
        std::printf("text-request \"%s\" \"%s\" at %.0f,%.0f %.0fx%.0f\n", request.title.c_str(), request.text.c_str(), request.x, request.y,
                    request.width, request.height);
    };
    auto host = storm::ui::Host::Create(config, &error);
    if (!host)
    {
        std::fprintf(stderr, "UI host: %s\n", error.c_str());
        return 4;
    }
    host->SetSafeArea(safe[0], safe[1], safe[2], safe[3]);
    if (!doc.empty() ? !host->LoadDocument(doc) : !interface.empty() && !host->SetInterface(ReadFile(interface)))
    {
        std::fprintf(stderr, "cannot load %s\n", (doc + interface).c_str());
        return 5;
    }
    bool presented = false;
    for (const std::string &pair : states)
    {
        const size_t equals = pair.find('=');
        if (equals == std::string::npos || !host->SetState(pair.substr(0, equals), ReadFile(pair.substr(equals + 1))))
        {
            std::fprintf(stderr, "cannot feed state %s\n", pair.c_str());
            return 5;
        }
        presented = presented || pair.substr(0, equals) == "presentation";
    }
    if (!states.empty() && !presented && states.front().rfind("dialogue=", 0) != 0)
    {
        std::fprintf(stderr, "no presentation state given: capturing an idle game in front\n");
        host->SetState("presentation", R"({"screen":"","loading":false,"cinematic":false,"dialogue":false,"live":true})");
    }
    const auto run = [&](double seconds) {
        const int frames = std::max(1, static_cast<int>(seconds * 60.0 + 0.5));
        for (int i = 0; i < frames; ++i)
        {
            clock += 1.0 / 60.0;
            host->Frame();
        }
    };
    run(0.05);
    if (!game_root.empty())
    {
        // Pictures decode on a worker thread: give them real time, then settle.
        for (int i = 0; i < 40; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            run(1.0 / 60.0);
        }
    }

    int status = 0;
    std::stringstream commands(script);
    std::string command;
    while (std::getline(commands, command, ';'))
    {
        std::stringstream words(command);
        std::string verb;
        words >> verb;
        if (verb == "down" || verb == "move" || verb == "up")
        {
            int pointer = 0;
            float x = 0, y = 0;
            words >> pointer >> x >> y;
            host->Touch(verb == "down" ? storm::ui::TouchPhase::Down : verb == "move" ? storm::ui::TouchPhase::Move : storm::ui::TouchPhase::Up, pointer,
                        x * density, y * density);
            run(1.0 / 60.0);
        }
        else if (verb == "wait")
        {
            double seconds = 0;
            words >> seconds;
            run(seconds);
        }
        else if (verb == "shot")
        {
            std::string path;
            words >> path;
            host->Frame();
            if (!SavePng(egl, width, height, path))
                status = 6;
        }
        else if (verb == "toast")
        {
            std::string text;
            std::getline(words, text);
            host->Toast(text.substr(text.find_first_not_of(' ') == std::string::npos ? 0 : text.find_first_not_of(' ')), "info");
            run(1.0 / 60.0);
        }
        else if (verb == "props") // props <element id> <json object>
        {
            std::string id, json;
            words >> id;
            std::getline(words, json);
            if (!host->SetProps(id, json))
                std::fprintf(stderr, "props: no component '%s' or bad JSON\n", id.c_str());
            run(1.0 / 60.0);
        }
        else if (verb == "interface") // interface <model.json>: the next model revision
        {
            std::string path;
            words >> path;
            if (!host->SetInterface(ReadFile(path)))
                status = 5;
            run(1.0 / 60.0);
        }
        else if (verb == "text") // text <answer>: what the player typed into the editor
        {
            std::string text;
            std::getline(words, text);
            host->SubmitText(text_token, text.substr(std::min(text.size(), text.find_first_not_of(' '))), true);
            run(1.0 / 60.0);
        }
        else if (verb == "state") // state <channel> <file.json>: the next state of a channel
        {
            std::string channel, path;
            words >> channel >> path;
            if (!host->SetState(channel, ReadFile(path)))
                status = 5;
            run(1.0 / 60.0);
        }
        else if (verb == "back")
        {
            std::printf("back %s\n", host->WantsBack() ? "ui" : "game");
            host->Back();
            run(1.0 / 60.0);
        }
        else if (verb == "cancel")
        {
            host->CancelInput();
            run(1.0 / 60.0);
        }
        else if (verb == "wants") // wants <x> <y>: would a pointer going down there belong to the UI?
        {
            float x = 0, y = 0;
            words >> x >> y;
            std::printf("wants %.0f %.0f %s\n", x, y, host->WantsTouch(x * density, y * density) ? "ui" : "game");
        }
        else if (verb == "bench") // bench <frames> [<channel> <a.json> <b.json>]
        {
            int frames = 0;
            std::string channel, first, second;
            words >> frames >> channel >> first >> second;
            const std::string alternate[2] = {channel.empty() ? std::string() : ReadFile(first), channel.empty() ? std::string() : ReadFile(second)};
            const auto finish = reinterpret_cast<void (*)()>(egl.GetProcAddress("glFinish"));
            struct Sum
            {
                double total = 0, worst = 0;
                void Add(double value)
                {
                    total += value;
                    worst = std::max(worst, value);
                }
            } update, render, gpu, frame;
            int drawn_frames = 0;
            long rebuilds = 0, laid_out = 0, geometry = 0, draws = 0, masks = 0, layers = 0, cached = 0, textures = 0, bytes = 0;
            for (int i = 0; i < frames; ++i)
            {
                const auto started = std::chrono::steady_clock::now();
                if (channel == "interface")
                    host->SetInterface(alternate[i % 2]);
                else if (!channel.empty())
                    host->SetState(channel, alternate[i % 2]);
                clock += 1.0 / 60.0;
                double delay = 0.0;
                storm::ui::FrameStats stats;
                if (host->Prepare(delay)) // as a platform host does: draw only an out-of-date frame
                {
                    host->Frame();
                    stats = host->LastFrameStats();
                    ++drawn_frames;
                }
                const auto drawn = std::chrono::steady_clock::now();
                finish();
                const auto finished = std::chrono::steady_clock::now();
                update.Add(stats.update_ms);
                render.Add(stats.render_ms);
                gpu.Add(std::chrono::duration<double, std::milli>(finished - drawn).count());
                frame.Add(std::chrono::duration<double, std::milli>(finished - started).count());
                rebuilds += stats.rebuilds;
                laid_out += stats.laid_out;
                geometry += stats.geometry;
                draws += stats.draws;
                masks += stats.masks;
                layers += stats.layers;
                cached += stats.cached_layers;
                textures += stats.textures;
                bytes += static_cast<long>(stats.texture_bytes);
            }
            const double n = std::max(1, frames);
            std::printf("bench %d frames%s%s, %d drawn: frame %.2f ms (max %.2f) = update %.2f (max %.2f) + render %.2f (max %.2f) + gpu %.2f (max %.2f); "
                        "per frame: rebuilds %.1f, laid out %.1f, meshes %.1f, draws %.1f, masks %.1f, layers %.1f (cached %.1f), textures %.2f (%.0f bytes)\n",
                        frames, channel.empty() ? "" : " alternating ", channel.c_str(), drawn_frames, frame.total / n, frame.worst, update.total / n, update.worst,
                        render.total / n, render.worst, gpu.total / n, gpu.worst, rebuilds / n, laid_out / n, geometry / n, draws / n, masks / n, layers / n, cached / n, textures / n,
                        bytes / n);
        }
        else if (verb == "scroll")
        {
            std::string id;
            words >> id;
            host->ScrollTo(id);
            run(0.05);
        }
        else if (!verb.empty())
        {
            std::fprintf(stderr, "unknown script command %s\n", verb.c_str());
            status = 2;
        }
    }
    host->Frame();
    if (!SavePng(egl, width, height, out))
    {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        status = 6;
    }
    host.reset();
    return status;
}
