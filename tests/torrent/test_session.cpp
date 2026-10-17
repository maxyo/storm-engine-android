#include "../../android/torrent/torrent_session.hpp"
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
namespace fs=std::filesystem;
namespace lt=libtorrent;
void require(bool b,char const* message) { if(!b)throw std::runtime_error(message); }
void write(fs::path const& p,lt::entry const& e) { std::ofstream o(p,std::ios::binary); lt::bencode(std::ostream_iterator<char>(o),e); }
int main(int argc,char** argv) {
    try {
        if(argc==2){std::cout<<storm::torrent::inspect_file(argv[1])<<'\n';return 0;}
        auto root=fs::temp_directory_path()/("storm-torrent-test-"+std::to_string(getpid()));
        fs::create_directories(root/"seed/fixture");
        std::string payload(256*1024+17,'x');
        std::string bin_payload(512*1024,'b');
        for(std::size_t i=0;i<payload.size();i+=97)payload[i]=char(i%251);
        {std::ofstream o(root/"seed/fixture/setup.exe",std::ios::binary);o.write(payload.data(),payload.size());}
        {std::ofstream o(root/"seed/fixture/setup-1.bin",std::ios::binary);o.write(bin_payload.data(),bin_payload.size());}
        lt::file_storage files;files.add_file("fixture/setup.exe",payload.size());files.add_file("fixture/setup-1.bin",bin_payload.size());
        lt::create_torrent creator(files,16*1024,lt::create_torrent::v1_only);
        lt::set_piece_hashes(creator,(root/"seed").string());
        auto metadata=creator.generate();write(root/"good.torrent",metadata);
        auto manifest=storm::torrent::inspect_file((root/"good.torrent").string());
        require(manifest.find("installer-candidate")!=std::string::npos,"Expected installer candidate");
        require(manifest.find("\"verifiedGame\":false")!=std::string::npos,"Metadata must not claim game identity");
        auto bad=metadata;bad["info"]["name"]="..";write(root/"bad.torrent",bad);
        bool rejected=false;try{storm::torrent::inspect_file((root/"bad.torrent").string());}catch(...){rejected=true;}
        require(rejected,"Traversal must be rejected before normalization");
        {std::ofstream out(root/"malformed.torrent");out<<"le";}
        rejected=false;try{storm::torrent::inspect_file((root/"malformed.torrent").string());}catch(...){rejected=true;}
        require(rejected,"Malformed root must reject safely");
        lt::settings_pack settings;
        settings.set_int(lt::settings_pack::min_reconnect_time,1);
        settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");
        settings.set_bool(lt::settings_pack::enable_dht,false);
        settings.set_bool(lt::settings_pack::enable_lsd,false);
        settings.set_bool(lt::settings_pack::enable_upnp,false);
        settings.set_bool(lt::settings_pack::enable_natpmp,false);
        lt::session seed(settings);lt::add_torrent_params params;
        params.ti=std::make_shared<lt::torrent_info>((root/"good.torrent").string());
        params.save_path=(root/"seed").string();params.flags&=~lt::torrent_flags::paused;params.flags&=~lt::torrent_flags::auto_managed;
        auto seed_handle=seed.add_torrent(params);
        for(int i=0;i<100&&!seed_handle.status().is_seeding;++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(seed_handle.status().is_seeding,"Local seed must hash benign fixture");
        auto magnet=lt::make_magnet_uri(*params.ti)+"&x.pe=127.0.0.1:"+std::to_string(seed.listen_port());
        std::string status;
        {
        storm::torrent::Session download(magnet,(root/"download").string(),true);
        rejected=false;try{storm::torrent::Session duplicate(magnet,(root/"download").string(),true);}catch(...){rejected=true;}
        require(rejected,"Simultaneous writers must be rejected");
        for(int i=0;i<200;++i){status=download.poll();if(status.find("awaiting-selection")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        require(status.find("awaiting-selection")!=std::string::npos,"Magnet metadata retrieval must finish");
        require(!fs::exists(root/"download/payload/fixture/setup.exe"),"Metadata retrieval must not create payload");
        rejected=false;try{download.start({999});}catch(...){rejected=true;}require(rejected,"Invalid selection must fail");
        download.start({0});
        for(int i=0;i<400;++i){status=download.poll();if(status.find("\"state\":\"complete\"")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        require(status.find("\"state\":\"complete\"")!=std::string::npos,"Real local peer transfer must complete");
        for(int i=0;i<100;++i){status=download.poll();if(status.find("\"checkpointPending\":false")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        std::ifstream in(root/"download/payload/fixture/setup.exe",std::ios::binary);
        std::string actual{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
        require(actual==payload,"Piece-verified payload must match original bytes");
        require(fs::exists(root/"download/resume.dat"),"Completion must checkpoint resume state");
        require(!fs::exists(root/"download/payload/fixture/setup-1.bin"),"Setup-only phase must not create bin payload");
        download.start({0,1});
        for(int i=0;i<400;++i){status=download.poll();if(status.find("\"state\":\"complete\"")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        if(status.find("\"state\":\"complete\"")==std::string::npos) std::cerr<<status<<'\n';
        require(status.find("\"state\":\"complete\"")!=std::string::npos,"Superset selection must complete after setup-only phase");
        for(int i=0;i<100;++i){status=download.poll();if(status.find("\"checkpointPending\":false")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        std::ifstream bin_in(root/"download/payload/fixture/setup-1.bin",std::ios::binary);
        std::string bin_actual{std::istreambuf_iterator<char>(bin_in),std::istreambuf_iterator<char>()};
        require(bin_actual==bin_payload,"Superset phase must download exact bin bytes");
        }
        // Corrupt an existing piece. Recovery must rehash and obtain that piece again.
        {std::fstream out(root/"download/payload/fixture/setup.exe",std::ios::in|std::ios::out|std::ios::binary);out.put('!');}
        {
        storm::torrent::Session restored(magnet,(root/"download").string(),true);
        status=restored.poll();require(status.find("awaiting-selection")!=std::string::npos,"Resume must await explicit selection");
        restored.start({0});
        for(int i=0;i<400;++i){status=restored.poll();if(status.find("\"state\":\"complete\"")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        require(status.find("\"state\":\"complete\"")!=std::string::npos,"Resume must complete after corruption");
        for(int i=0;i<100;++i){status=restored.poll();if(status.find("\"checkpointPending\":false")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        std::ifstream in(root/"download/payload/fixture/setup.exe",std::ios::binary);
        std::string actual{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
        require(actual==payload,"Resume must repair corrupted pieces, not trust saved bits");
        }
        fs::remove(root/"download/resume.dat"); // simulate process death before checkpoint
        {
        storm::torrent::Session recovered((root/"good.torrent").string(),(root/"download").string(),true);
        recovered.start({0,1});
        for(int i=0;i<200;++i){status=recovered.poll();if(status.find("\"state\":\"complete\"")!=std::string::npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        require(status.find("\"state\":\"complete\"")!=std::string::npos,"Synchronous identity must permit recovery without a checkpoint");
        }
        auto other=metadata;other["info"]["name"]="different";write(root/"other.torrent",other);
        rejected=false;try{storm::torrent::Session wrong((root/"other.torrent").string(),(root/"download").string(),true);}catch(...){rejected=true;}
        require(rejected,"Checkpoint must be bound to the original infohash");
        fs::create_directories(root/"unsafe");fs::create_symlink(root/"seed",root/"unsafe/link");
        rejected=false;try{storm::torrent::Session unsafe(magnet,(root/"unsafe").string(),true);}catch(...){rejected=true;}
        require(rejected,"Staging symlinks must be rejected");
        std::cout<<"PASS metadata, traversal, exclusive staging, zero-payload magnet, setup-only then superset, local transfer, bytes, checkpoint, resume repair, checkpoint-free recovery, symlink rejection\n";
        fs::remove_all(root);return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
