#include "battle_navigator.h"
#ifdef __ANDROID__
#include "i_battle.h"
#include "core.h"
#include "bi_utils.h"
#include "ships_list.h"
#include "ship_base.h"
#include "os_window.hpp"
#include "shared/battle_interface/msg_control.h"
#include "island_base.h"
#include <platform/android_ui.hpp>
#include "fire_side.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace {
constexpr int kLandWindow = 512;          // cells per side of the land texture around the ship
constexpr uint32_t kLandColor = 0xffb9a46c; // the land: warm sand (ARGB)
constexpr uint32_t kShoreColor = 0x90d8ecea, kCoastColor = 0xf0302312; // the beach line and the coast ink
using storm::android::sea_nav::Point;
using storm::android::sea_nav::polar;
namespace nav=storm::android::sea_nav;
constexpr float pi=storm::android::sea_nav::pi;
struct Shapes {
    std::vector<BI_COLORONLY_VERTEX> vertices;
    float x,y,unit;
    Point at(float a,float r)const {auto p=polar(a,r*unit);return {x+p.x,y+p.y};}
    void triangle(Point a,Point b,Point c,uint32_t color) {
        for(auto p:{a,b,c})vertices.push_back({glm::vec3(p.x,p.y,1.f),.5f,color});
    }
    void line(Point a,Point b,float width,uint32_t color) {
        const auto ink=nav::strokeQuad({a,b,width});if(!ink)return;
        triangle((*ink)[0],(*ink)[1],(*ink)[2],color);triangle((*ink)[0],(*ink)[2],(*ink)[3],color);
    }
    void arc(float a,float b,float radius,float thickness,uint32_t color,bool dashed=false) {
        const int steps=std::max(1,int(std::ceil(std::abs(b-a)*60)));
        for(int i=0;i<steps;++i){if(dashed&&(i/2)%2)continue;
            float lo=a+(b-a)*float(i)/steps,hi=a+(b-a)*float(i+1)/steps;
            // Radius follows the bezel's 120-unit artwork; path thickness is
            // already physical pixels and must not shrink with that artwork.
            const auto ink=nav::arcQuad({x,y},lo,hi,radius*unit,thickness);
            triangle(ink[0],ink[1],ink[2],color);triangle(ink[0],ink[2],ink[3],color);}
    }
    void flush(VRenderer *rs) {
        if(!vertices.empty())rs->DrawPrimitiveUP(D3DPT_TRIANGLELIST,BI_COLORONLY_VERTEX_FORMAT,vertices.size()/3,
            vertices.data(),sizeof(BI_COLORONLY_VERTEX),"battle_only_color");
        vertices.clear();
    }
};
}

void BATTLE_NAVIGATOR::UpdateAndroidBorts() {
    m_androidBorts={};
    const auto *main=g_ShipList.GetMainCharacterShip();
    auto *ship=g_ShipList.GetMainCharacterShipAttr();
    if(!main||!ship)return;
    constexpr const char *names[]={"cannonf","cannonr","cannonb","cannonl"};
    for(size_t i=0;i<4;++i){
        auto *state=BIUtils::GetAttributesFromPath(main->pAttr,"Cannons","Borts",names[i],0);
        auto *spec=BIUtils::GetAttributesFromPath(ship,"Cannons","Borts",names[i],0);
        if(!state||!spec)continue; // absent side: an unknown track, never zero/ready
        auto &b=m_androidBorts[i];b.present=true;
        auto read=[](ATTRIBUTES *a,const char *key){const char *s=a->GetAttribute(key);
            auto value=s?storm::android::shipNumberValue(s):std::nullopt;
            return value?float(*value):std::numeric_limits<float>::quiet_NaN();};
        b.charge=read(state,"ChargeRatio");b.damage=read(state,"DamageRatio");
        b.range=read(state,"MaxFireDistance");b.direction=read(spec,"FireDir");b.zone=read(spec,"FireZone");
    }
}

bool BATTLE_NAVIGATOR::AndroidAvailable() const {
    if(!rs||m_renderLost||!m_shipSpeedAvailable||!m_windSpeedAvailable||!m_chargeSignalAvailable||m_curWindPic!=61||
       !std::isfinite(m_fXPos)||!std::isfinite(m_fYPos)||!std::isfinite(m_fAngle)||!std::isfinite(m_fWindAngle)||!std::isfinite(m_fShipWindAgainst)||
       m_fWorldRad<=0||m_fCurScale<=0||m_fMinScale<=0||m_fMaxScale<m_fMinScale||m_fScaleStep<=0)return false;
    // The island map is re-rendered after every change of scale: the disc goes on without it
    // for those frames instead of handing the place back to the native compass.
    for(const auto &b:m_androidBorts)if(b.present&&!storm::android::sea_nav::valid(b))return false;
    return true;
}

// The island's land for the minimap: its depth map's land cells (20 m or finer), smoothed by a
// 5-tap binomial in both directions into a field whose 128 contour is a rounded coast; halved
// levels for views wider than a window of the finest one. Built once per island (SetIsland).
void BATTLE_NAVIGATOR::BuildAndroidLand() {
    TEXTURE_RELEASE(rs,m_landTexture);
    m_landLevel=-1;m_landLevels.clear();
    auto *island=static_cast<ISLAND_BASE *>(core.GetEntityPointer(core.GetEntityId("ISLAND")));
    if(!island)return; // the open sea: no land to draw
    const auto start=std::chrono::steady_clock::now();
    IslandLandMap map;
    if(!island->GetLandMap(map)||map.size<8||!(map.stepX>0)||!(map.stepZ>0)||map.land.size()!=size_t(map.size)*map.size){
        spdlog::error("navigator: the island has no usable depth map (size {}); the minimap shows no land",map.size);
        return;
    }
    const uint32_t n=map.size;
    AndroidLandLevel level{n,map.stepX,map.stepZ,storm::android::sea_nav::landField(map.land,n)};
    m_landX0=map.x0;m_landZ0=map.z0;
    size_t land=0;for(auto v:map.land)land+=v;
    m_landLevels.push_back(std::move(level));
    while(m_landLevels.back().size>kLandWindow/2){
        const auto &fine=m_landLevels.back();
        AndroidLandLevel coarse{fine.size/2,fine.stepX*2,fine.stepZ*2,std::vector<uint8_t>(size_t(fine.size/2)*(fine.size/2))};
        for(uint32_t z=0;z<coarse.size;++z)for(uint32_t x=0;x<coarse.size;++x){
            const size_t i=size_t(2*z)*fine.size+2*x;
            coarse.field[size_t(z)*coarse.size+x]=uint8_t((fine.field[i]+fine.field[i+1]+fine.field[i+fine.size]+fine.field[i+fine.size+1]+2)/4);}
        m_landLevels.push_back(std::move(coarse));
    }
    spdlog::info("navigator: land of the island: {} cells of {:.1f} x {:.1f} m, {} of them land, {} levels, {} ms",n,map.stepX,map.stepZ,land,
        m_landLevels.size(),std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
}

// The texture holds kLandWindow cells of one level around the ship: the finest level whose
// window covers the view with room to sail. Refilled only when the view leaves it or the zoom
// needs another level.
bool BATTLE_NAVIGATOR::AndroidLandWindow(float worldRadius) const {
    if(m_landLevels.empty())return false;
    int levelIndex=-1;
    for(size_t i=0;i<m_landLevels.size();++i){const auto &l=m_landLevels[i];
        if(worldRadius/std::min(l.stepX,l.stepZ)+2<kLandWindow*.375f){levelIndex=int(i);break;}}
    if(levelIndex<0){
        static bool told=false;
        if(!told){told=true;spdlog::error("navigator: the view ({:.0f} m) is wider than every land level; the minimap shows no land",worldRadius);}
        return false;
    }
    const auto &l=m_landLevels[size_t(levelIndex)];
    const float cx=(m_fXPos-m_landX0)/l.stepX,cz=(m_fYPos-m_landZ0)/l.stepZ;
    const float r=worldRadius/std::min(l.stepX,l.stepZ)+2;
    const bool inside=m_landLevel==levelIndex&&cx-r>=m_landWinX&&cx+r<=m_landWinX+kLandWindow&&cz-r>=m_landWinZ&&cz+r<=m_landWinZ+kLandWindow;
    if(inside&&m_landTexture>=0)return true;
    if(m_landTexture<0){
        // R, G, B, A in memory
        m_landTexture=rs->TextureCreate(kLandWindow,kLandWindow,1,0,D3DFMT_A8B8G8R8,D3DPOOL_MANAGED);
        if(m_landTexture<0){spdlog::error("navigator: cannot create the {0}x{0} land texture; the minimap shows no land",kLandWindow);return false;}
    }
    const int winX=int(std::floor(cx))-kLandWindow/2,winZ=int(std::floor(cz))-kLandWindow/2;
    D3DLOCKED_RECT locked;
    if(!rs->TextureLock(m_landTexture,0,locked,0)){spdlog::error("navigator: cannot lock the land texture");return false;}
    const uint8_t r8=uint8_t(kLandColor>>16),g8=uint8_t(kLandColor>>8),b8=uint8_t(kLandColor);
    for(int z=0;z<kLandWindow;++z){
        auto *out=static_cast<uint8_t *>(locked.pBits)+size_t(z)*locked.Pitch;
        const int fz=winZ+z;
        for(int x=0;x<kLandWindow;++x,out+=4){
            const int fx=winX+x;
            const uint8_t a=fx>=0&&fz>=0&&fx<int(l.size)&&fz<int(l.size)?l.field[size_t(fz)*l.size+fx]:0;
            out[0]=r8;out[1]=g8;out[2]=b8;out[3]=a;
        }
    }
    rs->TextureUnlock(m_landTexture,0);
    m_landLevel=levelIndex;m_landWinX=winX;m_landWinZ=winZ;
    return true;
}

bool BATTLE_NAVIGATOR::DrawAndroid() const {
    using namespace storm::android;
    m_androidLayoutId=0;
    const auto *owner=static_cast<const BATTLE_INTERFACE *>(m_pOwnerEI);
    const auto *layout=seaNavigator.layout(shipTelemetry.revision(),shipTelemetry.hudRevision(),m_navigatorGeneration);
    D3DVIEWPORT9 viewport;rs->GetViewport(&viewport);
    const auto aperture=layout?sea_nav::aperture(*layout,viewport.X,viewport.Y,viewport.Width,viewport.Height):std::nullopt;
    const auto window=core.GetWindow()?core.GetWindow()->GetWindowSize():storm::WindowSize{};
    const auto screen=rs->GetScreenSize();
    const char *reason=!owner?"owner":owner->AndroidCommandActive()?"commands":!AndroidAvailable()?"source":
        !mobileScreen.empty()?"screen":loadingState==1?"loading":cinematicState==1?"cinematic":!layout?"no-layout":
        !aperture?"viewport-aspect":"ready";
    static std::string lastReason;
    const std::string diagnostic=std::string(reason)+" revision="+std::to_string(shipTelemetry.revision())+" hud="+std::to_string(shipTelemetry.hudRevision())+
        " source="+std::to_string(m_navigatorGeneration)+" viewport="+std::to_string(viewport.Width)+"x"+std::to_string(viewport.Height)+
        " SDL="+std::to_string(window.width)+"x"+std::to_string(window.height)+" renderer="+std::to_string(screen.x)+"x"+std::to_string(screen.y)+
        " scale="+(aperture?std::to_string(aperture->scale):"unavailable")+" layout="+(layout?std::to_string(layout->id)+":"+std::to_string(layout->width)+"x"+std::to_string(layout->height):"absent")+
        " presented="+std::to_string(layout&&seaNavigator.presented(shipTelemetry.revision(),shipTelemetry.hudRevision(),m_navigatorGeneration,layout->id));
    if(diagnostic!=lastReason){lastReason=diagnostic;traceContextAction("navigator-gate","draw",diagnostic+" mask="+std::to_string(mobileShipHudMask()));}
    if(std::string_view(reason)!="ready")return false;
    const float cx=aperture->x,cy=aperture->y,radius=aperture->radius;
    Shapes mesh{{},cx,cy,radius/120.f};
    const auto ink=sea_nav::primitiveMetrics(*layout,*aperture);
    const glm::mat4 world;rs->SetTransform(D3DTS_WORLD,storm::math::d3dMatrix(world));
    // The minimap (V2-Sea-Sailing, owner's round-5 additions): a dark disc, heading up, with the
    // island map; per gun side its firing envelope (range and arc) and, along the rim, how far
    // it is loaded; the side the camera faces (it is the side that fires) lit; the wind as an
    // arrow across the disc, longer the stronger it blows; contacts as dots, the own ship as a
    // triangle in the middle.
    const float inner=radius-ink.dp; // inside the thin rim
    mesh.arc(0,2*pi,60,inner,0xc0183b48);mesh.flush(rs);
    // The land: the island's depth map as a sand fill cut at the coast (alpha test on the
    // smoothed field, sharp at every zoom), then a light beach line and the dark coast ink along
    // the same contour, traced per frame over the cells in view (marching squares).
    const float worldRadius=m_fWorldRad*m_fCurScale;
    const float metresPerPixel=worldRadius/inner;
    const float headingCos=std::cos(m_fAngle),headingSin=std::sin(m_fAngle);
    // world offset from the ship -> disc point, heading up (sea_nav::project's equations)
    const auto toDisc=[&](float dx,float dz){const float x=dx/metresPerPixel,y=dz/metresPerPixel;
        return Point{cx+x*headingCos-y*headingSin,cy-(y*headingCos+x*headingSin)};};
    if(AndroidLandWindow(worldRadius)){
        const auto &level=m_landLevels[size_t(m_landLevel)];
        const float texel=1.f/kLandWindow;
        std::array<BI_ONETEXTURE_VERTEX,RADIAL_QUANTITY+2> map{};
        const auto vertex=[&](float sx,float sy){
            // disc offset -> world offset (the inverse rotation), -> texture of the window
            const float dx=(sx*headingCos-sy*headingSin)*metresPerPixel,dz=(-sx*headingSin-sy*headingCos)*metresPerPixel;
            return BI_ONETEXTURE_VERTEX{glm::vec3(cx+sx,cy+sy,1),.5f,
                ((m_fXPos+dx-m_landX0)/level.stepX-m_landWinX)*texel,((m_fYPos+dz-m_landZ0)/level.stepZ-m_landWinZ)*texel};
        };
        map[0]=vertex(0,0);
        for(int i=0;i<=RADIAL_QUANTITY;++i){const float a=2*pi*i/RADIAL_QUANTITY;map[i+1]=vertex(std::sin(a)*inner,std::cos(a)*inner);}
        rs->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);rs->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        rs->TextureSet(0,m_landTexture);
        rs->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,BI_ONETEX_VERTEX_FORMAT,RADIAL_QUANTITY,map.data(),sizeof(BI_ONETEXTURE_VERTEX),"battle_island_land");
        rs->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);rs->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        // The coast: the 127.5 contour between cell centres, as the bilinear texture has it.
        std::vector<std::pair<Point,Point>> coast;
        const int n=int(level.size);
        const float ox=(m_fXPos-m_landX0)/level.stepX-.5f,oz=(m_fYPos-m_landZ0)/level.stepZ-.5f; // the ship in cell-centre units
        const int reachX=int(std::ceil(worldRadius/level.stepX))+1,reachZ=int(std::ceil(worldRadius/level.stepZ))+1;
        const float limit=inner*inner;
        const auto inside=[&](Point v){const float dx=v.x-cx,dy=v.y-cy;return dx*dx+dy*dy<=limit;};
        const int sx=int(std::floor(ox)),sz=int(std::floor(oz));
        sea_nav::coastSegments(level.field,n,sx-reachX,sx+reachX,sz-reachZ,sz+reachZ,[&](float ax,float az,float bx,float bz){
            Point p=toDisc((ax-ox)*level.stepX,(az-oz)*level.stepZ),q=toDisc((bx-ox)*level.stepX,(bz-oz)*level.stepZ);
            const bool pin=inside(p),qin=inside(q);
            if(!pin&&!qin)return;
            if(!pin||!qin){ // the outside end onto the rim
                const Point i=pin?p:q,o=pin?q:p;float lo=0,hi=1;
                for(int k=0;k<12;++k){const float m=(lo+hi)*.5f;(inside({i.x+(o.x-i.x)*m,i.y+(o.y-i.y)*m})?lo:hi)=m;}
                (pin?q:p)=Point{i.x+(o.x-i.x)*lo,i.y+(o.y-i.y)*lo};
            }
            coast.emplace_back(p,q);
        });
        for(const auto &[a,b]:coast)mesh.line(a,b,2.6f*ink.dp,kShoreColor);
        mesh.flush(rs);
        for(const auto &[a,b]:coast)mesh.line(a,b,1.1f*ink.dp,kCoastColor);
        mesh.flush(rs);
    }
    const auto at=[&](float angle,float r){const auto p=polar(angle,r);return Point{cx+p.x,cy+p.y};};
    // The gun side the camera looks at (the view's forward against the heading).
    int facing=-1;
    {
        glm::mat4 view;rs->GetTransform(D3DTS_VIEW,storm::math::d3dMatrix(view));
        storm::math::invertRigid(view);
        const auto forward=storm::math::vz(view);
        const float look=std::atan2(forward.x,forward.z)-m_fAngle;
        // The side the camera fire picks (fire_side.hpp): the nearest one over the full circle.
        facing=storm::fire_side::nearest(look,4,[&](int i)->std::optional<storm::fire_side::Side>{const auto &b=m_androidBorts[i];
            if(!b.present||b.damage>=1)return std::nullopt;return storm::fire_side::Side{b.direction,b.zone};});
    }
    // Firing envelopes: a quiet wedge per side out to its range, the facing one lit.
    for(int i=0;i<4;++i){const auto &b=m_androidBorts[i];if(!b.present)continue;
        const float reach=std::min(inner,b.range*inner/(m_fWorldRad*m_fCurScale));
        const float begin=b.direction-b.zone*.5f;
        const uint32_t fill=i==facing?0x46e8cf8a:0x1ad9bd7c,edge=i==facing?0xb0f0d898:0x50d9bd7c;
        const int steps=16;
        for(int k=0;k<steps;++k){const auto a=at(begin+b.zone*k/steps,reach),c=at(begin+b.zone*(k+1)/steps,reach);
            mesh.triangle({cx,cy},a,c,fill);mesh.line(a,c,ink.dp,edge);}
        mesh.line({cx,cy},at(begin,reach),ink.dp*.75f,edge);mesh.line({cx,cy},at(begin+b.zone,reach),ink.dp*.75f,edge);
    }
    mesh.flush(rs);
    // Ships as small hulls pointing where they sail (bow, shoulders, square stern), the length
    // their real one on the map's scale between 7 and 14 dp, ringed in dark ink to stand out on
    // sea and land alike.
    const auto hull=[&](Point c,float angle,float length,uint32_t color,uint32_t outline,float rim){
        const float w=length*.36f;
        const auto shape=[&](float grow){
            const float l=length*.5f+grow,h=w*.5f+grow;
            return std::array<Point,6>{sea_nav::localPoint(c,angle,0,-l),sea_nav::localPoint(c,angle,h,-l*.18f),
                sea_nav::localPoint(c,angle,h*.86f,l),sea_nav::localPoint(c,angle,-h*.86f,l),
                sea_nav::localPoint(c,angle,-h,-l*.18f),sea_nav::localPoint(c,angle,0,-l)};};
        for(const auto &[shape_,ink_]:{std::pair{shape(rim),outline},std::pair{shape(0),color}})
            for(int i=1;i<5;++i)mesh.triangle(shape_[0],shape_[i],shape_[i+1],ink_);
    };
    const auto shipLength=[&](const SHIP_DESCRIBE_LIST::SHIP_DESCR *ship){
        const float metres=ship&&ship->pShip?ship->pShip->GetBoxsize().z:0.f;
        return std::clamp(std::isfinite(metres)?metres/metresPerPixel:0.f,7*ink.dp,14*ink.dp);};
    // Every eligible contact in the colours of the design (enemy, ally, neutral); a script's
    // explicit colour for a ship still wins, the dead are grey.
    const auto *main=g_ShipList.GetMainCharacterShip();
    for(auto *ship=g_ShipList.GetShipRoot();ship;ship=ship->next){
        if(ship==main||!ship->pShip)continue;
        const auto pos=ship->pShip->GetPos();
        const auto p=sea_nav::project(pos.x-m_fXPos,pos.z-m_fYPos,m_fAngle,m_fWorldRad,m_fCurScale,inner);
        const float length=shipLength(ship);
        if(!p||std::hypot(p->x,p->y)>inner-length*.4f)continue;
        uint32_t color=ship->dwShipColor;
        if(!color)color=ship->relation==BI_RELATION_ENEMY?0xffd2503f:ship->relation==BI_RELATION_FRIEND?0xff8cc4df:0xffe0c487;
        if(ship->isDead)color=0xff7d7d78;
        const float heading=ship->pShip->GetAng().y-m_fAngle;
        hull({cx+p->x,cy+p->y},std::isfinite(heading)?heading:0.f,length,color,0xe0101418,ink.dp);
    }
    mesh.flush(rs);
    // Readiness along the rim: per side a band over its arc, gold where loaded (all gold =
    // ready to fire), teal while it reloads, rust for the share of its guns that is broken.
    for(int i=0;i<4;++i){const auto &b=m_androidBorts[i];if(!b.present)continue;
        const float half=std::min(b.zone*.5f,pi/5),begin=b.direction-half,end=b.direction+half;
        const float band=120.f*(inner-3.5f*ink.dp)/radius,thick=4*ink.dp;
        mesh.arc(begin,end,band,thick,0x80202a2e);
        const float ready=begin+(end-begin)*sea_nav::readyFraction(b),broken=end-(end-begin)*std::clamp(b.damage,0.f,1.f);
        if(ready>begin)mesh.arc(begin,ready,band,thick,b.charge>=1?0xffefd794:0xff8ab4b7);
        if(b.damage>0)mesh.arc(broken,end,band,thick,0xffbf7659);
    }
    // The wind as on the world-map compass: a dot on the side it blows from, a dashed streamer
    // trailing downwind through the centre, longer the stronger it blows.
    if(m_windSpeedAvailable){
        const float to=m_fWindAngle-m_fAngle;
        const float strength=std::clamp(static_cast<float>(storm::android::shipDisplayedSpeed(m_fWindStrength).value_or(0.0))/20.f,0.f,1.f);
        constexpr uint32_t streamer=0xe69cc3e6; // windStreamer
        const Point head=at(to+pi,.22f*inner),tail=at(to,(.15f+.8f*strength)*inner);
        const float length=std::hypot(tail.x-head.x,tail.y-head.y),dash=4*ink.dp,gap=3*ink.dp;
        for(float t=0;t<length;t+=dash+gap){const float t1=std::min(length,t+dash);
            mesh.line({head.x+(tail.x-head.x)*t/length,head.y+(tail.y-head.y)*t/length},
                      {head.x+(tail.x-head.x)*t1/length,head.y+(tail.y-head.y)*t1/length},2*ink.dp,streamer);}
        const Point dot=at(to+pi,.22f*inner+2.5f*ink.dp);
        for(int i=0;i<16;++i){const auto p=polar(2*pi*i/16,2.6f*ink.dp),q=polar(2*pi*(i+1)/16,2.6f*ink.dp);
            mesh.triangle(dot,{dot.x+p.x,dot.y+p.y},{dot.x+q.x,dot.y+q.y},streamer|0xff000000);}
        mesh.flush(rs);
    }
    // the own ship, heading up, in the cream of the HUD's text
    hull({cx,cy},0.f,std::max(shipLength(main),10*ink.dp),0xfff4ead2,0xf0101418,1.2f*ink.dp);
    // the thin brass rim of the design
    mesh.arc(0,2*pi,120.f*(radius-.5f*ink.dp)/radius,std::max(1.f,ink.dp),0xc0a98948);
    mesh.flush(rs);
    m_androidLayoutId=layout->id;
    seaNavigator.didDraw(layout->id);
    return true;
}
#endif
