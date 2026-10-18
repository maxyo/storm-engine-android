#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace storm::android {
// Native owns map, projection and gameplay. Android supplies only the measured
// aperture and a short lease for its fully drawn captions and touch controls.
struct SeaNavigatorLayout {
    uint64_t id{}; int width{}, height{}, x{}, y{}, radius{}, densityMilli{}, russian{};
    bool operator==(const SeaNavigatorLayout &) const = default;
};
class SeaNavigatorBridge {
    uint64_t revision_{}, hud_{}, source_{};
    uint32_t tick_{}, presentedTick_{};
    uint64_t nativeDrawnId_{}, presentedId_{};
    std::optional<SeaNavigatorLayout> layout_;
    int zoom_{};
    const char *result_="initial";
public:
    const char *result() const { return result_; }
    void clear() { layout_.reset(); zoom_=0; nativeDrawnId_=presentedId_=0; }
    bool offer(std::string_view request, uint32_t tick, uint64_t revision, uint64_t hud, uint64_t source) {
        const auto reject=[&](const char *why){result_=why;return false;};
        if(request.size()>160)return reject("oversize");
        std::array<std::string_view,12> parts{};size_t n=0;
        while(n<parts.size()) {const auto t=request.find('\t');parts[n++]=request.substr(0,t);
            if(t==request.npos){request={};break;}request.remove_prefix(t+1);}
        if(!request.empty()||n<4)return reject("field-count");
        auto number=[](std::string_view s)->std::optional<uint64_t>{uint64_t v{};
            auto p=std::from_chars(s.data(),s.data()+s.size(),v);
            if(s.empty()||p.ec!=std::errc()||p.ptr!=s.data()+s.size())return {};return v;};
        auto a=number(parts[0]),b=number(parts[1]),c=number(parts[2]);
        if(!a||!b||!c)return reject("owner-format");
        if(*a!=revision||*b!=hud||*c!=source)return reject("owner-generation");
        if(parts[3]=="clear"&&n==4){clear();result_="cleared";return true;}
        if(parts[3]=="visible") {
            const auto id=n==5?number(parts[4]):std::nullopt;
            const auto *current=layout(tick,revision,hud,source);
            if(!id||!current||current->id!=*id||nativeDrawnId_!=*id)return reject("visible-without-native-draw");
            presentedId_=*id;presentedTick_=tick;result_="presented";return true;
        }
        if(parts[3]=="in"||parts[3]=="out") {
            if(n!=4||!layout(tick,revision,hud,source))return reject("zoom-without-lease");
            zoom_=parts[3]=="in"?-1:1;result_="zoom";return true;
        }
        if(parts[3]!="prepare"||n!=12)return reject("prepare-format");
        auto id=number(parts[4]);if(!id||!*id)return reject("layout-id");
        std::array<int,7> v{};for(size_t i=0;i<7;++i){auto x=number(parts[i+5]);if(!x||*x>16384)return reject("layout-number");v[i]=int(*x);}
        if(v[6]>1||v[5]<100||v[5]>8000||v[0]<320||v[1]<180||v[4]<24||v[2]-v[4]<0||v[3]-v[4]<0||v[2]+v[4]>v[0]||v[3]+v[4]>v[1])return reject("layout-bounds");
        const SeaNavigatorLayout next{*id,v[0],v[1],v[2],v[3],v[4],v[5],v[6]};
        const bool sameOwner=revision_==revision&&hud_==hud&&source_==source;
        if(sameOwner&&layout_&&layout_->id==*id&&*layout_!=next)return reject("layout-id-reused");
        if(!layout(tick,revision,hud,source)||layout_->id!=*id)nativeDrawnId_=presentedId_=0;
        revision_=revision;hud_=hud;source_=source;tick_=tick;
        layout_=next;result_="prepared";return true;
    }
    const SeaNavigatorLayout *layout(uint32_t tick,uint64_t revision,uint64_t hud,uint64_t source)const {
        return layout_&&tick-tick_<=750&&revision==revision_&&hud==hud_&&source==source_?&*layout_:nullptr;
    }
    void didDraw(uint64_t id) { nativeDrawnId_=layout_&&layout_->id==id?id:0; }
    bool presented(uint32_t tick,uint64_t revision,uint64_t hud,uint64_t source,uint64_t id) const {
        const auto *current=layout(tick,revision,hud,source);
        return current&&current->id==id&&nativeDrawnId_==id&&presentedId_==id&&tick-presentedTick_<=750;
    }
    int takeZoom(uint32_t tick,uint64_t revision,uint64_t hud,uint64_t source) {
        int z=layout(tick,revision,hud,source)?zoom_:0;zoom_=0;return z;
    }
};
inline SeaNavigatorBridge seaNavigator;
namespace sea_nav {
constexpr float pi=3.14159265358979323846f;
struct Point {float x,y;};
struct Aperture {float x,y,radius,scale;};
inline std::optional<Aperture> aperture(const SeaNavigatorLayout &layout,int x,int y,int width,int height){
    if(layout.width<=0||layout.height<=0||width<=0||height<=0)return {};
    float sx=float(width)/layout.width,sy=float(height)/layout.height;
    if(std::abs(sx-sy)>std::fmax(sx,sy)*.01f)return {};
    float scale=std::fmin(sx,sy);
    return Aperture{x+layout.x*sx,y+layout.y*sy,layout.radius*scale,scale};
}
inline Point polar(float angle,float radius) {return {std::sin(angle)*radius,-std::cos(angle)*radius};}
// Presentation dimensions are physical dp, independent of the map's world
// radius and of the 120-unit bezel artwork. In particular, a smaller aperture
// must not turn the letters and functional paths into subpixel strokes.
struct PrimitiveMetrics {
    float dp, radius;
    float cardinalWidth() const { return 5*dp; } // centerline; ink is 5..7dp
    float cardinalHeight() const { return 8*dp; } // actual ink is at least 8dp
    float functionalStroke() const { return dp; }
    float decorativeStroke() const { return .75f*dp; }
    float arrowLength() const { return 6*dp; }
    float arrowWidth() const { return 4.5f*dp; }
    float hullLength() const { return 10*dp; }
    float hullWidth() const { return 5.5f*dp; }
    float contactLength() const { return 4*dp; }
    float contactWidth() const { return 3*dp; }
    float chevronLength() const { return 5*dp; }
    float chevronWidth() const { return 5*dp; }
    float cardinalRadius() const { return std::fmax(0.f,radius-11.f*dp); }
    // Full bearing ink stays inside 30dp at the 96dp aperture, leaving a
    // measured 1dp gap to the upright cardinal band even at equal azimuths.
    float bearingTipRadius() const { return std::fmax(0.f,radius-18.5f*dp); }
};
inline PrimitiveMetrics primitiveMetrics(const SeaNavigatorLayout &layout,const Aperture &aperture) {
    return {float(layout.densityMilli)*.001f*aperture.scale,aperture.radius};
}
struct Stroke { Point a,b; float width; };
// The renderer and the host checks use these same filled stroke vertices, so
// the readability assertions measure ink rather than nominal glyph size.
inline std::optional<std::array<Point,4>> strokeQuad(const Stroke &s) {
    float dx=s.b.x-s.a.x,dy=s.b.y-s.a.y,len=std::hypot(dx,dy);
    if(len<.001f)return {};
    dx*=s.width*.5f/len;dy*=s.width*.5f/len;
    return std::array<Point,4>{{{s.a.x-dy,s.a.y+dx},{s.a.x+dy,s.a.y-dx},
                               {s.b.x+dy,s.b.y-dx},{s.b.x-dy,s.b.y+dx}}};
}
inline std::array<Point,4> arcQuad(Point center,float begin,float end,float radius,float thickness) {
    auto point=[&](float angle,float r){const auto p=polar(angle,r);return Point{center.x+p.x,center.y+p.y};};
    return {{point(begin,radius-thickness*.5f),point(begin,radius+thickness*.5f),
             point(end,radius+thickness*.5f),point(end,radius-thickness*.5f)}};
}
inline Point localPoint(Point origin,float angle,float x,float y) {
    return {origin.x+x*std::cos(angle)-y*std::sin(angle),
            origin.y+y*std::cos(angle)+x*std::sin(angle)};
}
inline std::array<Point,4> hull(Point origin,float angle,float length,float width) {
    return {{localPoint(origin,angle,0,-length*.5f),localPoint(origin,angle,width*.5f,length*.5f),
             localPoint(origin,angle,0,length*.35f),localPoint(origin,angle,-width*.5f,length*.5f)}};
}
inline std::array<Point,3> arrowHead(Point origin,float angle,float tipRadius,float length,float width) {
    return {{localPoint(origin,angle,0,-tipRadius),localPoint(origin,angle,-width*.5f,-tipRadius+length),
             localPoint(origin,angle,width*.5f,-tipRadius+length)}};
}
// The sign of length selects an outward course chevron or the wind's inward
// tail feather without changing its native angle or radius.
inline std::array<Point,3> chevron(Point origin,float angle,float tipRadius,float length,float width) {
    const auto head=arrowHead(origin,angle,tipRadius,length,width);
    return {{head[1],head[0],head[2]}};
}
struct CardinalGlyph { std::array<Stroke,16> strokes{}; size_t size{}; };
inline CardinalGlyph cardinalGlyph(Point origin,int cardinal,bool russian,const PrimitiveMetrics &metrics) {
    CardinalGlyph glyph;
    auto line=[&](float x1,float y1,float x2,float y2) {
        glyph.strokes[glyph.size++]={Point{x1,y1},Point{x2,y2},metrics.functionalStroke()};
    };
    auto path=[&](std::initializer_list<Point> points) {
        bool first=true;Point last{};
        for(auto p:points){if(!first)line(last.x,last.y,p.x,p.y);first=false;last=p;}
    };
    if(russian){
        if(cardinal==0)path({{2,-3},{0,-4},{-2,-3},{-3,-1},{-3,2},{-1,4},{2,3}}); // С
        if(cardinal==1){path({{-2,4},{-2,-4},{1,-4},{3,-2},{1,0},{-2,0}});path({{1,0},{3,2},{1,4},{-2,4}});} // В
        if(cardinal==2){line(-4,-4,-4,4);line(-4,0,-1,0);path({{1,-4},{-1,-2},{-1,2},{1,4},{3,2},{3,-2},{1,-4}});} // Ю
        if(cardinal==3)path({{-2,-3},{0,-4},{3,-2},{0,0},{3,2},{1,4},{-2,3}}); // З
    }else{
        if(cardinal==0)path({{-3,4},{-3,-4},{3,4},{3,-4}});
        if(cardinal==1){path({{3,-4},{-3,-4},{-3,4},{3,4}});line(-3,0,2,0);}
        if(cardinal==2)path({{3,-3},{1,-4},{-2,-3},{-3,-1},{2,1},{3,3},{1,4},{-3,3}});
        if(cardinal==3)path({{-4,-4},{-2,4},{0,-1},{2,4},{4,-4}});
    }
    if(!glyph.size)return glyph;
    float left=glyph.strokes[0].a.x,right=left,top=glyph.strokes[0].a.y,bottom=top;
    for(size_t i=0;i<glyph.size;++i)for(auto p:{glyph.strokes[i].a,glyph.strokes[i].b}){
        left=std::fmin(left,p.x);right=std::fmax(right,p.x);top=std::fmin(top,p.y);bottom=std::fmax(bottom,p.y);
    }
    for(size_t i=0;i<glyph.size;++i)for(auto *p:{&glyph.strokes[i].a,&glyph.strokes[i].b}){
        p->x=origin.x+(p->x-(left+right)*.5f)*metrics.cardinalWidth()/(right-left);
        p->y=origin.y+(p->y-(top+bottom)*.5f)*metrics.cardinalHeight()/(bottom-top);
    }
    return glyph;
}
// These are the original navigator's world -> ship-relative screen equations.
inline std::optional<Point> project(float dx,float dz,float heading,float worldRadius,float scale,float radius) {
    if(!std::isfinite(dx)||!std::isfinite(dz)||!std::isfinite(heading)||!std::isfinite(worldRadius)||!std::isfinite(scale)||worldRadius<=0||scale<=0)return {};
    const float x=dx*radius/(worldRadius*scale),y=dz*radius/(worldRadius*scale);
    if(x*x+y*y>radius*radius)return {};
    return Point{x*std::cos(heading)-y*std::sin(heading),-(y*std::cos(heading)+x*std::sin(heading))};
}
struct Bort { bool present{};float charge{},damage{},direction{},zone{},range{}; };
inline bool valid(const Bort &b) {return b.present&&std::isfinite(b.charge)&&std::isfinite(b.damage)&&b.charge>=0&&b.charge<=1&&b.damage>=0&&b.damage<=1&&std::isfinite(b.direction)&&std::isfinite(b.zone)&&b.zone>=0&&b.zone<=2*pi&&std::isfinite(b.range)&&b.range>=0;}
inline float readyFraction(const Bort &b) {return b.charge*(1-b.damage);}
inline float zoom(float scale,float step,float low,float high,bool in) {return std::fmax(low,std::fmin(high,scale+(in?-step:step)));}
}
} // namespace storm::android
