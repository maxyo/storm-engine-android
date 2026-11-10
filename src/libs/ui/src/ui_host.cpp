#include "storm_ui/ui.h"

#include "storm_ui/render_backend.h"

#include "components.h"
#include "elements.h"
#include "screens/screen.h"
#include "storm_ui/interface_model.h"

#include <RmlUi/Core.h>
#include <plutovg.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace storm::ui
{

namespace
{

class DirectorySource final : public FileSource
{
  public:
    explicit DirectorySource(std::string root) : root_(std::move(root))
    {
    }
    bool Read(const std::string &path, std::vector<uint8_t> &out) override
    {
        if (path.find("..") != std::string::npos)
            return false;
        std::ifstream file(root_ + "/" + path, std::ios::binary);
        if (!file)
            return false;
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

  private:
    std::string root_;
};

class PackFiles final : public Rml::FileInterface
{
  public:
    explicit PackFiles(FileSource &source) : source_(source)
    {
    }

    Rml::FileHandle Open(const Rml::String &path) override
    {
        auto file = std::make_unique<Opened>();
        if (!source_.Read(Normalise(path), file->bytes))
            return 0;
        return reinterpret_cast<Rml::FileHandle>(file.release());
    }
    void Close(Rml::FileHandle handle) override
    {
        delete reinterpret_cast<Opened *>(handle);
    }
    size_t Read(void *buffer, size_t size, Rml::FileHandle handle) override
    {
        auto *file = reinterpret_cast<Opened *>(handle);
        const size_t count = std::min(size, file->bytes.size() - file->position);
        std::memcpy(buffer, file->bytes.data() + file->position, count);
        file->position += count;
        return count;
    }
    bool Seek(Rml::FileHandle handle, long offset, int origin) override
    {
        auto *file = reinterpret_cast<Opened *>(handle);
        const long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? static_cast<long>(file->position) : static_cast<long>(file->bytes.size());
        const long target = base + offset;
        if (target < 0 || static_cast<size_t>(target) > file->bytes.size())
            return false;
        file->position = static_cast<size_t>(target);
        return true;
    }
    size_t Tell(Rml::FileHandle handle) override
    {
        return reinterpret_cast<Opened *>(handle)->position;
    }

    // Collapses "a/../b" so that documents can link styles relative to themselves.
    static std::string Normalise(const std::string &path)
    {
        std::vector<std::string> parts;
        size_t at = 0;
        while (at <= path.size())
        {
            size_t end = path.find('/', at);
            if (end == std::string::npos)
                end = path.size();
            const std::string part = path.substr(at, end - at);
            if (part == "..")
            {
                if (!parts.empty())
                    parts.pop_back();
            }
            else if (!part.empty() && part != ".")
                parts.push_back(part);
            at = end + 1;
        }
        std::string out;
        for (const std::string &part : parts)
            out += (out.empty() ? "" : "/") + part;
        return out;
    }

  private:
    struct Opened
    {
        std::vector<uint8_t> bytes;
        size_t position = 0;
    };
    FileSource &source_;
};

class System final : public Rml::SystemInterface
{
  public:
    std::function<double()> clock;

    double GetElapsedTime() override
    {
        if (clock)
            return clock();
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        return duration<double>(steady_clock::now() - start).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
#if defined(__ANDROID__)
        const int priority = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? ANDROID_LOG_ERROR
                             : type == Rml::Log::LT_WARNING                            ? ANDROID_LOG_WARN
                                                                                       : ANDROID_LOG_INFO;
        __android_log_print(priority, "StormUi", "%s", message.c_str());
#else
        std::fprintf(stderr, "[ui %d] %s\n", static_cast<int>(type), message.c_str());
#endif
        return true;
    }
};

bool ReadJson(FileSource &files, const std::string &path, Json &out, std::string *error)
{
    std::vector<uint8_t> bytes;
    if (!files.Read(path, bytes))
    {
        if (error)
            *error = "UI pack lacks " + path;
        return false;
    }
    out = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (out.is_discarded())
    {
        if (error)
            *error = "UI pack: malformed " + path;
        return false;
    }
    return true;
}

class HostImpl final : public Host, public Services, public screens::Surface
{
  public:
    ~HostImpl() override
    {
        captures_.clear();
        ticking_.clear();
        router_.reset();
        if (context_)
        {
            context_->UnloadAllDocuments();
            Rml::RemoveContext(context_->GetName());
        }
        ShutdownGameImages();
        ReleaseIconTextures();
        ReleaseDrawTextures();
        if (initialised_)
            Rml::Shutdown();
        registry_.reset();
    }

    bool Start(const Config &config, std::string *error)
    {
        config_ = config;
        system_.clock = config.clock;
        if (!config.files)
        {
            if (error)
                *error = "no UI pack";
            return false;
        }
        backend_ = config.backend;
        if (!backend_)
        {
            if (error)
                *error = "no render backend given";
            return false;
        }
        files_ = std::make_unique<PackFiles>(*config.files);
        Rml::SetSystemInterface(&system_);
        Rml::SetFileInterface(files_.get());
        Rml::SetRenderInterface(&backend_->Interface());
        if (!Rml::Initialise())
        {
            if (error)
                *error = "RmlUi failed to start";
            return false;
        }
        initialised_ = true;

        Json pack, components, icons;
        if (!ReadJson(*config.files, "pack.json", pack, error) ||
            !ReadJson(*config.files, pack.value("components", std::string("components.json")), components, error) ||
            !ReadJson(*config.files, pack.value("icons", std::string("icons.json")), icons, error))
            return false;
        settings_ = pack.value("settings", Json::object());
        RegisterBuiltinBehaviors();
        RegisterDrawElements();
        RegisterIconElement();
        RegisterGameImageElement();
        LoadIconPack(icons);
        registry_ = std::make_unique<ComponentRegistry>(*this);
        registry_->Load(components);
        SetGameImageLoader(config.game_picture_loader);
        SetGameImageRoot(config.game_root);

        bool fallback = true;
        const Json fonts = pack.value("fonts", Json::array());
        for (const Json &font : fonts)
        {
            if (!Rml::LoadFontFace(font.get<std::string>(), fallback))
                Rml::Log::Message(Rml::Log::LT_ERROR, "Font %s failed to load", font.get<std::string>().c_str());
            fallback = false;
        }

        backend_->SetViewport(config.width, config.height);
        context_ = Rml::CreateContext("storm-ui", {config.width, config.height});
        if (!context_)
        {
            if (error)
                *error = "RmlUi context failed";
            return false;
        }
        context_->SetDensityIndependentPixelRatio(config.density);
        screen_path_ = pack.value("screen", std::string());
        router_ = std::make_unique<screens::Router>(*this);
        overlay_path_ = pack.value("overlay", std::string());
        if (!overlay_path_.empty())
        {
            overlay_ = context_->LoadDocument(overlay_path_);
            if (overlay_)
                overlay_->Show();
        }
        return true;
    }

    // --- Host ---------------------------------------------------------------------

    bool LoadDocument(const std::string &path) override
    {
        router_->CloseInterface();
        return OpenDocument(path);
    }

    bool OpenDocument(const std::string &path)
    {
        captures_.clear();
        screen_root_ = nullptr;
        if (document_)
        {
            document_->Close();
            document_ = nullptr;
        }
        document_ = context_->LoadDocument(path);
        if (!document_)
            return false;
        ReportUnknownComponents(document_);
        document_->Show();
        if (overlay_)
            overlay_->PullToFront();
        return true;
    }

    void Resize(int width, int height, float density) override
    {
        config_.width = width;
        config_.height = height;
        config_.density = density;
        backend_->SetViewport(width, height);
        context_->SetDimensions({width, height});
        context_->SetDensityIndependentPixelRatio(density);
    }

    void SetSafeArea(float left, float top, float right, float bottom) override
    {
        const Json safe = {{"left", left}, {"top", top}, {"right", right}, {"bottom", bottom}};
        if (safe == safe_)
            return;
        safe_ = safe;
        // Rebuild every component: the insets reach the templates through their scope.
        std::vector<Rml::Element *> pending = {document_, overlay_};
        while (!pending.empty())
        {
            Rml::Element *element = pending.back();
            pending.pop_back();
            if (!element)
                continue;
            if (auto *component = dynamic_cast<ComponentElement *>(element))
                component->SetProps(Json::object());
            for (int i = 0; i < element->GetNumChildren(); ++i)
                pending.push_back(element->GetChild(i));
        }
        // The host's own toast stacks keep clear of the cutout too (8 / 16 dp: their offsets in base.rcss).
        if (overlay_)
        {
            const auto inset = [this](const char *id, Rml::PropertyId side, float base, float extra) {
                if (Rml::Element *stack = overlay_->GetElementById(id))
                    stack->SetProperty(side, Rml::Property(base + extra, Rml::Unit::DP));
            };
            for (const char *id : {"toasts-top", "toasts-bottom"})
            {
                inset(id, Rml::PropertyId::Left, 0.0f, left);
                inset(id, Rml::PropertyId::Right, 0.0f, right);
            }
            inset("toasts-top", Rml::PropertyId::Top, 8.0f, top);
            inset("toasts-bottom", Rml::PropertyId::Bottom, 16.0f, bottom);
        }
    }

    void SetGameRoot(const std::string &game_root) override
    {
        config_.game_root = game_root;
        SetGameImageRoot(game_root);
    }

    void Touch(TouchPhase phase, int pointer, float x, float y) override
    {
        const Rml::Vector2f at = {x, y};
        if (phase == TouchPhase::Down)
        {
            Capture capture;
            capture.pointer.id = pointer;
            capture.pointer.position = capture.pointer.start = at;
            capture.pointer.target = context_->GetElementAtPoint(at);
            ComponentElement *component = InteractiveComponentAt(capture.pointer.target);
            if (component && component->PointerExclusive(capture.pointer))
            {
                capture.exclusive = true;
                capture.component = component->GetObserverPtr();
                component->PointerDown(capture.pointer);
            }
            else
            {
                if (component && component->PointerDown(capture.pointer))
                    capture.component = component->GetObserverPtr();
                if (scroll_pointer_ < 0)
                {
                    scroll_pointer_ = pointer;
                    context_->ProcessTouchStart(Rml::TouchList{Rml::Touch{static_cast<Rml::TouchId>(pointer), at}}, 0);
                }
            }
            captures_[pointer] = std::move(capture);
            return;
        }

        const auto found = captures_.find(pointer);
        if (found == captures_.end())
            return;
        Capture &capture = found->second;
        capture.pointer.position = at;
        auto *component = static_cast<ComponentElement *>(capture.component.get());
        const bool ended = phase == TouchPhase::Up || phase == TouchPhase::Cancel;

        if (capture.exclusive)
        {
            if (component)
            {
                if (ended)
                    component->PointerUp(capture.pointer, phase == TouchPhase::Cancel);
                else
                    component->PointerMove(capture.pointer);
            }
        }
        else
        {
            if (component)
            {
                // A tap that travels becomes a scroll: the press is withdrawn.
                const float slop = 10.0f * config_.density;
                if (ended)
                    component->PointerUp(capture.pointer, phase == TouchPhase::Cancel);
                else if ((at - capture.pointer.start).SquaredMagnitude() > slop * slop)
                {
                    component->PointerUp(capture.pointer, true);
                    capture.component = {};
                }
                else
                    component->PointerMove(capture.pointer);
            }
            if (pointer == scroll_pointer_)
            {
                const Rml::TouchList touch{Rml::Touch{static_cast<Rml::TouchId>(pointer), at}};
                if (phase == TouchPhase::Move)
                    context_->ProcessTouchMove(touch, 0);
                else if (phase == TouchPhase::Up)
                    context_->ProcessTouchEnd(touch, 0);
                else
                    context_->ProcessTouchCancel(touch);
                if (ended)
                    scroll_pointer_ = -1;
            }
        }
        if (ended)
            captures_.erase(found);
    }

    double Frame() override
    {
        const double now = system_.GetElapsedTime();
        const std::vector<Rml::ObserverPtr<Rml::Element>> ticking = ticking_;
        for (const auto &entry : ticking)
            if (auto *component = static_cast<ComponentElement *>(entry.get()))
                component->Tick(now);
        ExpireToasts(now);
        const bool images = GameImagesChanged();
        const double routing = router_->Tick();

        context_->Update();
        if (TextClampsChanged()) // clamped text was re-cut for a new width
            context_->Update();
        backend_->BeginFrame(true);
        context_->Render();
        backend_->EndFrame();

        double delay = context_->GetNextUpdateDelay();
        if (!ticking_.empty() || images)
            delay = 0.0;
        delay = std::min(delay, routing);
        for (const Toast_ &toast : toasts_)
            delay = std::min(delay, std::max(0.0, toast.until - now));
        return delay;
    }

    void Toast(const std::string &text, const std::string &tone) override
    {
        if (!overlay_ || text.empty())
            return;
        const bool top = document_ && document_->GetAttribute<Rml::String>("data-toast", "bottom") == "top";
        Rml::Element *stack = overlay_->GetElementById(top ? "toasts-top" : "toasts-bottom");
        if (!stack)
            return;
        while (toasts_.size() >= 3)
            RemoveToast(0);
        Rml::ElementPtr notice = overlay_->CreateElement("c-notice");
        notice->SetAttribute("variant", "toast");
        notice->SetAttribute("place", "none");
        notice->SetAttribute("tone", tone.empty() ? "info" : tone);
        notice->SetAttribute("text", text);
        if (top)
            notice->SetAttribute("compact", "");
        Toast_ toast;
        toast.element = stack->AppendChild(std::move(notice))->GetObserverPtr();
        toast.until = system_.GetElapsedTime() + Setting("toastMs", 2600.0) / 1000.0;
        toasts_.push_back(std::move(toast));
    }

    bool SetProps(const std::string &element_id, const std::string &json) override
    {
        Rml::Element *element = document_ ? document_->GetElementById(element_id) : nullptr;
        auto *component = dynamic_cast<ComponentElement *>(element);
        const Json props = Json::parse(json, nullptr, false);
        if (!component || !props.is_object())
            return false;
        component->SetProps(props);
        return true;
    }

    void ScrollTo(const std::string &element_id) override
    {
        if (Rml::Element *element = document_ ? document_->GetElementById(element_id) : nullptr)
            element->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Start));
    }

    // --- game interfaces -----------------------------------------------------------

    bool SetInterface(const std::string &json) override
    {
        InterfaceModel model;
        std::string error;
        if (!ParseInterfaceModel(json, model, &error))
        {
            Error(error);
            return false;
        }
        SetInterface(model);
        return true;
    }

    void SetInterface(const InterfaceModel &model) override
    {
        router_->SetInterface(model);
    }

    void CloseInterface() override
    {
        router_->CloseInterface();
    }

    bool SetState(const std::string &channel, const std::string &json) override
    {
        const Json state = Json::parse(json, nullptr, false);
        if (state.is_discarded())
        {
            Error("state channel " + channel + ": malformed JSON");
            return false;
        }
        try
        {
            router_->SetState(channel, state);
        }
        catch (const Json::exception &error)
        {
            Error("state channel " + channel + ": " + error.what());
            return false;
        }
        return true;
    }

    void SubmitText(const std::string &token, const std::string &text, bool accepted) override
    {
        router_->SubmitText(token, text, accepted);
    }

    bool InterfaceActive() const override
    {
        return router_->Active();
    }

    // --- screens::Surface ---------------------------------------------------------

    void Show(const std::string &tag, const Json &props) override
    {
        if (!screen_root_ && (screen_path_.empty() || !OpenDocument(screen_path_)))
        {
            Error("UI pack has no screen document");
            return;
        }
        screen_root_ = document_;
        Rml::Element *mounted = document_->GetElementById("screen");
        if (!mounted || mounted->GetTagName() != tag)
        {
            if (mounted)
                document_->RemoveChild(mounted);
            Rml::ElementPtr created = document_->CreateElement(tag);
            created->SetId("screen");
            mounted = document_->AppendChild(std::move(created));
        }
        if (auto *component = dynamic_cast<ComponentElement *>(mounted))
            component->SetProps(props);
        else
            Error("UI pack has no screen " + tag);
    }

    void Hide() override
    {
        captures_.clear();
        if (screen_root_ && document_)
        {
            document_->Close();
            document_ = nullptr;
        }
        screen_root_ = nullptr;
    }

    void Send(const Action &action) override
    {
        if (config_.on_action)
            config_.on_action(action);
    }

    void RequestText(TextRequest request, const std::string &anchor) override
    {
        if (Rml::Element *element = document_ ? document_->GetElementById(anchor) : nullptr)
        {
            const Rml::Vector2f origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
            request.x = origin.x;
            request.y = origin.y;
            request.width = size.x;
            request.height = size.y;
        }
        if (config_.on_text_request)
            config_.on_text_request(request);
        else
            router_->SubmitText(request.token, {}, false);
    }

    void Toast(const std::string &text) override
    {
        Toast(text, "info");
    }

    void Log(const std::string &line) override
    {
        Rml::Log::Message(Rml::Log::LT_INFO, "%s", line.c_str());
    }

    void Error(const std::string &line) override
    {
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s", line.c_str());
        if (config_.show_errors)
            Toast(line, "danger");
    }

    // --- Services -----------------------------------------------------------------

    double Now() const override
    {
        return const_cast<System &>(system_).GetElapsedTime();
    }
    float Density() const override
    {
        return config_.density;
    }
    Json SafeArea() const override
    {
        return safe_;
    }
    double Setting(const std::string &name, double fallback) const override
    {
        const auto found = settings_.find(name);
        return found != settings_.end() && found->is_number() ? found->get<double>() : fallback;
    }
    void Emit(ComponentElement &source, const std::string &verb, const std::string &value) override
    {
        std::string id;
        for (Rml::Element *element = &source; element && id.empty(); element = element->GetParentNode())
            id = element->GetId();
        if (screen_root_)
        {
            // A game interface: its screen maps the event to engine actions.
            router_->OnEvent({id, verb, value});
            return;
        }
        if (!config_.on_action)
            return;
        Action action;
        action.node = id;
        action.verb = verb;
        action.value = value;
        config_.on_action(action);
    }
    void Tooltip(Rml::Element *anchor, const std::string &text, bool below) override
    {
        Rml::Element *tip = overlay_ ? overlay_->GetElementById("tooltip") : nullptr;
        if (!tip)
            return;
        if (!anchor || text.empty())
        {
            tip->SetClass("open", false);
            return;
        }
        const Rml::Vector2f origin = anchor->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = anchor->GetBox().GetSize(Rml::BoxArea::Border);
        const float gap = 6.0f * config_.density;
        const float width = std::min(static_cast<float>(config_.width) - 2.0f * gap, std::max(size.x, 280.0f * config_.density));
        const float left = std::clamp(origin.x, gap, static_cast<float>(config_.width) - gap - width);
        tip->SetInnerRML(EscapeRml(text));
        tip->SetProperty(Rml::PropertyId::Left, Rml::Property(left, Rml::Unit::PX));
        tip->SetProperty(Rml::PropertyId::Width, Rml::Property(width, Rml::Unit::PX));
        if (below)
        {
            tip->SetProperty(Rml::PropertyId::Top, Rml::Property(origin.y + size.y + gap, Rml::Unit::PX));
            tip->RemoveProperty(Rml::PropertyId::Bottom);
        }
        else
        {
            tip->SetProperty(Rml::PropertyId::Bottom, Rml::Property(static_cast<float>(config_.height) - origin.y + gap, Rml::Unit::PX));
            tip->RemoveProperty(Rml::PropertyId::Top);
        }
        tip->SetClass("open", true);
    }
    void RequestTicks(ComponentElement &component, bool enable) override
    {
        const auto found = std::find_if(ticking_.begin(), ticking_.end(), [&](const auto &entry) { return entry.get() == &component; });
        if (enable && found == ticking_.end())
            ticking_.push_back(component.GetObserverPtr());
        else if (!enable && found != ticking_.end())
            ticking_.erase(found);
    }
    void Forget(ComponentElement &component) override
    {
        ticking_.erase(std::remove_if(ticking_.begin(), ticking_.end(), [&](const auto &entry) { return !entry || entry.get() == &component; }),
                       ticking_.end());
    }

  private:
    struct Capture
    {
        Pointer pointer;
        Rml::ObserverPtr<Rml::Element> component;
        bool exclusive = false;
    };
    struct Toast_
    {
        Rml::ObserverPtr<Rml::Element> element;
        double until = 0.0;
    };

    void RemoveToast(size_t index)
    {
        if (Rml::Element *element = toasts_[index].element.get())
            if (Rml::Element *parent = element->GetParentNode())
                parent->RemoveChild(element);
        toasts_.erase(toasts_.begin() + static_cast<long>(index));
    }

    void ExpireToasts(double now)
    {
        for (size_t i = toasts_.size(); i-- > 0;)
            if (now >= toasts_[i].until)
                RemoveToast(i);
    }

    Config config_;
    System system_;
    std::unique_ptr<PackFiles> files_;
    RenderBackend *backend_ = nullptr;
    std::unique_ptr<ComponentRegistry> registry_;
    Rml::Context *context_ = nullptr;
    Rml::ElementDocument *document_ = nullptr;
    Rml::ElementDocument *overlay_ = nullptr;
    std::string overlay_path_;
    std::string screen_path_;
    std::unique_ptr<screens::Router> router_;
    Rml::ElementDocument *screen_root_ = nullptr; // the screen document while a game interface is mounted
    Json settings_ = Json::object();
    Json safe_ = {{"left", 0}, {"top", 0}, {"right", 0}, {"bottom", 0}};
    std::unordered_map<int, Capture> captures_;
    std::vector<Rml::ObserverPtr<Rml::Element>> ticking_;
    std::vector<Toast_> toasts_;
    int scroll_pointer_ = -1;
    bool initialised_ = false;
};

} // namespace

bool DecodePackImage(const std::string &source, int &width, int &height, std::vector<unsigned char> &rgba)
{
    Rml::FileInterface *files = Rml::GetFileInterface();
    const Rml::FileHandle handle = files->Open(source);
    if (!handle)
        return false;
    std::vector<uint8_t> bytes(files->Length(handle));
    files->Read(bytes.data(), bytes.size(), handle);
    files->Close(handle);
    plutovg_surface_t *surface = plutovg_surface_load_from_image_data(bytes.data(), static_cast<int>(bytes.size()));
    if (!surface)
        return false;
    width = plutovg_surface_get_width(surface);
    height = plutovg_surface_get_height(surface);
    const int stride = plutovg_surface_get_stride(surface);
    const unsigned char *data = plutovg_surface_get_data(surface);
    rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y)
    {
        const auto *row = reinterpret_cast<const uint32_t *>(data + static_cast<size_t>(y) * static_cast<size_t>(stride));
        for (int x = 0; x < width; ++x)
        {
            const uint32_t argb = row[x]; // premultiplied
            unsigned char *pixel = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            pixel[0] = static_cast<unsigned char>(argb >> 16);
            pixel[1] = static_cast<unsigned char>(argb >> 8);
            pixel[2] = static_cast<unsigned char>(argb);
            pixel[3] = static_cast<unsigned char>(argb >> 24);
        }
    }
    plutovg_surface_destroy(surface);
    return true;
}

std::unique_ptr<FileSource> DirectoryFiles(std::string root)
{
    return std::make_unique<DirectorySource>(std::move(root));
}

std::unique_ptr<Host> Host::Create(const Config &config, std::string *error)
{
    auto host = std::make_unique<HostImpl>();
    if (!host->Start(config, error))
        return nullptr;
    return host;
}

} // namespace storm::ui
