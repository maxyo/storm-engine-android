// Included inside xinterface.cpp's Android implementation: the facts of the treasurer's purchase
// list (PROGRAM/interface/GoodsTransfer.c) for mobile_goods_transfer.hpp. All rules stay in script.
namespace {
ATTRIBUTES *GoodsScriptObject(const char *name, std::optional<uint32_t> index = std::nullopt){
    auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));
    return !v?nullptr:index?v->GetAClass(*index):v->GetAClass();
}
std::optional<int> GoodsInteger(const char *name){
    auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));int32_t n;
    return v&&v->Get(n)?std::optional<int>(n):std::nullopt;
}
const char *GoodsAttr(ATTRIBUTES *root,const std::string &path){
    auto *a=root?root->FindAClass(root,path.c_str()):nullptr;return a?a->GetThisAttr():nullptr;
}
// Characters index of companion slot `slot` (GetCompanionIndex): 0 is the hero.
std::optional<int> GoodsCompanion(ATTRIBUTES *hero,int slot){
    const char *v=slot==0?GoodsAttr(hero,"index"):GoodsAttr(hero,"Fellows.Companions.id"+std::to_string(slot));
    if(!v||!*v)return std::nullopt;const int index=std::atoi(v);return index>=0?std::optional<int>(index):std::nullopt;
}
}

void XINTERFACE::MobileCollectGoodsTransfer(std::vector<MobileWidget> &widgets)
{
    storm::mobile::GoodsTransferSource source;
    auto *list=FindNode("MAIN_WINDOW",nullptr);
    auto *amount=FindNode("GOODS_TRANSFER_WINDOW",nullptr);
    const bool amountShown=amount&&amount->m_nNodeType==NODETYPE_WINDOW&&static_cast<CXI_WINDOW*>(amount)->GetShow();
    source.listActive=list&&list->m_nNodeType==NODETYPE_WINDOW&&static_cast<CXI_WINDOW*>(list)->GetActive()&&!amountShown;
    auto *hero=GoodsScriptObject("pchar");
    const auto slot=GoodsInteger("iCurCompanion");
    std::optional<int> ship=slot&&hero?GoodsCompanion(hero,*slot):std::nullopt;
    ATTRIBUTES *shipCharacter=ship?GoodsScriptObject("Characters",static_cast<uint32_t>(*ship)):nullptr;
    const char *shipId=GoodsAttr(shipCharacter,"id");
    if(!hero)source.missing.push_back("pchar");
    if(!slot)source.missing.push_back("iCurCompanion");
    if(!shipId)source.missing.push_back("companion");
    source.shipIndex=slot;
    if(hero){int count=0;for(int i=0;i<8;++i)if(GoodsCompanion(hero,i))++count;source.shipCount=count;}
    if(auto *strings=core.Entity_GetAttributeClass(g_idInterface,"strings"))
        if(const char *name=strings->GetAttribute("CurCompanion"))source.ship=name;
    // The list: the collection's name strings (id = good) that have their "Transfer<good>" partner.
    std::map<std::string,std::string> names;std::vector<std::string> order;std::set<std::string> partners;
    for(const auto &w:widgets)if(w.node=="GOODS_NAMES"&&!w.rowId.empty()){
        if(w.rowId.rfind("Transfer",0)==0)partners.insert(w.rowId.substr(8));
        else if(names.emplace(w.rowId,w.label).second)order.push_back(w.rowId);
    }
    for(const auto &id:order){
        if(!partners.count(id))continue;
        storm::mobile::GoodsTransferGood good{id,names[id]};
        if(shipId)if(const char *n=GoodsAttr(hero,"TransferGoods."+std::string(shipId)+"."+id))good.order=std::atoi(n);
        if(const char *n=GoodsAttr(shipCharacter,"Ship.Cargo.Goods."+id))good.hold=std::atoi(n);
        source.goods.push_back(std::move(good));
    }
    if(source.goods.empty())source.missing.push_back("goods");
    if(amountShown){
        const auto current=GoodsInteger("iCurGoodIndex");
        const char *id=current&&*current>=0?GoodsAttr(GoodsScriptObject("Goods",static_cast<uint32_t>(*current)),"name"):nullptr;
        if(id)source.open=id;else source.missing.push_back("iCurGoodIndex");
    }
    storm::mobile::CollectGoodsTransfer(source,widgets);
}

bool XINTERFACE::MobileApplyGoodsTransfer(const std::string &value)
{
    // Only from the list window, for a good the list shows, at this opening of the screen.
    const auto order=storm::mobile::GoodsTransferOrder(value,storm::android::mobileScreenRevision);
    std::vector<MobileWidget> live;MobileCollectWidgets(live);
    const auto row=std::find_if(live.begin(),live.end(),[&](const auto &w){
        return order&&w.node=="ANDROID_GOODS_ORDER"&&w.rowId==order->good&&w.enabled;});
    if(row==live.end())return false;
    const std::string &id=order->good;
    auto *icons=core.Entity_GetAttributeClass(g_idInterface,"GOODS_ICONS");
    auto *window=GoodsScriptObject("showWindow");
    const char *gx=GoodsAttr(icons,"imagelist."+id+".x"),*gy=GoodsAttr(icons,"imagelist."+id+".y");
    const char *ox=GoodsAttr(icons,"offset.x"),*oy=GoodsAttr(icons,"offset.y");
    const char *sx=GoodsAttr(icons,"scale.x"),*sy=GoodsAttr(icons,"scale.y");
    const char *ww=GoodsAttr(window,"width"),*wh=GoodsAttr(window,"height");
    if(!gx||!gy||!ox||!oy||!sx||!sy||!ww||!wh||std::atof(sx)<=0||std::atof(sy)<=0){
        core.Trace("goodstransfer: cannot order %s: GOODS_ICONS or showWindow geometry missing",id.c_str());
        return false;
    }
    const storm::mobile::GoodsIconPicture picture{static_cast<float>(std::atof(ox)),static_cast<float>(std::atof(oy)),
        static_cast<float>(std::atof(sx)),static_cast<float>(std::atof(sy)),
        storm::mobile::GoodsWindowOffsetX(static_cast<float>(std::atof(ww)),static_cast<float>(std::atof(wh)))};
    const auto point=storm::mobile::GoodsIconClick(static_cast<float>(std::atof(gx)),static_cast<float>(std::atof(gy)),picture);
    ATTRIBUTES *mouse=AttributesPointer->GetAttributeClass("mousepos");
    if(!mouse)mouse=AttributesPointer->CreateSubAClass(AttributesPointer,"mousepos");
    if(!mouse)return false;
    mouse->SetAttributeUseFloat("x",point.x);mouse->SetAttributeUseFloat("y",point.y);
    core.Event("ShowItemsWindow");
    // The script must now show this good's amount window; anything else is not ours to confirm.
    const auto current=GoodsInteger("iCurGoodIndex");
    const char *opened=current&&*current>=0?GoodsAttr(GoodsScriptObject("Goods",static_cast<uint32_t>(*current)),"name"):nullptr;
    auto *edit=FindNode("TG_EDIT",nullptr);auto *ok=FindNode("TG_OK_BUTTON",nullptr);
    if(!opened||id!=opened||!edit||!edit->m_bUse||!ok||!ok->m_bUse){
        core.Trace("goodstransfer: the amount window of %s did not open",id.c_str());
        return false;
    }
    if(!edit->MobileApply(-1,std::to_string(order->amount)))return false;
    const int code=ok->CheckCommandUsed(ACTION_MOUSECLICK)?ACTION_MOUSECLICK:ACTION_ACTIVATE;
    bool stop=false;if(auto *next=ok->DoAction(code,stop,true))SetCurNode(next);
    return true;
}
