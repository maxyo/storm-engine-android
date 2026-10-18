#include "battle_navigator.h"
#ifdef __ANDROID__
#include "i_battle.h"
#include "core.h"
#include "bi_utils.h"
#include "ships_list.h"
#include "ship_base.h"
#include "os_window.hpp"
#include "shared/battle_interface/msg_control.h"
#include <platform/android_ui.hpp>
#include <algorithm>
#include <vector>

namespace {
using storm::android::sea_nav::Point;
using storm::android::sea_nav::polar;
namespace nav=storm::android::sea_nav;
constexpr float pi=storm::android::sea_nav::pi;
struct Shapes {
    std::vector<BI_COLORONLY_VERTEX> vertices;
    float x,y,unit;
    Point at(float a,float r)const {auto p=polar(a,r*unit);return {x+p.x,y+p.y};}
    void triangle(Point a,Point b,Point c,uint32_t color) {
        for(auto p:{a,b,c})vertices.push_back({CVECTOR(p.x,p.y,1.f),.5f,color});
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
    void flush(VDX9RENDER *rs) {
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
       m_fWorldRad<=0||m_fCurScale<=0||m_fMinScale<=0||m_fMaxScale<m_fMinScale||m_fScaleStep<=0||
       (m_islandExpected&&(!m_bYesIsland||!m_pIslandTexture||!m_islandTextureReady||m_fIslandRadius<=0)))return false;
    for(const auto &b:m_androidBorts)if(b.present&&!storm::android::sea_nav::valid(b))return false;
    return true;
}

bool BATTLE_NAVIGATOR::AndroidPresented() const {
    using namespace storm::android;
    return m_androidDrawn && seaNavigator.presented(SDL_GetTicks(),shipTelemetry.revision(),shipTelemetry.hudRevision(),
        m_navigatorGeneration,m_androidLayoutId);
}

bool BATTLE_NAVIGATOR::DrawAndroid() const {
    using namespace storm::android;
    m_androidLayoutId=0;
    const auto *owner=static_cast<const BATTLE_INTERFACE *>(m_pOwnerEI);
    const auto *layout=seaNavigator.layout(SDL_GetTicks(),shipTelemetry.revision(),shipTelemetry.hudRevision(),m_navigatorGeneration);
    D3DVIEWPORT9 viewport;rs->GetViewport(&viewport);
    const auto aperture=layout?sea_nav::aperture(*layout,viewport.X,viewport.Y,viewport.Width,viewport.Height):std::nullopt;
    const auto window=core.GetWindow()?core.GetWindow()->GetWindowSize():storm::WindowSize{};
    const auto screen=rs->GetScreenSize();
    const char *reason=!owner?"owner":owner->AndroidCommandActive()?"commands":!AndroidAvailable()?"source":
        !mobileScreen.empty()?"screen":loadingState==1?"loading":cinematicState==1?"cinematic":!layout?"layout-lease":
        !aperture?"viewport-aspect":"ready";
    static std::string lastReason;
    const std::string diagnostic=std::string(reason)+" revision="+std::to_string(shipTelemetry.revision())+" hud="+std::to_string(shipTelemetry.hudRevision())+
        " source="+std::to_string(m_navigatorGeneration)+" viewport="+std::to_string(viewport.Width)+"x"+std::to_string(viewport.Height)+
        " SDL="+std::to_string(window.width)+"x"+std::to_string(window.height)+" renderer="+std::to_string(screen.x)+"x"+std::to_string(screen.y)+
        " scale="+(aperture?std::to_string(aperture->scale):"unavailable")+" layout="+(layout?std::to_string(layout->id)+":"+std::to_string(layout->width)+"x"+std::to_string(layout->height):"absent")+
        " presented="+std::to_string(layout&&seaNavigator.presented(SDL_GetTicks(),shipTelemetry.revision(),shipTelemetry.hudRevision(),m_navigatorGeneration,layout->id));
    if(diagnostic!=lastReason){lastReason=diagnostic;traceContextAction("navigator-gate","draw",diagnostic+" mask="+std::to_string(mobileShipHudMask()));}
    if(std::string_view(reason)!="ready")return false;
    const float cx=aperture->x,cy=aperture->y,radius=aperture->radius;
    Shapes mesh{{},cx,cy,radius/120.f};
    const auto ink=sea_nav::primitiveMetrics(*layout,*aperture);
    const float functional=ink.functionalStroke(),decorative=ink.decorativeStroke();
    const CMatrix world;rs->SetTransform(D3DTS_WORLD,world);
    mesh.arc(0,2*pi,60,radius,0xff183b48);mesh.flush(rs);
    // Reuse the real island render target and exactly the original texture-space
    // center, world radius/scale, and heading. No island artwork is bundled.
    if(m_bYesIsland){
        std::array<BI_ONETEXTURE_VERTEX,RADIAL_QUANTITY+2> map{};
        const float tx=.5f+(m_fXPos-m_fXIsland)/m_fIslandWidth,ty=.5f-(m_fYPos-m_fYIsland)/m_fIslandHeight;
        const float tr=m_fWorldRad*m_fCurScale/m_fIslandRadius;
        map[0]={CVECTOR(cx,cy,1),.5f,tx,ty};
        for(int i=0;i<=RADIAL_QUANTITY;++i){float a=2*pi*i/RADIAL_QUANTITY;
            map[i+1]={CVECTOR(cx+std::sin(a)*radius*93/120,cy+std::cos(a)*radius*93/120,1),.5f,
                tx+tr*std::sin(a-m_fAngle),ty+tr*std::cos(a-m_fAngle)};}
        rs->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);rs->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        rs->SetTexture(0,m_pIslandTexture);
        rs->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,BI_ONETEX_VERTEX_FORMAT,RADIAL_QUANTITY,map.data(),sizeof(BI_ONETEXTURE_VERTEX),"battle_island");
        rs->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);rs->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    }
    // Four independent real firing envelopes, below contacts and needles.
    for(const auto &b:m_androidBorts)if(b.present){
        const float distance=std::min(93.f,b.range*93.f/(m_fWorldRad*m_fCurScale));
        const float begin=b.direction-b.zone*.5f;
        for(int i=0;i<BI_ONESIDE_SIZE-1;++i){
            auto a=mesh.at(begin+b.zone*i/(BI_ONESIDE_SIZE-1),distance),c=mesh.at(begin+b.zone*(i+1)/(BI_ONESIDE_SIZE-1),distance);
            mesh.triangle({cx,cy},a,c,0x30d9bd7c);mesh.line(a,c,functional,0x80d9bd7c);
        }
        mesh.line({cx,cy},mesh.at(begin,distance),functional,0x80d9bd7c);
        mesh.line({cx,cy},mesh.at(begin+b.zone,distance),functional,0x80d9bd7c);
    }
    // Complete eligible ship list, including non-companions. Native relation and
    // explicit override precedence are retained. Every contact keeps its heading.
    const auto *main=g_ShipList.GetMainCharacterShip();
    for(auto *ship=g_ShipList.GetShipRoot();ship;ship=ship->next){
        if(ship==main)continue;
        auto pos=ship->pShip->GetPos();
        auto p=sea_nav::project(pos.x-m_fXPos,pos.z-m_fYPos,m_fAngle,m_fWorldRad,m_fCurScale,radius*93/120);
        if(!p)continue;
        uint32_t color=ship->dwShipColor;
        if(!color)color=ship->relation==BI_RELATION_ENEMY?m_dwEnemyShipColor:ship->relation==BI_RELATION_FRIEND?m_dwFrendShipColor:ship->relation==BI_RELATION_NEUTRAL?m_dwNeutralShipColor:0xffffffff;
        if(ship->isDead)color=m_dwDeadShipColor;
        const float angle=ship->pShip->GetAng().y-m_fAngle;
        const Point origin{cx+p->x,cy+p->y};
        const auto contact=sea_nav::hull(origin,angle,ink.contactLength(),ink.contactWidth());
        const auto &a=contact[0],&b=contact[1],&c=contact[2],&d=contact[3];
        if(!ship->isDead){mesh.triangle(a,b,c,color);mesh.triangle(a,c,d,color);}
        for(auto pair:std::array<std::pair<Point,Point>,4>{{{a,b},{b,c},{c,d},{d,a}}})
            mesh.line(pair.first,pair.second,decorative,ship->isDead?color:0xfff9edd0);
        if(ship->isDead){
            auto vertex=[&](float x,float y){return sea_nav::localPoint(origin,angle,x*ink.dp,y*ink.dp);};
            mesh.line(vertex(-1.5f,-1.5f),vertex(1.5f,1.5f),functional,color);
            mesh.line(vertex(1.5f,-1.5f),vertex(-1.5f,1.5f),functional,color);
        }
    }
    // Copper bezel and native-heading graduation.
    mesh.arc(0,2*pi,106,26*mesh.unit,0xff201d16);
    mesh.arc(0,2*pi,118,std::max(2*mesh.unit,decorative),0xffe3c989);
    mesh.arc(0,2*pi,115,std::max(3*mesh.unit,decorative),0xffa98948);
    mesh.arc(0,2*pi,103,decorative,0xff947b4c);
    for(int i=0;i<72;++i){
        // Leave breathing room around the larger upright cardinal ink. The
        // graduation and letters still share the original native azimuths.
        const float phase=2*pi*i/72;
        if(std::abs(std::remainder(phase,pi*.5f))<.20f)continue;
        const float a=phase-m_fAngle;
        mesh.line(mesh.at(a,i%6?99:96),mesh.at(a,103),decorative,0xffe0cda0);
    }
    // Vector glyphs retain their locale and native angles, but their ink is
    // sized in dp rather than proportionally shrinking with the circle.
    for(int i=0;i<4;++i){
        const auto offset=polar(i*pi*.5f-m_fAngle,ink.cardinalRadius());
        const auto glyph=sea_nav::cardinalGlyph({cx+offset.x,cy+offset.y},i,layout->russian,ink);
        for(size_t j=0;j<glyph.size;++j){const auto &s=glyph.strokes[j];mesh.line(s.a,s.b,s.width,0xfff3dfb1);}
    }
    for(int i=0;i<4;++i){const auto &b=m_androidBorts[i];float begin=i*pi*.5f-31*pi/180,end=i*pi*.5f+31*pi/180;
        mesh.arc(begin,end,110,std::max(8*mesh.unit,functional),0xff405357);
        if(!b.present){mesh.arc(begin,end,110,functional,0xffb4b29a,true);continue;}
        const float undamaged=end-(end-begin)*b.damage,ready=begin+(end-begin)*sea_nav::readyFraction(b);
        mesh.arc(begin,ready,110,std::max(8*mesh.unit,functional),b.charge==1?0xffefd794:0xff8ab4b7);
        if(b.charge<1)mesh.arc(ready,undamaged,110,std::max(3*mesh.unit,functional),0xffb4b29a,true);
        if(b.damage>0){mesh.arc(undamaged,end,110,std::max(9*mesh.unit,functional),0xffbf7659);
            for(float a=undamaged;a<end;a+=.07f)mesh.line(mesh.at(a,106),mesh.at(std::min(a+.035f,end),114),functional,0xfff1bb8a);}
    }
    // No camera yaw or cosmetic separation: angles are the original equations.
    const float wind=m_fWindAngle-m_fAngle;
    for(float course:{wind-m_fShipWindAgainst,wind+m_fShipWindAgainst}){
        const auto v=sea_nav::chevron({cx,cy},course,ink.bearingTipRadius(),ink.chevronLength(),ink.chevronWidth());
        mesh.line(v[0],v[1],functional,0xffd4b377);mesh.line(v[1],v[2],functional,0xffd4b377);
    }
    mesh.line(mesh.at(wind,-66),mesh.at(wind,71),functional,0xffd2a65d);
    const auto head=sea_nav::arrowHead({cx,cy},wind,ink.bearingTipRadius(),ink.arrowLength(),ink.arrowWidth());
    mesh.triangle(head[0],head[1],head[2],0xffe3b86c);
    const auto feather=sea_nav::chevron({cx,cy},wind-pi,54*mesh.unit,-ink.chevronLength(),ink.chevronWidth());
    mesh.line(feather[0],feather[1],functional,0xffd2a65d);mesh.line(feather[1],feather[2],functional,0xffd2a65d);
    const auto own=sea_nav::hull({cx,cy},0,ink.hullLength(),ink.hullWidth());
    mesh.triangle(own[0],own[1],own[2],0xfff9edd0);mesh.triangle(own[0],own[2],own[3],0xfff9edd0);
    for(int i=0;i<4;++i)mesh.arc(i*pi*.5f+pi*.25f-.012f,i*pi*.5f+pi*.25f+.012f,117,std::max(3*mesh.unit,decorative),0xfff4dfac);
    mesh.flush(rs);
    m_androidLayoutId=layout->id;
    seaNavigator.didDraw(layout->id);
    return true;
}
#endif
