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
#include <map>
#include <regex>
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
// iostreams swallow exceptions (bad_alloc while growing) into badbit and keep going, which
// would silently truncate JSON. Rethrow instead: a document is either complete or absent.
struct JsonStream : std::ostringstream {
    JsonStream() { exceptions(std::ios::badbit|std::ios::failbit); }
};
std::string lower(std::string x) {
    for (auto& c : x) if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(c));
    return x;
}
std::string quote(std::string const& s) {
    JsonStream out; out << '"';
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
// One spelling per ASCII-case-folded path: Android storage may be case-insensitive,
// so files differing only in case, or a file shadowing a directory, are rejected.
struct RawPaths {
    std::set<std::string> files;
    std::map<std::string,std::string> directories;
    void add(std::string const& path) {
        auto key=lower(path);
        if(!files.insert(key).second) throw std::runtime_error("Duplicate torrent paths");
        if(directories.count(key)) throw std::runtime_error("Torrent file and directory paths overlap");
        for(auto parent=fs::path(path).parent_path();!parent.empty();parent=parent.parent_path()) {
            auto original=parent.string(),canonical=lower(original);
            if(files.count(canonical)) throw std::runtime_error("Torrent file and directory paths overlap");
            auto entry=directories.emplace(canonical,original);
            if(!entry.second && entry.first->second!=original) throw std::runtime_error("Torrent directory case collision");
        }
    }
};
void tree(lt::bdecode_node const& n, int depth, int& count, RawPaths& paths, std::string const& prefix={}) {
    if (depth > 64 || n.type() != lt::bdecode_node::dict_t) throw std::runtime_error("Invalid v2 file tree");
    for (int i=0; i<n.dict_size(); ++i) {
        auto e=n.dict_at(i); auto name=e.first.to_string();
        if (name.empty()) { if (++count > max_files) throw std::runtime_error("Too many torrent files"); paths.add(prefix); }
        else { component(name); tree(e.second, depth+1, count, paths, prefix.empty()?name:prefix+"/"+name); }
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
    // Both spellings and the one libtorrent uses must each be collision-free.
    RawPaths original_paths,utf8_paths,effective_paths;
    for (int i=0; files && i<files.list_size(); ++i) {
        auto f=files.list_at(i);
        if(f.type()!=lt::bdecode_node::dict_t) throw std::runtime_error("Invalid torrent file record");
        for (auto key : {"path", "path.utf-8"}) {
            auto path=f.dict_find_list(key);
            if (!path && std::string(key)=="path.utf-8") continue;
            if (!path || path.list_size()==0 || path.list_size()>64) throw std::runtime_error("Invalid torrent path");
            std::string joined;
            for (int j=0;j<path.list_size();++j) {
                auto part=path.list_string_value_at(j).to_string(); component(part);
                joined+=(joined.empty()?"":"/")+part;
            }
            (std::string(key)=="path"?original_paths:utf8_paths).add(joined);
        }
        auto effective=f.dict_find_list("path.utf-8"); if(!effective) effective=f.dict_find_list("path");
        std::string effective_path;
        for (int j=0;j<effective.list_size();++j) effective_path+=(effective_path.empty()?"":"/")+effective.list_string_value_at(j).to_string();
        effective_paths.add(effective_path);
        if (f.dict_find("symlink path")) throw std::runtime_error("Torrent symlinks are forbidden");
    }
    auto v2=info.dict_find_dict("file tree"); int count=0;
    if(v2) { RawPaths v2_paths; tree(v2,0,count,v2_paths); }
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
std::string integers(std::vector<int> const& values) {
    JsonStream out; out << '[';
    for(std::size_t i=0;i<values.size();++i) { if(i) out << ','; out << values[i]; }
    out << ']'; return out.str();
}
struct Candidate {
    std::string id,kind,root,root_key;
    std::vector<int> indices,companions;
    std::vector<std::pair<int,std::string>> relative_paths;
    bool companions_truncated=false;
};
struct Manifest {
    std::vector<bool> recommended,selectable;
    std::string json;
};
// Multi-volume family of an archive file name, and the part's order in it.
struct ArchiveName { std::string family; std::uint64_t order=0; };
ArchiveName archive_name(std::string const& name) {
    static const std::regex rar(R"(^(.+)\.part([0-9]+)\.rar$)");
    static const std::regex split(R"(^(.+)\.([0-9]{3,})$)");
    static const std::regex old_part(R"(^(.+)\.([rz])([0-9]{2,})$)");
    static const std::regex numbered(R"(^(.+)\.part([0-9]+)$)");
    std::smatch match;
    auto number=[](std::string const& digits) {
        std::uint64_t n=0; for(char c:digits) { if(n>100000000) return std::uint64_t(1000000000); n=n*10+(c-'0'); } return n;
    };
    if(std::regex_match(name,match,rar)) return {"rar-part:"+match[1].str(),number(match[2])};
    if(std::regex_match(name,match,old_part)) return {"archive:"+match[1].str()+(match[2]=="r"?".rar":".zip"),number(match[3])+1};
    if(std::regex_match(name,match,split)) return {"split:"+match[1].str(),number(match[2])};
    if(std::regex_match(name,match,numbered)) return {"part:"+match[1].str(),number(match[2])};
    auto ext=fs::path(name).extension().string();
    for(auto const* known:{".zip",".7z",".rar",".iso",".tar",".gz",".bz2",".xz",".zst",".cab",".arc"})
        if(ext==known) return {"archive:"+name,0};
    return {};
}
bool volume_hint(std::string const& name) {
    auto ext=fs::path(name).extension().string();
    return ext==".bin"||ext==".dat"||ext==".cab"||ext==".pak"||ext==".arc"||ext==".pck"||
        (ext.size()>1 && std::all_of(ext.begin()+1,ext.end(),[](char c){return c>='0'&&c<='9';}));
}
bool inside(std::string const& path_key,std::string const& root_key) {
    return root_key.empty() || (path_key.size()>root_key.size() && path_key.compare(0,root_key.size(),root_key)==0 && path_key[root_key.size()]=='/');
}
// Metadata version 2: every candidate root from file names alone, never combined.
Manifest manifest(lt::torrent_info const& ti) {
    auto const& files=ti.files();
    if(files.num_files()>max_files || files.total_size()>max_bytes || files.total_size()<=0)
        throw std::runtime_error("Torrent exceeds file or size limits");
    // Also validate raw info for magnet metadata and resume data.
    auto bytes=ti.info_section(); lt::error_code ec; lt::bdecode_node node;
    lt::bdecode(bytes.data(),bytes.data()+bytes.size(),node,ec,nullptr,100,2000000);
    if(ec) throw std::runtime_error(ec.message());
    raw_info(node);
    RawPaths seen;
    std::vector<std::string> paths;
    Manifest m; m.recommended.resize(files.num_files()); m.selectable.resize(files.num_files());
    for(auto i:files.file_range()) {
        auto path=files.file_path(i);
        if(path.size()>4096 || fs::path(path).is_absolute()) throw std::runtime_error("Unsafe torrent path");
        for(auto const& part:fs::path(path)) component(part.string());
        if(files.file_flags(i)&lt::file_storage::flag_symlink) throw std::runtime_error("Torrent symlinks are forbidden");
        seen.add(path);
        paths.push_back(path); m.selectable[static_cast<int>(i)]=!(files.file_flags(i)&lt::file_storage::flag_pad_file);
    }
    // Loose roots: PROGRAM and RESOURCE directly below the same parent, at any depth.
    struct Loose { std::string original; std::vector<int> program,resource,ini; std::vector<std::pair<int,std::string>> relative; };
    std::map<std::string,Loose> loose;
    for(int index=0;index<files.num_files();++index) {
        if(!m.selectable[index]) continue;
        auto const& path=paths[index];
        std::vector<std::string> parts; for(auto const& part:fs::path(path)) parts.push_back(part.string());
        std::string prefix;
        for(std::size_t j=0;j+1<parts.size();++j) {
            auto lowered=lower(parts[j]);
            if(lowered=="program"||lowered=="resource") {
                auto& group=loose[lower(prefix)]; group.original=prefix;
                (lowered=="program"?group.program:group.resource).push_back(index);
                std::string relative=lowered=="program"?"PROGRAM":"RESOURCE";
                for(std::size_t k=j+1;k<parts.size();++k) relative+="/"+parts[k];
                group.relative.emplace_back(index,relative);
                break;
            }
            prefix+=(prefix.empty()?"":"/")+parts[j];
        }
        if(lower(parts.back())=="engine.ini") {
            auto parent=fs::path(path).parent_path().string();
            auto& group=loose[lower(parent)]; group.original=parent; group.ini.push_back(index); group.relative.emplace_back(index,"engine.ini");
        }
    }
    std::vector<Candidate> candidates;
    std::vector<std::string> loose_keys;
    for(auto const& [key,group]:loose) if(!group.program.empty() && !group.resource.empty()) {
        Candidate c; c.id="loose:"+key; c.kind="loose-game-candidate"; c.root=group.original; c.root_key=key;
        c.indices=group.program; c.indices.insert(c.indices.end(),group.resource.begin(),group.resource.end());
        c.indices.insert(c.indices.end(),group.ini.begin(),group.ini.end());
        std::sort(c.indices.begin(),c.indices.end()); c.indices.erase(std::unique(c.indices.begin(),c.indices.end()),c.indices.end());
        c.relative_paths=group.relative; std::sort(c.relative_paths.begin(),c.relative_paths.end());
        candidates.push_back(c); loose_keys.push_back(key);
    }
    // Executables and archives inside a loose root belong to that game, not to a new candidate.
    struct Archive { std::string root; std::vector<std::pair<std::uint64_t,int>> parts; };
    std::map<std::string,Archive> archives;
    std::map<std::string,std::vector<int>> volumes;
    for(int index=0;index<files.num_files();++index) {
        if(!m.selectable[index]) continue;
        auto key=lower(paths[index]);
        if(std::any_of(loose_keys.begin(),loose_keys.end(),[&](std::string const& root){return inside(key,root);})) continue;
        auto parent=fs::path(paths[index]).parent_path().string(); auto base=lower(fs::path(paths[index]).filename().string());
        if(fs::path(base).extension()==".exe") {
            Candidate c; c.id="installer:"+std::to_string(index); c.kind="installer-candidate";
            c.root=parent; c.root_key=lower(parent); c.indices={index}; candidates.push_back(c);
        }
        auto archive=archive_name(base);
        if(!archive.family.empty()) { auto& group=archives[lower(parent)+"/"+archive.family]; group.root=parent; group.parts.emplace_back(archive.order,index); }
        else if(volume_hint(base)) volumes[lower(parent)].push_back(index);
    }
    for(auto& [key,group]:archives) {
        std::sort(group.parts.begin(),group.parts.end()); Candidate c;
        c.id="archive:"+key; c.kind="archive-candidate"; c.root=group.root; c.root_key=lower(group.root);
        for(auto const& part:group.parts) c.indices.push_back(part.second);
        candidates.push_back(c);
    }
    // Installer volumes of the same directory are hints, capped, never selected automatically.
    for(auto& c:candidates) if(c.kind=="installer-candidate") {
        auto const& hints=volumes[c.root_key]; auto count=std::min<std::size_t>(64,hints.size());
        c.companions.assign(hints.begin(),hints.begin()+count); c.companions_truncated=hints.size()>count;
    }
    // A recommendation exists only for a single candidate: independent roots are never unioned.
    if(candidates.size()==1) {
        for(int i:candidates.front().indices) m.recommended[i]=true;
        for(int i:candidates.front().companions) m.recommended[i]=true;
    }
    std::set<std::string> kinds; for(auto const& c:candidates) kinds.insert(c.kind);
    std::string kind=kinds.empty()?"unknown":kinds.size()>1?"mixed-candidates":*kinds.begin();
    JsonStream out;
    out << "{\"metadataVersion\":2,\"name\":" << quote(ti.name()) << ",\"totalBytes\":" << files.total_size()
        << ",\"pieceBytes\":" << ti.piece_length() << ",\"candidate\":" << (candidates.empty()?"false":"true")
        << ",\"verifiedGame\":false,\"evidence\":\"filenames-only\",\"kind\":" << quote(kind) << ",\"candidates\":[";
    for(std::size_t j=0;j<candidates.size();++j) {
        auto const& c=candidates[j]; if(j) out << ',';
        out << "{\"id\":" << quote(c.id) << ",\"kind\":" << quote(c.kind) << ",\"root\":" << quote(c.root) << ",\"rootKey\":" << quote(c.root_key)
            << ",\"indices\":" << integers(c.indices) << ",\"companionIndices\":" << integers(c.companions)
            << ",\"companionHintsTruncated\":" << (c.companions_truncated?"true":"false") << ",\"relativePaths\":[";
        for(std::size_t k=0;k<c.relative_paths.size();++k) {
            if(k) out << ',';
            out << "{\"index\":" << c.relative_paths[k].first << ",\"path\":" << quote(c.relative_paths[k].second) << '}';
        }
        out << "]}";
    }
    out << "],\"files\":[";
    for(int i=0;i<files.num_files();++i) {
        if(i) out << ',';
        out << "{\"index\":" << i << ",\"path\":" << quote(paths[i]) << ",\"bytes\":" << files.file_size(lt::file_index_t{i})
            << ",\"recommended\":" << (m.recommended[i]?"true":"false") << ",\"selectable\":" << (m.selectable[i]?"true":"false") << '}';
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
std::string storage_kind(lt::error_code const& ec) {
    if(ec==boost::system::errc::no_space_on_device) return "storage-full";
    return ec.category()==boost::system::generic_category()||ec.category()==boost::system::system_category()?"storage":"transfer";
}
struct Session::Impl {
    fs::path staging;
    StorageLock storage_lock; // declared before session: remains locked until disk threads stop
    lt::session session;
    explicit Impl(std::string const& path,bool local):staging(resolve_staging(path)),storage_lock(staging),session(settings(local)) {}
    lt::torrent_handle handle;
    // A fatal error stops the transfer; its kind lets the app say what to do.
    std::string error,error_kind;
    // Network trouble (tracker, listen socket) is not fatal but explains a stall.
    std::string network_error;
    std::string manifest_cache; // immutable metadata JSON, built once
    std::int64_t last_progress_bytes=-1;
    std::chrono::steady_clock::time_point last_progress=std::chrono::steady_clock::now();
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
    void fail(std::string const& kind,std::string const& message) {
        if(!error.empty()) return; // keep the first cause
        error=message; error_kind=kind;
    }
    void alerts() {
        std::vector<lt::alert*> events; session.pop_alerts(&events);
        for(auto* a:events) {
            if(auto* r=lt::alert_cast<lt::save_resume_data_alert>(a)) {
                auto data=lt::write_resume_data_buf(r->params);
                auto tmp=staging/"resume.tmp";
                bool written;
                { std::ofstream out(tmp,std::ios::binary|std::ios::trunc); out.write(data.data(),data.size()); out.flush(); written=bool(out); }
                checkpoint=false;
                if(!written) {
                    std::error_code space; auto free=fs::space(staging,space).available;
                    fail(!space && free<data.size()+1024*1024?"storage-full":"storage","Cannot save the torrent checkpoint");
                    checkpoint_again=false; continue;
                }
                fs::rename(tmp,staging/"resume.dat");
                if(checkpoint_again) { checkpoint_again=false; save(); }
            } else if(auto* e=lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                checkpoint=false;checkpoint_again=false;fail(storage_kind(e->error),"Cannot save the torrent checkpoint: "+e->error.message());
            }
            else if(auto* e=lt::alert_cast<lt::file_error_alert>(a)) fail(storage_kind(e->error),e->error.message()+": "+e->filename());
            else if(auto* e=lt::alert_cast<lt::torrent_error_alert>(a)) fail(storage_kind(e->error),e->error.message());
            else if(auto* e=lt::alert_cast<lt::tracker_error_alert>(a)) network_error="Tracker "+std::string(e->tracker_url())+": "+e->error.message();
            else if(auto* e=lt::alert_cast<lt::listen_failed_alert>(a)) network_error="Cannot listen for peers on "+e->address.to_string()+":"+std::to_string(e->port)+": "+e->error.message();
        }
    }
};
Session::Session(std::string source,std::string staging,bool local):impl(std::make_unique<Impl>(staging,local)) {
    staging=impl->staging.string();
    lt::error_code ec; lt::add_torrent_params p;
    if(source.rfind("magnet:?",0)==0) {
        p=lt::parse_magnet_uri(source,ec);
        if(ec) throw std::runtime_error("Invalid magnet link: "+ec.message());
        if(!p.info_hashes.has_v1() && !p.info_hashes.has_v2()) throw std::runtime_error("Invalid magnet link: no info-hash");
    } else p.ti=load(source);
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
    if(selected.empty()) throw std::runtime_error("Select files to download first");
    safe_directory(impl->staging);
    std::vector<lt::download_priority_t> priorities(ti->num_files(),lt::dont_download);
    std::int64_t required=0;
    for(int i:selected) {
        if(i<0 || i>=ti->num_files() || !m.selectable[i]) throw std::runtime_error("Selected torrent file is not selectable");
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
    impl->handle.clear_error(); // a retry after freeing space or reconnecting starts clean
    impl->selection_pending=true;impl->priorities=priorities;
    impl->handle.prioritize_files(priorities);
    impl->handle.unset_flags(lt::torrent_flags::upload_mode);
    impl->approved=true; impl->selected=selected; impl->error.clear(); impl->error_kind.clear();
    impl->last_progress_bytes=-1;
}
void Session::pause() { impl->selection_pending=false; impl->handle.pause(); impl->save(); }
std::string Session::poll() {
    impl->alerts();
    auto status=impl->handle.status(); auto ti=impl->handle.torrent_file();
    std::string metadata="null",state="metadata";
    if(ti) {
        // Invalid metadata is a permanent error of this set; running out of memory is not.
        if(impl->manifest_cache.empty()) {
            try { impl->manifest_cache=manifest(*ti).json; }
            catch(std::bad_alloc const&) { throw; }
            catch(std::exception const& e) { impl->fail("metadata",e.what()); impl->handle.pause(); }
        }
        if(!impl->manifest_cache.empty()) metadata=impl->manifest_cache;
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
    // Seconds without new verified bytes (or metadata) while the transfer should be moving.
    auto now=std::chrono::steady_clock::now();
    std::int64_t progress=ti?status.total_wanted_done:-2;
    bool moving=state=="metadata"||state=="downloading";
    if(!moving || progress!=impl->last_progress_bytes) { impl->last_progress_bytes=progress; impl->last_progress=now; }
    auto stalled=moving?std::chrono::duration_cast<std::chrono::seconds>(now-impl->last_progress).count():0;
    JsonStream out;
    out << "{\"state\":" << quote(state) << ",\"downloadedBytes\":" << status.total_wanted_done
        << ",\"wantedBytes\":" << status.total_wanted << ",\"bytesPerSecond\":" << status.download_payload_rate
        << ",\"peers\":" << status.num_peers << ",\"seeds\":" << status.num_seeds
        << ",\"stalledSeconds\":" << stalled << ",\"networkError\":" << quote(impl->network_error)
        << ",\"checkpointPending\":" << (impl->checkpoint?"true":"false")
        << ",\"error\":" << quote(impl->error) << ",\"errorKind\":" << quote(impl->error_kind)
        << ",\"payloadDirectory\":" << quote((impl->staging/"payload").string())
        << ",\"metadata\":" << metadata << '}'; return out.str();
}
}
