#include "mobile_widget.hpp"
#include <cassert>
#include <iostream>
#include <strings.h>
#include <cstring>
#define Assert assert
#define PICTURE_TEXTURE_RELEASE(service,group,id) ((void)0)
#define STORM_DELETE(pointer) do {delete[] pointer;pointer=nullptr;} while(false)
#define TEXTURE_RELEASE(renderer,id) (id=-1)
#define VIDEOTEXTURE_RELEASE(renderer,texture) (texture=nullptr)
namespace storm {
bool iEquals(const char *a,const char *b) {return strcasecmp(a,b)==0;}
namespace android { bool imagePoll=true,imageReply=true; std::string mobileScreen="questbook"; }
namespace mobile { ImageRef Reference(Descriptor,uint32_t) {return {1,1,64,64};} }
}
struct FXYRECT {float left=0,top=0,right=.5f,bottom=.5f;};
struct PictureService {
    bool found=true;
    int GetTextureID(const char *) {return 7;}
    bool ResolveImageSource(const char *,const char *,std::string &path,FXYRECT &,int32_t &,int32_t &) {
        path="interfaces/perks.tga";return true;
    }
    bool GetTexturePos(const char *group,const char *name,FXYRECT &out) {
        assert((std::string(group)=="TABS" && std::string(name)=="TabDeSelected") ||
               (std::string(group)=="PERK_ENABLE" &&
                (std::string(name)=="BasicDefense" || std::string(name)=="Alchemy")));
        out=FXYRECT{};return found;
    }
} pictures;
struct Renderer {
    int TextureCreate(const char *){return 8;}
    void *GetVideoTexture(const char *){return this;}
} renderer;
constexpr int ACTION_MOUSECLICK=1,ACTION_ACTIVATE=2;
int MobileSaveSlot(const char *name) {return std::string(name)=="SAVEIMG1"?0:-1;}
struct CXI_PICTURE {
    const char *m_nodeName="TABBTN_QUEST";
    char *m_pcGroupName=nullptr;
    std::string m_mobileTexturePath="resource/textures/interfaces/tabs.tga.tx";
    std::string m_mobilePictureName;
    int m_idTex=-1;
    Renderer *m_rs=&renderer;
    void *m_pTex=nullptr;
    struct Vertex {float tu=0,tv=0;uint32_t color=0xffffffff;} m_v[4];
    struct {int left=0,top=0;} m_rect;
    bool m_bSelected=true,m_bClickable=true,m_bLockStatus=false,m_bMakeBlind=false;
    bool commands=true;
    PictureService *pPictureService=&pictures;
    bool CheckCommandUsed(int) const {return commands;}
    void MobileCollect(std::vector<MobileWidget>&)const;
    void SetNewPicture(bool,const char *);
    void SetNewPictureByGroup(const char *,const char *);
    void ChangeUV(FXYRECT &);
    void ReleasePicture();
};
#include "collect.inc"
#include "sources.inc"
int main() {
    CXI_PICTURE picture;char tabs[]="TABS",other[]="OTHER";
    picture.m_pcGroupName=tabs;picture.m_v[3].tu=picture.m_v[3].tv=.5f;
    const auto read=[&](){std::vector<MobileWidget>w;picture.MobileCollect(w);assert(w.size()==1);return w.front();};
    for(const char *node:{"TABBTN_QUEST","TABBTN_QUESTARJ","TABBTN_INFO","TABBTN_CASHBOOK","TABBTN_STATISTIC","TABBTN_SHIP_PLACE","TABBTN_STOREBOOK","TABBTN_1","TABBTN_4"}) {
        picture.m_nodeName=node;assert(read().value=="1");
        picture.m_v[3].tv=1;assert(read().value=="0");picture.m_v[3].tv=.5f;
        pictures.found=false;assert(read().value.empty());pictures.found=true;
    }
    picture.m_nodeName="TABBTN_QUEST";storm::android::mobileScreen="items";assert(read().value.empty());
    picture.m_nodeName="TABBTN_1";assert(read().value=="1");
    storm::android::mobileScreen="questbook";
    for(const char *node:{"TABBTN_QUEST_UNKNOWN","TABBTN_5","TABBTN_QUESTARJ2","ICON"}) {
        picture.m_nodeName=node;assert(read().value.empty());
    }
    picture.m_nodeName="TABBTN_QUEST";picture.m_pcGroupName=other;assert(read().value.empty());
    picture.m_pcGroupName=nullptr;assert(read().value.empty());picture.m_pcGroupName=tabs;
    picture.m_bLockStatus=true;auto w=read();assert(w.value=="1"&&!w.enabled&&w.actions.empty());
    picture.m_bLockStatus=false;w=read();assert(w.enabled&&w.actions==std::vector<std::string>{"press"});
    picture.m_nodeName="SAVEIMG1";picture.m_bMakeBlind=true;w=read();assert(w.type=="save"&&w.index==0&&w.value=="1");
    picture.m_pcGroupName=nullptr;picture.m_nodeName="PERK_PICTURE";
    picture.SetNewPictureByGroup("PERK_ENABLE","BasicDefense");
    w=read();assert(w.images.front().source.group=="PERK_ENABLE" && w.images.front().source.image=="BasicDefense");
    assert(MobileImageJson(w.images.front()).find("\"image\":\"BasicDefense\"")!=std::string::npos);
    picture.SetNewPicture(false,"portraits/player.tga");
    w=read();assert(w.images.front().source.kind.empty() && picture.m_mobilePictureName.empty());
    picture.SetNewPictureByGroup("PERK_ENABLE","Alchemy");
    assert(read().images.front().source.image=="Alchemy");
    FXYRECT custom;picture.ChangeUV(custom);assert(read().images.front().source.kind.empty());
    picture.SetNewPictureByGroup("PERK_ENABLE","BasicDefense");
    picture.SetNewPicture(true,"video");assert(picture.m_mobilePictureName.empty());
    std::vector<MobileWidget> hidden;picture.MobileCollect(hidden);assert(hidden.empty());
    picture.ReleasePicture();assert(picture.m_mobileTexturePath.empty() && picture.m_mobilePictureName.empty());
    std::cout<<"PASS real picture collection: tabs, image source atlas/file/atlas, arbitrary UV, video/release, lock and save slots\n";
}
