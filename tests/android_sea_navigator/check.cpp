#include <platform/android_sea_navigator.hpp>
#include <platform/android_ship_telemetry.hpp>
#include <cassert>
#include <limits>
#include <iostream>
using namespace storm::android;
using namespace storm::android::sea_nav;
static bool near(float a,float b){return std::abs(a-b)<.001;}
struct Bounds {
    float left=std::numeric_limits<float>::max(),top=left;
    float right=std::numeric_limits<float>::lowest(),bottom=right;
    void add(Point p){left=std::fmin(left,p.x);right=std::fmax(right,p.x);top=std::fmin(top,p.y);bottom=std::fmax(bottom,p.y);}
    float width() const {return right-left;} float height() const {return bottom-top;}
};
static float distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
static float dot(Point a,Point b){return a.x*b.x+a.y*b.y;}
static float distanceToSegment(Point p,Point a,Point b){
    Point delta{b.x-a.x,b.y-a.y};
    const float t=std::clamp(dot({p.x-a.x,p.y-a.y},delta)/dot(delta,delta),0.f,1.f);
    return distance(p,{a.x+t*delta.x,a.y+t*delta.y});
}
static void checkStroke(const Stroke &s,float width){
    const auto quad=strokeQuad(s);assert(quad);
    assert(near(distance((*quad)[0],(*quad)[1]),width));
    assert(near(distance((*quad)[2],(*quad)[3]),width));
}
static void checkReadability(){
    // Final 96dp circle has identical ink at standard and compact viewports.
    // Exercise physical density and a scaled/offset renderer viewport together:
    // sizing must use density * aperture scale, never aperture radius / 120.
    for(int density:{1000,1500,2000,3000})for(int width:{960,1280})for(float scale:{.5f,1.f,1.5f}){
        const float dp=density*.001f;
        const int height=width==960?540:720;
        const SeaNavigatorLayout layout{1,width,height,width-int(62*dp),int(90*dp),int(48*dp),density,1};
        const auto circle=aperture(layout,13,17,int(width*scale),int(height*scale));assert(circle);
        assert(near(circle->x,13+layout.x*scale)&&near(circle->y,17+layout.y*scale));
        const auto metrics=primitiveMetrics(layout,*circle);
        assert(near(metrics.dp,dp*scale)&&near(metrics.radius,48*metrics.dp));
        const Point center{circle->x,circle->y};
        // Both alphabets, every cardinal and sixteen ship headings. Measure
        // actual filled stroke quads, including slanted segment ink and caps.
        for(bool russian:{false,true})for(int heading=0;heading<16;++heading)for(int i=0;i<4;++i){
            const float angle=i*pi*.5f-heading*pi/8;
            const auto offset=polar(angle,metrics.cardinalRadius());
            const Point origin{center.x+offset.x,center.y+offset.y};
            const auto glyph=cardinalGlyph(origin,i,russian,metrics);assert(glyph.size);
            Bounds bounds;
            for(size_t j=0;j<glyph.size;++j){
                const auto &s=glyph.strokes[j];checkStroke(s,metrics.dp);
                const auto quad=strokeQuad(s);assert(quad);
                for(auto p:*quad){
                    bounds.add(p);
                    const float damageInner=circle->radius*110/120-std::max(9*circle->radius/120,metrics.functionalStroke())*.5f;
                    assert(distance(p,center)<damageInner); // letters do not cover the four damage/charge tracks
                }
                for(size_t k=0;k<4;++k)assert(distanceToSegment(center,(*quad)[k],(*quad)[(k+1)%4])+.001f>=31*metrics.dp);
            }
            assert(bounds.height()+.001f>=8*metrics.dp);
            assert(bounds.width()+.001f>=5*metrics.dp&&bounds.width()<=7*metrics.dp+.001f);
            // Upright text: the ink envelope does not rotate with its azimuth.
            const auto upright=cardinalGlyph(center,i,russian,metrics);Bounds reference;
            for(size_t j=0;j<upright.size;++j){const auto quad=strokeQuad(upright.strokes[j]);assert(quad);for(auto p:*quad)reference.add(p);}
            assert(near(bounds.width(),reference.width())&&near(bounds.height(),reference.height()));
        }
        const auto own=hull(center,0,metrics.hullLength(),metrics.hullWidth());Bounds ownInk;
        for(auto p:own)ownInk.add(p);
        assert(ownInk.height()+.001f>=8*metrics.dp&&ownInk.width()+.001f>=5*metrics.dp);
        for(int heading=0;heading<16;++heading){
            const float angle=heading*pi/8;const auto forward=polar(angle,1);
            const auto head=arrowHead(center,angle,metrics.bearingTipRadius(),metrics.arrowLength(),metrics.arrowWidth());
            for(auto p:head)assert(distance(p,center)<=30*metrics.dp+.001f);
            const Point base{(head[1].x+head[2].x)*.5f,(head[1].y+head[2].y)*.5f};
            assert(dot({head[0].x-base.x,head[0].y-base.y},forward)+.001f>=6*metrics.dp);
            assert(distance(head[1],head[2])+.001f>=4*metrics.dp);
            const auto course=chevron(center,angle,metrics.bearingTipRadius(),metrics.chevronLength(),metrics.chevronWidth());
            assert(distance(course[0],course[2])+.001f>=5*metrics.dp);
            const Point midpoint{(course[0].x+course[2].x)*.5f,(course[0].y+course[2].y)*.5f};
            assert(dot({course[1].x-midpoint.x,course[1].y-midpoint.y},forward)+.001f>=5*metrics.dp);
            checkStroke({course[0],course[1],metrics.functionalStroke()},metrics.dp);
            checkStroke({course[1],course[2],metrics.functionalStroke()},metrics.dp);
            for(auto edge:std::array<Stroke,2>{{{course[0],course[1],metrics.functionalStroke()},{course[1],course[2],metrics.functionalStroke()}}}){
                const auto quad=strokeQuad(edge);assert(quad);for(auto p:*quad)assert(distance(p,center)<=30*metrics.dp+.001f);
            }
            const auto contact=hull(center,angle,metrics.contactLength(),metrics.contactWidth());
            assert(near(distance(contact[1],contact[3]),3*metrics.dp));
            const Point stern{(contact[1].x+contact[3].x)*.5f,(contact[1].y+contact[3].y)*.5f};
            assert(near(distance(contact[0],stern),4*metrics.dp));
        }
        // Each functional arc has a real 1dp radial ink width, including the
        // unknown-bort path; background fill still ends at the aperture edge.
        for(int bort=0;bort<4;++bort){
            const float begin=bort*pi*.5f-31*pi/180,end=bort*pi*.5f+31*pi/180;
            const auto track=arcQuad(center,begin,end,circle->radius*110/120,metrics.functionalStroke());
            assert(near(distance(track[0],track[1]),metrics.dp));
            assert(near(distance(track[2],track[3]),metrics.dp));
            for(auto p:track)assert(distance(p,center)<circle->radius);
        }
        const auto fill=arcQuad(center,0,.1f,circle->radius*.5f,circle->radius);
        assert(near(distance(fill[0],center),0)&&near(distance(fill[3],center),0));
        assert(near(distance(fill[1],center),circle->radius)&&near(distance(fill[2],center),circle->radius));
        checkStroke({center,{center.x+10*metrics.dp,center.y+20*metrics.dp},metrics.functionalStroke()},metrics.dp);
        checkStroke({center,{center.x+10*metrics.dp,center.y},metrics.decorativeStroke()},.75f*metrics.dp);
    }
    assert(!strokeQuad({{0,0},{0,0},1}));
}
int main(){
    checkReadability();
    SeaNavigatorBridge bridge;
    const char *layout="4\t8\t2\tprepare\t9\t1280\t720\t1124\t228\t132\t2000\t1";
    assert(!bridge.offer("4\t8\t2\tin",10,4,8,2));
    assert(bridge.offer(layout,100,4,8,2));
    assert(bridge.layout(850,4,8,2)->id==9);assert(!bridge.layout(851,4,8,2));
    const auto *physical=bridge.layout(100,4,8,2);
    auto half=aperture(*physical,0,0,640,360);assert(half&&near(half->x,562)&&near(half->y,114)&&near(half->radius,66));
    auto full=aperture(*physical,0,0,1280,720);assert(full&&near(full->x,1124)&&near(full->radius,132));
    assert(!aperture(*physical,0,0,640,480));assert(!aperture(*physical,0,0,0,0));
    assert(!bridge.layout(100,5,8,2));assert(!bridge.layout(100,4,9,2));assert(!bridge.layout(100,4,8,3));
    assert(!bridge.offer(layout,100,5,8,2));
    assert(!bridge.offer("4\t8\t2\tprepare\t9\t1280\t720\t1200\t228\t132\t2000\t1",100,4,8,2));
    assert(!bridge.offer("4\t8\t2\tprepare\t9\t1280\t720\t1124\t228\tNaN\t2000\t1",100,4,8,2));
    assert(!bridge.offer("4\t8\t2\tprepare\t9\t1280\t720\t1124\t228\t132\t2000\t1\textra",100,4,8,2));
    assert(!bridge.offer("4\t8\t2\tprepare\t9\t1280\t720\t1124\t228\t132",100,4,8,2));
    assert(!bridge.offer(std::string(200,'0'),100,4,8,2));
    assert(bridge.offer("4\t8\t2\tin",101,4,8,2));assert(bridge.takeZoom(101,4,8,2)==-1);assert(bridge.takeZoom(101,4,8,2)==0);
    assert(bridge.offer("4\t8\t2\tout",101,4,8,2));assert(bridge.takeZoom(101,4,9,2)==0);
    assert(bridge.offer("4\t8\t2\tin",101,4,8,2));assert(bridge.offer("4\t8\t2\tclear",102,4,8,2));assert(bridge.takeZoom(102,4,8,2)==0);
    assert(bridge.offer(layout,UINT32_MAX-50,4,8,2));assert(bridge.layout(100,4,8,2));assert(!bridge.layout(800,4,8,2));
    SeaNavigatorBridge ownership;
    assert(ownership.offer(layout,100,4,8,2));
    assert(!ownership.offer("4\t8\t2\tvisible\t9",100,4,8,2)); // preparing geometry is not actual native draw
    ownership.didDraw(9);
    assert(ownership.offer("4\t8\t2\tvisible\t9",101,4,8,2));
    assert(ownership.presented(200,4,8,2,9));
    assert(ownership.offer(layout,300,4,8,2)); // ordinary heartbeat cannot revoke the visible owner
    assert(ownership.presented(301,4,8,2,9));
    assert(!ownership.offer("4\t8\t2\tprepare\t9\t1280\t720\t1120\t228\t132\t2000\t1",301,4,8,2));
    assert(std::string_view(ownership.result())=="layout-id-reused");
    assert(ownership.offer(layout,800,4,8,2));
    assert(!ownership.presented(852,4,8,2,9)); // geometry heartbeat alone cannot renew visible-pixel lease
    assert(ownership.offer("4\t8\t2\tvisible\t9",853,4,8,2));
    assert(ownership.presented(854,4,8,2,9));
    assert(!ownership.presented(854,4,9,2,9));assert(!ownership.presented(854,5,8,2,9));assert(!ownership.presented(854,4,8,3,9));
    assert(ownership.offer("4\t8\t2\tprepare\t10\t1280\t720\t1124\t228\t132\t2000\t1",855,4,8,2));
    assert(!ownership.presented(855,4,8,2,9));assert(!ownership.offer("4\t8\t2\tvisible\t10",855,4,8,2));
    ownership.didDraw(10);assert(ownership.offer("4\t8\t2\tvisible\t10",856,4,8,2));
    ownership.clear();assert(!ownership.presented(856,4,8,2,10));
    assert(ownership.offer(layout,1000,4,8,2));ownership.didDraw(9);
    assert(ownership.offer("4\t8\t2\tvisible\t9",1700,4,8,2));
    assert(ownership.offer(layout,1751,4,8,2)); // expired geometry requires a fresh native draw even with same id
    assert(!ownership.presented(1751,4,8,2,9));
    assert(!ownership.offer("4\t8\t2\tvisible\t9",1751,4,8,2));
    ownership.didDraw(9);assert(ownership.offer("4\t8\t2\tvisible\t9",1752,4,8,2));
    assert(ownership.presented(1753,4,8,2,9));
    // Reproduce runtime 2015 -> 991/1951 caption ACK churn while the same
    // complete navigator remains visible. Freshness is not ownership.
    ShipTelemetryFrame live;live.valid=true;live.ship.entityId=42;
    live.ship.hull=ShipMetric{90,100};live.ship.crew=ShipMetric{90,100};live.ship.sails=ShipMetric{90,100};live.sailState=0;
    live.ammo="balls";live.ammoType=0;live.ammoCount=88;live.captionsVisible=true;
    live.shipSpeedKnots=0;live.windSpeed=8.6;live.powderCount=208;live.dateTimeText="date";live.locationText="island";
    ShipTelemetryState liveState;liveState.update(4,live);
    const auto hud=liveState.hudRevision();
    const auto owner="4\t"+std::to_string(hud)+"\t2\t";
    SeaNavigatorBridge liveBridge;
    assert(liveBridge.offer(owner+"prepare\t1\t960\t540\t844\t171\t99\t1500\t1",10,4,hud,2));
    liveBridge.didDraw(1);assert(liveBridge.offer(owner+"visible\t1",11,4,hud,2));
    for(auto mask:{2015,991,1951,2015}){
        live.windSpeed=*live.windSpeed+.1;live.shipSpeedKnots=*live.shipSpeedKnots+.1;liveState.update(4,live);
        assert(liveState.acknowledge("4\t"+std::to_string(mask)+"\t"+std::to_string(hud)));
        assert(liveState.mask()==uint32_t(mask));
        assert(liveBridge.presented(20,liveState.revision(),liveState.hudRevision(),2,1));
    }
    live.windSpeed.reset();liveState.update(4,live);
    assert(!liveBridge.presented(21,liveState.revision(),liveState.hudRevision(),2,1)); // real source loss revokes
    auto north=project(0,100,0,400,1,80);assert(north&&near(north->x,0)&&near(north->y,-20));
    auto turn=project(0,100,pi/2,400,1,80);assert(turn&&near(turn->x,-20)&&near(turn->y,0));
    auto east=project(100,0,pi/2,400,1,80);assert(east&&near(east->x,0)&&near(east->y,-20));
    assert(project(400,0,0,400,1,80));assert(!project(401,0,0,400,1,80));
    auto zoomed=project(0,100,0,400,.5f,80);assert(zoomed&&near(zoomed->y,-40));
    assert(!project(0,0,0,400,0,80));assert(!project(0,0,std::numeric_limits<float>::quiet_NaN(),400,1,80));
    assert(near(zoom(.015f,.01f,.01f,2,true),.01f));assert(near(zoom(1.995f,.01f,.01f,2,false),2));
    Bort absent{};assert(!valid(absent));
    Bort b{true,.5f,.25f,pi/2,1,400};assert(valid(b)&&near(readyFraction(b),.375f));
    b.damage=1;assert(valid(b)&&near(readyFraction(b),0));b.charge=0;assert(valid(b));b.damage=-1;assert(!valid(b));
    ShipTelemetryFrame frame;frame.valid=true;frame.navigatorPowderWarning=false;frame.navigatorAmmoWarning=false;frame.navigatorWindState=61;frame.navigatorGeneration=42;frame.navigatorLayoutId=9;
    ShipTelemetryState telemetry;telemetry.update(1,frame);auto json=telemetry.json();
    assert(json.find("\"navigatorPowderWarning\":false")!=json.npos);assert(json.find("\"navigatorWindState\":61")!=json.npos);assert(json.find("\"navigatorLayoutId\":9")!=json.npos);
    frame.navigatorPowderWarning.reset();telemetry.update(1,frame);assert(telemetry.json().find("navigatorPowderWarning")==json.npos);
    std::cout<<"PASS production vector ink minima at 96dp across viewport/density/heading transforms, independent presentation/geometry lease, native-draw proof, expiry and structural invalidation, navigator owner/source/viewport lease, malformed/oversize requests, zoom cancel/guards, native projection and bort damage composition, absent versus false warning\n";
}
