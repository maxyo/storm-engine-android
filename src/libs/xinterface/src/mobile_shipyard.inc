// Included inside xinterface.cpp's Android implementation; all game rules stay in script.
namespace {
std::string YardAttribute(ATTRIBUTES *root,const std::string &path) {
    if(!root)return {};auto *a=root->FindAClass(root,path.c_str());
    const char *s=a?a->GetThisAttr():nullptr;return s?s:"";
}
ATTRIBUTES *YardReference(const char *name){auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));return v?v->GetAClass():nullptr;}
std::optional<int32_t> YardInteger(const char *name){auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));int32_t n;return v&&v->Get(n)?std::optional<int32_t>(n):std::nullopt;}
std::string YardString(const char *name){auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));const char *s=nullptr;return v&&v->Get(s)&&s?s:"";}
// PROGRAM/quests/quests.c GetFullName: preserve all three source name fields.
std::string YardCharacterName(ATTRIBUTES *person){std::string name=YardAttribute(person,"name");for(const char *field:{"middlename","lastname"}){auto part=YardAttribute(person,field);if(!part.empty())name+=" "+part;}return name;}
bool YardName(const std::string &a,const char *b){return storm::iEquals(a,b);}
}

void XINTERFACE::MobileCollectShipyard(std::vector<MobileWidget> &widgets)
{
    auto *root=AttributesPointer;
    auto *fleet=root?root->GetAttributeClass("SHIPS_SCROLL"):nullptr;
    auto *merchant=YardReference("refNPCShipyard"),*boundOwner=YardReference("xi_refCharacter");
    auto *characters=static_cast<VDATA*>(core.GetScriptVariable("Characters"));
    const auto current=storm::mobile::InventoryNumber(fleet?fleet->GetAttribute("current"):nullptr);
    const auto seen=YardInteger("nCurScrollNum"),yard=YardInteger("bShipyardOnTop"),empty=YardInteger("bEmptySlot");
    std::string failure,mode="main";
    const auto reject=[&](std::string why){if(!failure.empty())failure+=';';failure+=std::move(why);};
    MobileWidget state{"ANDROID_SHIPYARD_STATE","shipyard_state","","1",1,0,false};
    auto &ctx=state.context;
    ctx["profile"]=storm::mobile::OwnerProfile()?storm::mobile::OwnerProfile():"";
    ctx["merchant"]=YardAttribute(merchant,"id");
    ctx["messageMode"]=YardString("sMessageMode");ctx["pendingOffer"]=YardString("sShipId");
    ctx["curTable"]=YardString("CurTable");ctx["curRow"]=YardString("CurRow");
    if(!merchant||!current||!seen||!yard||!empty||ctx["profile"].empty()||ctx["merchant"].empty())reject("source-context-missing");
    bool mainVisible=false,mainActive=false;int childCount=0;
    for(const auto &[name,kind]:std::initializer_list<std::pair<const char*,const char*>>{
        {"MAIN_WINDOW","main"},{"MSG_WINDOW","confirm"},{"REPAIR_WINDOW","repair"},{"OFFICERS_WINDOW","captain"},{"RPG_WINDOW","rpg"},{"CANNONS_WINDOW","cannons"}}){
        auto *node=FindNode(name,nullptr);
        if(!node||node->m_nNodeType!=NODETYPE_WINDOW){reject(std::string("window-missing:")+name);continue;}
        auto *window=static_cast<CXI_WINDOW*>(node);
        ctx[std::string("window.")+name]=std::string(window->GetShow()?"shown":"hidden")+":"+(window->GetActive()?"active":"locked");
        if(std::string(kind)=="main"){mainVisible=window->GetShow();mainActive=window->GetActive();}
        else if(window->GetShow()){++childCount;mode=kind;if(!window->GetActive())reject(std::string("child-locked:")+name);}
    }
    if(!mainVisible||childCount>1||(childCount==0&&!mainActive))reject("ambiguous-window-state");
    if(auto *tooltip=FindNode("tooltip_fader",nullptr);tooltip&&tooltip->m_bUse)mode="help";
    ctx["mode"]=mode;
    ctx["previewKind"]=yard&&*yard?"yard":"owned";
    const auto translate=[&](const std::string &raw){if(raw.empty())return std::string{};if(raw[0]=='#')return raw.substr(1);const char *s=pStringService?pStringService->GetString(raw.c_str()):nullptr;return std::string(s?s:"");};
    const auto slotImage=[&](ATTRIBUTES *list,ATTRIBUTES *entry){
        storm::mobile::ImageRef ref;if(!list||!entry)return ref;
        const char *tex=entry->GetAttribute("tex1"),*pic=entry->GetAttribute("img1");
        auto *groups=list->GetAttributeClass("ImagesGroup");const auto index=storm::mobile::InventoryNumber(tex);
        const char *group=index&&groups&&*index<groups->GetAttributesNum()?groups->GetAttribute(*index):tex;
        if(group&&pic)ref=storm::mobile::AtlasReference(pPictureService,group,pic);return ref;
    };
    for(auto &w:widgets)if(YardName(w.node,"SHIPS_SCROLL")&&w.type=="choice"){
        auto *entry=fleet?fleet->GetAttributeClass("pic"+std::to_string(w.index+1)):nullptr;
        const auto owner=storm::mobile::InventoryNumber(entry?entry->GetAttribute("character"):nullptr);
        const std::string raw=YardAttribute(entry,"character");
        auto *person=owner&&characters&&*owner<characters->GetElementsNum()?characters->GetAClass(*owner):nullptr;
        auto &s=w.context;s["owner"]=raw;s["slot"]=std::to_string(w.index);
        s["ownerKind"]=raw=="-1"?"EMPTY_SLOT":YardAttribute(entry,"str2")=="NoneBoat"?"OWNER_NO_SHIP":"OWNER_SHIP";
        s["shipName"]=raw=="-1"?"":s["ownerKind"]=="OWNER_SHIP"?YardAttribute(person,"ship.name"):translate("NoneBoat");
        s["captainName"]=person?YardCharacterName(person):"";s["rank"]=YardAttribute(person,"rank");
        s["class"]=raw=="-1"?"":translate(YardAttribute(entry,"str1"));s["saleQuote"]=raw=="-1"?"":translate(YardAttribute(entry,"str3"));s["type"]=raw=="-1"?"":translate(YardAttribute(entry,"str4"));
        const auto image=slotImage(fleet,entry);if(image.handle)w.images.push_back(image);
        if(raw!="-1"&&!person)reject("owner-reference-missing");
        if(w.value=="1"){
            for(const auto &[key,value]:s)ctx["owner."+key]=value;
            if(!current||*current!=uint32_t(w.index)||!seen||*seen!=w.index)reject("owner-frame-pending");
            if(empty&&bool(*empty)!=(raw=="-1"))reject("owner-empty-pending");
            if(raw!="-1"&&YardAttribute(boundOwner,"index")!=raw)reject("owner-reference-pending");
        }
    }
    if(ctx.find("owner.ownerKind")==ctx.end())reject("selected-owner-missing");
    auto *offers=root?root->GetAttributeClass("TABLE_SHIPYARD"):nullptr;
    MobileWidget catalog{"ANDROID_SHIPYARD_CATALOG","shipyard_catalog","","",-1,0,false};
    for(int col=0;col<5;col++){MobileCell cell;cell.column=col;cell.text=YardAttribute(offers,"hr.td"+std::to_string(col+1)+".str");if(cell.text.empty())reject("catalog-header-missing");catalog.cells.push_back(std::move(cell));}
    if(offers&&offers->FindAClass(offers,"hr.td6"))reject("catalog-extra-column");
    widgets.push_back(std::move(catalog));
    const auto selected=storm::mobile::InventoryNumber(offers?offers->GetAttribute("select"):nullptr);
    auto *offer=selected&&*selected?offers->GetAttributeClass("tr"+std::to_string(*selected)):nullptr;
    ctx["offer"]=YardAttribute(offer,"sShipId");
    if(yard&&*yard){
        auto *actual=merchant?merchant->FindAClass(merchant,("shipyard."+ctx["offer"]).c_str()):nullptr;
        if(ctx["offer"].empty()||!actual||YardAttribute(actual,"type")!=YardAttribute(merchant,"ship.type")||YardAttribute(actual,"name")!=YardAttribute(merchant,"ship.name"))reject("offer-preview-pending");
    }
    if(mode=="confirm"&&ctx["messageMode"]!="ShipBuy"&&ctx["messageMode"]!="ShipSell")reject("confirm-mode-missing");
    if(mode=="captain"){
        auto *list=root?root->GetAttributeClass("PASSENGERSLIST"):nullptr;
        const auto selectedCaptain=storm::mobile::InventoryNumber(list?list->GetAttribute("current"):nullptr);
        const auto seenCaptain=YardInteger("nCurScrollOfficerNum");
        if(!selectedCaptain||!seenCaptain||*selectedCaptain!=uint32_t(*seenCaptain))reject("captain-frame-pending");
        auto *entry=selectedCaptain?list->GetAttributeClass("pic"+std::to_string(*selectedCaptain+1)):nullptr;
        ctx["captain"]=YardAttribute(entry,"character");
        if(!empty||!*empty||ctx["pendingOffer"].empty())reject("captain-purchase-context-missing");
        for(auto &w:widgets)if(YardName(w.node,"PASSENGERSLIST")&&w.type=="choice"){
            auto *candidate=list->GetAttributeClass("pic"+std::to_string(w.index+1));
            w.context["character"]=YardAttribute(candidate,"character");auto ref=slotImage(list,candidate);if(ref.handle)w.images.push_back(ref);
        }
    }
    const auto addControl=[&](const char *name,const char *label,const char *verb,int index=-1){
        auto *node=FindNode(name,nullptr);if(!node||!node->m_bUse)return;
        MobileWidget w{name,"shipyard_control",label,"",index,0,node->m_bClickable&&node->m_bSelected&&!node->m_bLockedNode&&!node->m_bLockStatus&&node->m_nDoDelay==0};
        int code=std::string(verb)=="help"||std::string(verb)=="step5"?ACTION_MOUSERCLICK:ACTION_MOUSECLICK;
        if(node->CheckCommandUsed(code))w.actions.push_back(verb);else w.enabled=false;widgets.push_back(std::move(w));
    };
    for(const auto &[name,label]:std::initializer_list<std::pair<const char*,const char*>>{{"REPAIR_LEFT_H","−"},{"REPAIR_RIGHT_H","+"},{"REPAIR_LEFT_S","−"},{"REPAIR_RIGHT_S","+"}}){addControl(name,label,"press");addControl(name,label,"step5",-5);}
    for(const char *name:{"SHIP_BIG_PICTURE","MAIN_CHARACTER_PICTURE","SHIPS_SCROLL","TABLE_SHIPYARD"})addControl(name,"","help",-2);
    addControl("EXIT_BTN","","press");
    if(mode=="help"||mode=="rpg"){
        MobileWidget close{"ANDROID_SHIPYARD_CLOSE","shipyard_control","","",-1,0,true};close.actions.push_back("close");widgets.push_back(std::move(close));
    }
    for(auto &w:widgets){
        auto *node=FindNode(w.node.c_str(),nullptr);
        if(w.type=="button"){
            w.enabled=w.enabled&&node&&node->m_bUse&&node->m_bSelected&&!node->m_bLockedNode&&!node->m_bLockStatus&&node->m_nDoDelay==0;
            w.actions.clear();if(w.enabled&&(node->CheckCommandUsed(ACTION_MOUSECLICK)||node->CheckCommandUsed(ACTION_ACTIVATE)))w.actions.push_back("press");
        }
        if(w.type=="choice"&&YardName(w.node,"TABLE_OTHER")&&w.enabled&&node){
            if(node->CheckCommandUsed(ACTION_MOUSERCLICK))w.actions.push_back("help");
            if(w.rowId!="CannonType")w.actions.erase(std::remove(w.actions.begin(),w.actions.end(),"activate"),w.actions.end());
        }
    }
    ctx["ready"]=failure.empty()?"1":"0";state.unavailableReason=failure;
    if(!failure.empty())state.value="0";
    widgets.push_back(std::move(state));
    const auto signature=MobileWidgetJson("shipyard",widgets,storm::android::mobileScreenRevision);
    if(signature!=m_mobileShipyardSignature){m_mobileShipyardSignature=signature;++m_mobileShipyardGeneration;}
    for(auto &w:widgets)if(w.enabled)for(auto &verb:w.actions)
        verb="yard@"+std::to_string(storm::android::mobileScreenRevision)+":"+std::to_string(m_mobileShipyardGeneration)+":"+verb;
}

bool XINTERFACE::MobileApplyShipyard(const std::string &name,int index,const std::string &value)
{
    std::vector<MobileWidget> live;MobileCollectWidgets(live);
    auto state=std::find_if(live.begin(),live.end(),[](const auto&w){return w.type=="shipyard_state";});
    if(state==live.end()||state->value!="1")return false;
    const auto grant=std::find_if(live.begin(),live.end(),[&](const auto&w){return w.node==name&&w.index==index&&w.enabled&&std::find(w.actions.begin(),w.actions.end(),value)!=w.actions.end();});
    if(grant==live.end())return false;
    const std::string verb=value.substr(value.find_last_of(':')+1);const auto rowId=grant->rowId;
    ++m_mobileShipyardGeneration;
    if(name=="ANDROID_SHIPYARD_CLOSE"&&verb=="close"){core.Event("MouseRClickUp");return true;}
    auto *node=FindNode(name.c_str(),nullptr);
    if(!node||!node->m_bUse||node->m_bLockedNode||node->m_bLockStatus||node->m_nDoDelay>0)return false;
    if(grant->type=="choice"){
        if(!node->MobileApply(index,"select@"+std::to_string(storm::android::mobileScreenRevision)+":"+rowId))return false;
        if(verb=="select")return true;
        node=FindNode(name.c_str(),nullptr);
        if(!node||!node->m_bUse||node->m_bLockedNode||node->m_bLockStatus)return false;
    }
    const int code=verb=="help"||verb=="step5"?ACTION_MOUSERCLICK:
        verb=="activate"?ACTION_ACTIVATE:node->CheckCommandUsed(ACTION_MOUSECLICK)?ACTION_MOUSECLICK:ACTION_ACTIVATE;
    if(!node->CheckCommandUsed(code)||!node->m_bSelected)return false;
    SetCurNode(node);bool stop=false;if(auto *next=node->DoAction(code,stop,true))SetCurNode(next);return true;
}
