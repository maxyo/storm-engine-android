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
       m_fWorldRad<=0||m_fCurScale<=0||m_fMinScale<=0||m_fMaxScale<m_fMinScale||m_fScaleStep<=0)return false;
    // The island map is re-rendered after every change of scale: the disc goes on without it
    // for those frames instead of handing the place back to the native compass.
    for(const auto &b:m_androidBorts)if(b.present&&!storm::android::sea_nav::valid(b))return false;
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
    // The V2 minimap (V2-Sea-Sailing): a bare dark disc, heading up, with the island map,
    // the contacts as dots and the own ship as a triangle in the middle. No bezel, letters,
    // wind or gun arcs: the fire button and the readouts under the disc carry those.
    const float inner=radius-ink.dp; // inside the thin rim
    mesh.arc(0,2*pi,60,inner,0xc0183b48);mesh.flush(rs);
    // Reuse the real island render target and exactly the original texture-space
    // center, world radius/scale, and heading. No island artwork is bundled.
    if(m_bYesIsland&&m_pIslandTexture&&m_islandTextureReady&&m_fIslandRadius>0){
        std::array<BI_ONETEXTURE_VERTEX,RADIAL_QUANTITY+2> map{};
        const float tx=.5f+(m_fXPos-m_fXIsland)/m_fIslandWidth,ty=.5f-(m_fYPos-m_fYIsland)/m_fIslandHeight;
        const float tr=m_fWorldRad*m_fCurScale/m_fIslandRadius;
        map[0]={glm::vec3(cx,cy,1),.5f,tx,ty};
        for(int i=0;i<=RADIAL_QUANTITY;++i){float a=2*pi*i/RADIAL_QUANTITY;
            map[i+1]={glm::vec3(cx+std::sin(a)*inner,cy+std::cos(a)*inner,1),.5f,
                tx+tr*std::sin(a-m_fAngle),ty+tr*std::cos(a-m_fAngle)};}
        rs->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);rs->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        rs->SetTexture(0,m_pIslandTexture);
        rs->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,BI_ONETEX_VERTEX_FORMAT,RADIAL_QUANTITY,map.data(),sizeof(BI_ONETEXTURE_VERTEX),"battle_island");
        rs->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);rs->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    }
    const auto dot=[&](Point c,float r,uint32_t color){
        for(int i=0;i<16;++i){const auto p=polar(2*pi*i/16,r),q=polar(2*pi*(i+1)/16,r);
            mesh.triangle(c,{c.x+p.x,c.y+p.y},{c.x+q.x,c.y+q.y},color);}
    };
    // Every eligible contact as a dot in the colours of the design (enemy, ally, neutral);
    // a script's explicit colour for a ship still wins, the dead are grey.
    const auto *main=g_ShipList.GetMainCharacterShip();
    for(auto *ship=g_ShipList.GetShipRoot();ship;ship=ship->next){
        if(ship==main)continue;
        auto pos=ship->pShip->GetPos();
        auto p=sea_nav::project(pos.x-m_fXPos,pos.z-m_fYPos,m_fAngle,m_fWorldRad,m_fCurScale,inner-4*ink.dp);
        if(!p)continue;
        uint32_t color=ship->dwShipColor;
        if(!color)color=ship->relation==BI_RELATION_ENEMY?0xffc8483a:ship->relation==BI_RELATION_FRIEND?0xff8cc4df:0xffd9bd7c;
        if(ship->isDead)color=0xff7d7d78;
        dot({cx+p->x,cy+p->y},3*ink.dp,color);
    }
    // the own ship, heading up
    mesh.triangle({cx,cy-7*ink.dp},{cx+5*ink.dp,cy+5*ink.dp},{cx-5*ink.dp,cy+5*ink.dp},0xfff3e3bd);
    // the thin brass rim of the design
    mesh.arc(0,2*pi,120.f*(radius-.5f*ink.dp)/radius,std::max(1.f,ink.dp),0xc0a98948);
    mesh.flush(rs);
    m_androidLayoutId=layout->id;
    seaNavigator.didDraw(layout->id);
    return true;
}
#endif
