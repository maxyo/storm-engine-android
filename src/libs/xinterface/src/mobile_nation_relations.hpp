#pragma once
#include "mobile_widget.hpp"
#include <algorithm>
#include <set>

namespace storm::mobile {
// Source-only dynamic CreateImage records. Positions associate an actual relation
// with its actual labelled axes; they never identify a country or relation value.
struct NationImage { std::string node, caption; int x=0,y=0; ImageRef image; };
inline int NationPictureNumber(const std::string &name) {
    if(name.size()<4 || (name[0]!='p'&&name[0]!='P')||(name[1]!='i'&&name[1]!='I')||(name[2]!='c'&&name[2]!='C'))return -1;
    int n=0;for(size_t i=3;i<name.size();++i){if(name[i]<'0'||name[i]>'9'||n>1000)return -1;n=n*10+name[i]-'0';}return n;
}
inline bool CollectNationMatrix(const std::vector<NationImage> &pictures,std::vector<MobileWidget> &out,std::string *reason=nullptr) {
    const auto reject=[&](const std::string &message){if(reason)*reason="matrix:"+message;return false;};
    std::vector<NationImage> axes,relations;
    for(const auto &p:pictures){
        if(NationPictureNumber(p.node)<1)continue;
        if(p.image.source.group=="NATIONS")axes.push_back(p);
        else if(p.image.source.group=="relations")relations.push_back(p);
        else return reject("unexpected-group="+p.image.source.group+" node="+p.node);
    }
    std::sort(axes.begin(),axes.end(),[](const auto&a,const auto&b){return NationPictureNumber(a.node)<NationPictureNumber(b.node);});
    if(axes.size()<4 || axes.size()%2 || axes.size()>32)return reject("axis-image-count="+std::to_string(axes.size()));
    const size_t count=axes.size()/2;
    // Beta adds main-character portrait axes and relations. Do not discard those.
    if(relations.size()!=count*(count-1))return reject("pair-count="+std::to_string(relations.size())+" expected="+std::to_string(count*(count-1))+" (additional beta content must be retained)");
    std::set<std::string> columns,rows;
    for(size_t i=0;i<count;++i){
        if(NationPictureNumber(axes[i].node)!=int(i+1)||NationPictureNumber(axes[count+i].node)!=int(count+i+1)||
           axes[i].caption.empty()||axes[count+i].caption.empty()||
           axes[i].y!=axes[0].y||axes[count+i].x!=axes[count].x||
           !columns.insert(axes[i].image.source.image).second||!rows.insert(axes[count+i].image.source.image).second)return reject("axis-schema-or-caption index="+std::to_string(i)+" column="+axes[i].node+" row="+axes[count+i].node);
    }
    if(columns!=rows)return reject("row-column-identities-differ");
    std::vector<MobileWidget> result;
    MobileWidget header{"ANDROID_NATION_COLUMNS","nation_columns","","1",-1,0,false};
    header.headerCells.emplace();
    for(size_t i=0;i<count;++i){MobileCell c;c.column=int(i);c.text=axes[i].caption;c.images.push_back(axes[i].image);header.headerCells->push_back(std::move(c));}
    result.push_back(std::move(header));
    std::set<std::string> used;
    for(size_t row=0;row<count;++row){
        const auto &axis=axes[count+row];
        MobileWidget w{"ANDROID_NATION_ROW","nation_row",axis.caption,"",int(row),0,false};w.rowId=axis.image.source.image;w.images.push_back(axis.image);
        for(size_t col=0;col<count;++col){
            MobileCell c;c.column=int(col);
            if(axis.image.source.image==axes[col].image.source.image)c.text="self";
            else {
                const NationImage *found=nullptr;
                for(const auto &p:relations)if(p.x==axes[col].x&&p.y==axis.y){if(found)return reject("duplicate-pair from="+axis.image.source.image+" to="+axes[col].image.source.image);found=&p;}
                if(!found||!used.insert(found->node).second)return reject("missing-or-reused-pair from="+axis.image.source.image+" to="+axes[col].image.source.image);
                c.text=found->caption.empty()?found->image.source.image:found->caption;c.images.push_back(found->image);
            }
            w.cells.push_back(std::move(c));
        }
        result.push_back(std::move(w));
    }
    if(used.size()!=relations.size())return reject("unconsumed-pairs");
    out.insert(out.end(),result.begin(),result.end());return true;
}
} // namespace storm::mobile
