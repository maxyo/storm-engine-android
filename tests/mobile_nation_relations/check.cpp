#include "mobile_nation_relations.hpp"
#include <cassert>
#include <iostream>
using namespace storm::mobile;
NationImage picture(int n,const std::string &group,const std::string &name,int x,int y){NationImage p;p.node="pic"+std::to_string(n);p.caption="caption:"+name;p.x=x;p.y=y;p.image.source={"atlas",group,name,{},{},-1};return p;}
std::vector<NationImage> fixture(){std::vector<NationImage> images;const std::string ids[]={"France","Pirate","England","Holland","Spain"};int n=1;
for(int i=0;i<5;i++)images.push_back(picture(n++,"NATIONS",ids[i],156+76*i,120));
for(int i=0;i<5;i++)images.push_back(picture(n++,"NATIONS",ids[i],75,200+70*i));
for(int r=0;r<5;r++)for(int c=0;c<5;c++)if(r!=c)images.push_back(picture(n++,"relations",r==0?"Enemy":r==1?"Friend":r==2?"Unknown":"Neutral",156+76*c,200+70*r));return images;}
int main(){auto images=fixture();images[0].node="PIC1";std::vector<MobileWidget> result;assert(CollectNationMatrix(images,result));assert(result.size()==6);assert(result[0].headerCells->at(0).images[0].source.image=="France");assert(result[1].rowId=="France");assert(result[1].cells[1].images[0].source.image=="Enemy");assert(result[3].cells[0].images[0].source.image=="Unknown");assert(result[1].cells[0].images.empty());
// Unordered linked list, unknown truthful value, source order, sparse/malformed/beta.
std::reverse(images.begin(),images.end());std::vector<MobileWidget> reversed;assert(CollectNationMatrix(images,reversed));assert(MobileWidgetJson("nationrelation",result,7)==MobileWidgetJson("nationrelation",reversed,7));
auto checkBad=[](std::vector<NationImage> v){std::vector<MobileWidget> out;std::string reason;assert(!CollectNationMatrix(v,out,&reason));assert(out.empty()&&!reason.empty());};
auto bad=fixture();bad.pop_back();checkBad(bad);bad=fixture();bad.push_back(picture(31,"relations","Enemy",100,200));checkBad(bad);bad=fixture();bad[0].image.source.image=bad[1].image.source.image;checkBad(bad);bad=fixture();bad[0].caption="";checkBad(bad);bad=fixture();bad[10].x=999;checkBad(bad);bad=fixture();bad[10].y=bad[11].y;bad[10].x=bad[11].x;checkBad(bad);
std::cout<<MobileWidgetJson("nationrelation",result,7,12)<<"\n";
}
