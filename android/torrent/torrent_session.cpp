#include "torrent_session.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

namespace storm::torrent {
namespace lt = libtorrent;
namespace fs = std::filesystem;
namespace {
constexpr int max_files = 100000;
constexpr std::int64_t max_bytes = std::int64_t(512) * 1024 * 1024 * 1024;
std::string lower(std::string x) {
    for (auto& c : x) if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(c));
    return x;
}
std::string quote(std::string const& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) { char b[7]; snprintf(b, sizeof b, "\\u%04x", c); out << b; }
        else out << c;
    }
    out << '"'; return out.str();
}
void component(std::string const& s) {
    if (s.empty() || s == "." || s == ".." || s.size() > 255 ||
        s.find_first_of("/\\:") != std::string::npos ||
        std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Unsafe torrent path component");
}
void tree(lt::bdecode_node const& n, int depth, int& count) {
    if (depth > 64 || n.type() != lt::bdecode_node::dict_t) throw std::runtime_error("Invalid v2 file tree");
    for (int i=0; i<n.dict_size(); ++i) {
        auto e=n.dict_at(i); auto name=e.first.to_string();
        if (name.empty()) { if (++count > max_files) throw std::runtime_error("Too many torrent files"); }
        else { component(name); tree(e.second, depth+1, count); }
    }
}
// Check original bytes BEFORE libtorrent normalizes unsafe names.
void raw_info(lt::bdecode_node const& info) {
    if (info.type() != lt::bdecode_node::dict_t) throw std::runtime_error("Missing torrent info");
    component(info.dict_find_string_value("name").to_string());
    auto utf_name=info.dict_find("name.utf-8");
    if (utf_name) {
        if(utf_name.type()!=lt::bdecode_node::string_t) throw std::runtime_error("Invalid UTF-8 torrent name");
        component(utf_name.string_value().to_string());
    }
    auto files=info.dict_find_list("files");
    if (files && files.list_size() > max_files) throw std::runtime_error("Too many torrent files");
    for (int i=0; files && i<files.list_size(); ++i) {
        auto f=files.list_at(i);
        if(f.type()!=lt::bdecode_node::dict_t) throw std::runtime_error("Invalid torrent file record");
        for (auto key : {"path", "path.utf-8"}) {
            auto path=f.dict_find_list(key);
            if (!path && std::string(key)=="path.utf-8") continue;
            if (!path || path.list_size()==0 || path.list_size()>64) throw std::runtime_error("Invalid torrent path");
            for (int j=0;j<path.list_size();++j) component(path.list_string_value_at(j).to_string());
        }
        if (f.dict_find("symlink path")) throw std::runtime_error("Torrent symlinks are forbidden");
    }
    auto v2=info.dict_find_dict("file tree"); int count=0;
    if(v2) tree(v2,0,count);
}
std::vector<char> read(std::string const& path, std::uintmax_t limit=16*1024*1024) {
    if (fs::file_size(path)>limit) throw std::runtime_error("Metadata file is too large");
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot read metadata file");
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
std::shared_ptr<lt::torrent_info> load(std::string const& path) {
    auto data=read(path); lt::error_code ec; lt::bdecode_node node;
    lt::bdecode(data.data(),data.data()+data.size(),node,ec, nullptr,100,2000000);
    if(ec) throw std::runtime_error(ec.message());
    if(node.type()!=lt::bdecode_node::dict_t) throw std::runtime_error("Torrent must be a dictionary");
    raw_info(node.dict_find_dict("info"));
    auto ti=std::make_shared<lt::torrent_info>(node,ec);
    if(ec) throw std::runtime_error(ec.message());
    return ti;
}
struct Manifest {
    std::vector<bool> recommended;
    std::string json;
    bool candidate=false;
};
Manifest manifest(lt::torrent_info const& ti) {
    auto const& files=ti.files();
    if(files.num_files()>max_files || files.total_size()>max_bytes || files.total_size()<=0)
        throw std::runtime_error("Torrent exceeds file or size limits");
    // Also validate raw info for magnet metadata and resume data.
    auto bytes=ti.info_section(); lt::error_code ec; lt::bdecode_node node;
    lt::bdecode(bytes.data(),bytes.data()+bytes.size(),node,ec,nullptr,100,2000000);
    if(ec) throw std::runtime_error(ec.message());
    raw_info(node);
    std::set<std::string> seen;
    bool resource=false,program=false,installer=false,archive=false;
    std::vector<std::string> paths;
    for(auto i:files.file_range()) {
        auto p=files.file_path(i);
        if(p.size()>4096 || fs::path(p).is_absolute()) throw std::runtime_error("Unsafe torrent path");
        for(auto const& part:fs::path(p)) component(part.string());
        if(files.file_flags(i)&lt::file_storage::flag_symlink) throw std::runtime_error("Torrent symlinks are forbidden");
        auto l=lower(p);
        if(!seen.insert(l).second) throw std::runtime_error("Duplicate torrent paths");
        for(auto const& part:fs::path(l)) { resource|=part=="resource"; program|=part=="program"; }
        auto base=fs::path(l).filename().string(); auto ext=fs::path(l).extension().string();
        installer|=base=="setup.exe";
        archive|=ext==".zip" || ext==".7z" || ext==".rar" || ext==".iso";
        paths.push_back(p);
    }
    for(auto const& p:seen) for(auto parent=fs::path(p).parent_path();!parent.empty();parent=parent.parent_path())
        if(seen.count(parent.string())) throw std::runtime_error("Torrent file and directory paths overlap");
    Manifest m; m.candidate=(resource&&program)||installer||archive;
    m.recommended.resize(paths.size());
    std::ostringstream out;
    out << "{\"name\":" << quote(ti.name()) << ",\"totalBytes\":" << files.total_size()
        << ",\"pieceBytes\":" << ti.piece_length() << ",\"candidate\":" << (m.candidate?"true":"false")
        << ",\"verifiedGame\":false,\"kind\":" << quote(resource&&program?"loose-game-candidate":installer?"installer-candidate":archive?"archive-candidate":"unknown")
        << ",\"files\":[";
    for(auto i:files.file_range()) {
        auto p=lower(paths[static_cast<int>(i)]); auto base=fs::path(p).filename().string(); auto ext=fs::path(p).extension().string();
        bool excluded=false,game_file=false; for(auto const& part:fs::path(p)) {
            auto s=part.string(); excluded|=s=="crack"||s=="cracks"||s=="nodvd"||s=="redist"||s=="_commonredist";
            game_file|=s=="resource"||s=="program";
        }
        bool selected=!excluded && !(files.file_flags(i)&lt::file_storage::flag_pad_file) &&
            ((resource&&program&&(game_file||base=="engine.ini")) || base=="setup.exe" || (base.rfind("setup",0)==0&&ext==".bin") ||
             ext==".zip"||ext==".7z"||ext==".rar"||ext==".iso");
        m.recommended[static_cast<int>(i)]=selected;
        if(static_cast<int>(i))out << ',';
        out << "{\"index\":" << static_cast<int>(i) << ",\"path\":" << quote(paths[static_cast<int>(i)])
            << ",\"bytes\":" << files.file_size(i) << ",\"recommended\":" << (selected?"true":"false") << '}';
    }
    out << "]}"; m.json=out.str(); return m;
}
void safe_directory(fs::path const& root) {
    if(!root.is_absolute() || fs::weakly_canonical(root)!=root.lexically_normal())
        throw std::runtime_error("Staging must be an absolute path without symlinks");
    fs::create_directories(root);
    for(auto const& e:fs::recursive_directory_iterator(root))
        if(e.is_symlink()) throw std::runtime_error("Staging contains a symlink");
}
fs::path resolve_staging(std::string const& path) {
    if(!fs::path(path).is_absolute()) throw std::runtime_error("Staging must be an absolute private path");
    // Android may expose app storage through /data/user/0 aliases.
    return fs::weakly_canonical(path);
}
struct StorageLock {
    int fd=-1;
    explicit StorageLock(fs::path const& staging) {
        safe_directory(staging);
        fd=::open((staging/"session.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0) throw std::runtime_error("Cannot lock torrent staging");
        if(::flock(fd,LOCK_EX|LOCK_NB)!=0) { ::close(fd);fd=-1;throw std::runtime_error("Torrent set is already open"); }
    }
    ~StorageLock() { if(fd>=0) ::close(fd); }
};
lt::settings_pack settings(bool local=false) {
    lt::settings_pack s;
    s.set_int(lt::settings_pack::alert_mask,lt::alert_category::error|lt::alert_category::status|lt::alert_category::storage);
    s.set_int(lt::settings_pack::connections_limit,40);
    s.set_int(lt::settings_pack::active_downloads,1);
    s.set_int(lt::settings_pack::max_metadata_size,16*1024*1024);
    s.set_bool(lt::settings_pack::enable_upnp,false);
    s.set_bool(lt::settings_pack::enable_natpmp,false);
    s.set_bool(lt::settings_pack::enable_lsd,false);
    if(local) {
        s.set_int(lt::settings_pack::min_reconnect_time,1);
        s.set_bool(lt::settings_pack::enable_dht,false);
        s.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");
        s.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");
    }
    return s;
}
}
std::string inspect_file(std::string const& path) { return manifest(*load(path)).json; }
struct Session::Impl {
    fs::path staging;
    StorageLock storage_lock; // declared before session: remains locked until disk threads stop
    lt::session session;
    explicit Impl(std::string const& path,bool local):staging(resolve_staging(path)),storage_lock(staging),session(settings(local)) {}
    lt::torrent_handle handle;
    std::string error;
    bool approved=false, checkpoint=false, checkpoint_again=false, selection_pending=false;
    std::chrono::steady_clock::time_point last_checkpoint{};
    std::vector<int> selected;
    std::vector<lt::download_priority_t> priorities;
    void save() {
        if(!handle.is_valid()) return;
        if(checkpoint) { checkpoint_again=true; return; }
        checkpoint=true;
        last_checkpoint=std::chrono::steady_clock::now();
        handle.save_resume_data(lt::torrent_handle::save_info_dict|lt::torrent_handle::flush_disk_cache);
    }
    void alerts() {
        std::vector<lt::alert*> events; session.pop_alerts(&events);
        for(auto* a:events) {
            if(auto* r=lt::alert_cast<lt::save_resume_data_alert>(a)) {
                auto data=lt::write_resume_data_buf(r->params);
                auto tmp=staging/"resume.tmp";
                { std::ofstream out(tmp,std::ios::binary|std::ios::trunc); out.write(data.data(),data.size());
                  if(!out) throw std::runtime_error("Cannot save torrent checkpoint"); }
                fs::rename(tmp,staging/"resume.dat"); checkpoint=false;
                if(checkpoint_again) { checkpoint_again=false; save(); }
            } else if(auto* e=lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                checkpoint=false;checkpoint_again=false;error=e->error.message();
            }
            else if(auto* e=lt::alert_cast<lt::torrent_error_alert>(a)) error=e->error.message();
            else if(auto* e=lt::alert_cast<lt::file_error_alert>(a)) error=e->error.message();
        }
    }
};
Session::Session(std::string source,std::string staging,bool local):impl(std::make_unique<Impl>(staging,local)) {
    staging=impl->staging.string();
    lt::error_code ec; lt::add_torrent_params p;
    if(source.rfind("magnet:?",0)==0) p=lt::parse_magnet_uri(source,ec);
    else p.ti=load(source);
    if(ec) throw std::runtime_error(ec.message());
    if(p.ti) manifest(*p.ti);
    auto hashes=p.ti?p.ti->info_hashes():p.info_hashes;
    auto identity_path=fs::path(staging)/"torrent.id";
    auto identity=hashes.v1.to_string()+hashes.v2.to_string();
    bool known_identity=false;
    if(fs::exists(identity_path)) {
        auto bytes=read(identity_path,128);
        if(std::string(bytes.begin(),bytes.end())!=identity) throw std::runtime_error("Staging belongs to a different torrent");
        known_identity=true;
    }
    auto resume=fs::path(staging)/"resume.dat";
    if(fs::exists(resume)) {
        auto data=read(resume,64*1024*1024); auto saved=lt::read_resume_data(data,ec);
        if(ec) throw std::runtime_error(ec.message());
        auto saved_hashes=saved.ti?saved.ti->info_hashes():saved.info_hashes;
        if((hashes.has_v1() && hashes.v1!=saved_hashes.v1) || (hashes.has_v2() && hashes.v2!=saved_hashes.v2))
            throw std::runtime_error("Staging belongs to a different torrent");
        // Recover metadata, but hash the actual files again instead of trusting saved
        // piece bits. Never accept saved paths, priorities, trackers or renamed files.
        if(!p.ti && saved.ti) { manifest(*saved.ti); p.ti=saved.ti; }
    } else if(!known_identity && fs::exists(fs::path(staging)/"payload") && !fs::is_empty(fs::path(staging)/"payload")) {
        throw std::runtime_error("Nonempty staging has no matching checkpoint");
    }
    if(!known_identity) {
        // Establish identity BEFORE any payload can be written. This permits a
        // safe full rehash after process death before the first async checkpoint.
        auto temporary=fs::path(staging)/"torrent.id.tmp";
        {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(identity.data(),identity.size());
         if(!out)throw std::runtime_error("Cannot save torrent identity");}
        fs::rename(temporary,identity_path);
    }
    p.save_path=(fs::path(staging)/"payload").string();
    p.file_priorities.clear();
    p.flags &= ~(lt::torrent_flags::auto_managed|lt::torrent_flags::paused);
    p.flags |= lt::torrent_flags::default_dont_download|lt::torrent_flags::upload_mode;
    if(p.ti) p.file_priorities.assign(p.ti->num_files(),lt::dont_download);
    impl->handle=impl->session.add_torrent(p,ec);
    if(ec) throw std::runtime_error(ec.message());
}
Session::~Session() = default;
void Session::start(std::vector<int> const& selected) {
    auto ti=impl->handle.torrent_file();
    if(!ti) throw std::runtime_error("Torrent metadata is not available yet");
    auto m=manifest(*ti);
    if(!m.candidate || selected.empty()) throw std::runtime_error("Select game payload files first");
    safe_directory(impl->staging);
    std::vector<lt::download_priority_t> priorities(ti->num_files(),lt::dont_download);
    std::int64_t required=0;
    for(int i:selected) {
        if(i<0 || i>=ti->num_files() || !m.recommended[i]) throw std::runtime_error("Selected file is not a supported game payload");
        if(priorities[i]!=lt::dont_download) continue;
        priorities[i]=lt::default_priority; required+=ti->files().file_size(lt::file_index_t{i});
    }
    std::int64_t existing=0;
    for(auto i:ti->files().file_range()) if(priorities[static_cast<int>(i)]!=lt::dont_download) {
        auto path=impl->staging/"payload"/ti->files().file_path(i);
        struct stat statbuf{};
        if(::stat(path.c_str(),&statbuf)==0 && S_ISREG(statbuf.st_mode))
            existing+=std::min<std::int64_t>(std::int64_t(statbuf.st_blocks)*512,ti->files().file_size(i));
    }
    // Includes boundary pieces in the part file and a filesystem reserve. Extraction
    // space is checked separately by the archive importer before extraction.
    required=std::max<std::int64_t>(0,required-existing)+std::int64_t(selected.size())*2*ti->piece_length()+64*1024*1024;
    if(fs::space(impl->staging).available<static_cast<std::uintmax_t>(required)) throw std::runtime_error("Not enough free storage for selected payload");
    impl->alerts();
    impl->selection_pending=true;impl->priorities=priorities;
    impl->handle.prioritize_files(priorities);
    impl->handle.unset_flags(lt::torrent_flags::upload_mode);
    impl->approved=true; impl->selected=selected; impl->error.clear();
}
void Session::pause() { impl->selection_pending=false; impl->handle.pause(); impl->save(); }
std::string Session::poll() {
    impl->alerts();
    auto status=impl->handle.status(); auto ti=impl->handle.torrent_file();
    std::string metadata="null",state="metadata";
    if(ti) {
        try { metadata=manifest(*ti).json; }
        catch(std::exception const& e) { impl->error=e.what(); impl->handle.pause(); }
        state=impl->approved?"downloading":"awaiting-selection";
        if(impl->selection_pending && impl->handle.get_file_priorities()==impl->priorities) {
            impl->selection_pending=false;
            // Resume only AFTER the disk thread applies the new selection. Resuming
            // a finished subset sooner can leave it with no download candidates.
            impl->handle.resume(); status=impl->handle.status();
        }
        bool complete=impl->approved && !impl->selection_pending && status.is_finished;
        if(complete) {
            std::vector<std::int64_t> progress;
            impl->handle.file_progress(progress,lt::torrent_handle::piece_granularity);
            for(int i:impl->selected) if(i>=static_cast<int>(progress.size()) || progress[i]<ti->files().file_size(lt::file_index_t{i})) complete=false;
        }
        if(complete) { state="complete"; if(!(status.flags&lt::torrent_flags::paused)) pause(); }
        else if(impl->approved && !impl->selection_pending && (status.flags&lt::torrent_flags::paused)) state="paused";
        if(impl->error.empty() && std::chrono::steady_clock::now()-impl->last_checkpoint>std::chrono::seconds(30)) impl->save();
    }
    if(!impl->error.empty()) { state="error"; impl->handle.pause(); }
    std::ostringstream out;
    out << "{\"state\":" << quote(state) << ",\"downloadedBytes\":" << status.total_wanted_done
        << ",\"wantedBytes\":" << status.total_wanted << ",\"bytesPerSecond\":" << status.download_payload_rate
        << ",\"peers\":" << status.num_peers << ",\"checkpointPending\":" << (impl->checkpoint?"true":"false")
        << ",\"error\":" << quote(impl->error) << ",\"payloadDirectory\":" << quote((impl->staging/"payload").string())
        << ",\"metadata\":" << metadata << '}'; return out.str();
}
}
