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
// Sizes and script coordinates are in dp. The clock is simulated (60 frames a second),
// so captures are repeatable. Reported actions go to stdout as "action id verb value".
#include "storm_ui/render_backend.h"
#include "storm_ui/ui.h"

#include <plutovg.h>

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
    std::string pack, doc, out = "shot.png", game_root, script, interface, state, text_token;
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
            state = value;
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
    if (pack.empty() || (doc.empty() && interface.empty() && state.empty()))
    {
        std::fprintf(stderr, "usage: storm_ui_shot --pack DIR (--doc FILE | --interface JSON | --state CHANNEL=JSON) [--size WxH] [--density D] [--out PNG] [--game-root DIR] [--safe L,T,R,B] [--script S]\n");
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
    config.width = width;
    config.height = height;
    config.density = density;
    config.game_root = game_root;
    config.clock = [&clock] { return clock; };
    config.on_action = [](const storm::ui::Action &action) {
        if (action.screen.empty())
            std::printf("action %s %s %s\n", action.node.c_str(), action.verb.c_str(), action.value.c_str());
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
    const auto feed_state = [&host](const std::string &pair) {
        const size_t equals = pair.find('=');
        return equals != std::string::npos && host->SetState(pair.substr(0, equals), ReadFile(pair.substr(equals + 1)));
    };
    if (!doc.empty() ? !host->LoadDocument(doc) : !interface.empty() ? !host->SetInterface(ReadFile(interface)) : !feed_state(state))
    {
        std::fprintf(stderr, "cannot load %s\n", (doc + interface + state).c_str());
        return 5;
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
