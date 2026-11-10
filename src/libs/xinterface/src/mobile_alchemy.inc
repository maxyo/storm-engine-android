// Included inside xinterface.cpp's Android implementation: the facts of the alchemy screen
// (PROGRAM/interface/alchemy.c) for mobile_alchemy.hpp, and its two guarded actions. Every item
// move and the crafting itself stay the script's own (TableSelectChange, TransactionOK,
// «Результат»); the engine only fills in what a player would.
namespace {
ATTRIBUTES *AlchemyObject(const char *name, std::optional<uint32_t> index = std::nullopt){
    auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));
    return !v?nullptr:index?v->GetAClass(*index):v->GetAClass();
}
std::optional<int> AlchemyInteger(const char *name){
    auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));int32_t n;
    return v&&v->Get(n)?std::optional<int>(n):std::nullopt;
}
std::string AlchemyString(const char *name){
    auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));const char *s=nullptr;
    return v&&v->Get(s)&&s?s:"";
}
const char *AlchemyAttr(ATTRIBUTES *root,const std::string &path){
    auto *a=root?root->FindAClass(root,path.c_str()):nullptr;return a?a->GetThisAttr():nullptr;
}
// Items index of every item id the screen names (one pass over the script's Items array).
std::map<std::string,uint32_t> AlchemyItemIndex(const std::set<std::string> &ids){
    std::map<std::string,uint32_t> out;
    auto *items=static_cast<VDATA*>(core.GetScriptVariable("Items"));
    if(!items)return out;
    for(uint32_t i=0;i<items->GetElementsNum()&&out.size()<ids.size();++i){
        const char *id=AlchemyAttr(items->GetAClass(i),"id");
        if(id&&ids.count(id))out.emplace(id,i);
    }
    return out;
}
}

storm::mobile::AlchemySource XINTERFACE::MobileAlchemySource(const std::vector<MobileWidget> &widgets)
{
    storm::mobile::AlchemySource source;
    auto *root=AttributesPointer;
    auto *list=root?root->GetAttributeClass("ITEMS_SCROLL"):nullptr;
    auto *scroll=FindNode("ITEMS_SCROLL",nullptr);
    auto *items=static_cast<VDATA*>(core.GetScriptVariable("Items"));
    source.current=AlchemyString("sCurItem");
    if(!list)source.missing.push_back("ITEMS_SCROLL");
    if(!scroll||scroll->m_nNodeType!=NODETYPE_SCROLLIMAGE)source.missing.push_back("ITEMS_SCROLL node");
    if(!items)source.missing.push_back("Items");
    if(source.current.empty())source.missing.push_back("sCurItem");
    if(!source.missing.empty())return source;

    // The recipes: only picN records with an item (ListSize and NotUsed count padding).
    std::vector<std::pair<int,std::string>> pictures;std::set<std::string> ids{source.current};
    for(uint32_t i=0;i<list->GetAttributesNum();++i){
        auto *entry=list->GetAttributeClass(i);const std::string name=entry?entry->GetThisName():"";
        if(name.rfind("pic",0)!=0||name.size()<4||name.find_first_not_of("0123456789",3)!=std::string::npos)continue;
        const char *id=AlchemyAttr(entry,"itemId");
        if(!id||!*id)continue;
        pictures.emplace_back(std::atoi(name.c_str()+3)-1,id);ids.insert(id);
    }
    std::sort(pictures.begin(),pictures.end());
    auto index=AlchemyItemIndex(ids);
    if(m_mobileAlchemyStrings<0||m_mobileAlchemyLanguage!=(pStringService->GetLanguage()?pStringService->GetLanguage():"")){
        if(m_mobileAlchemyStrings>=0)pStringService->CloseUsersStringFile(m_mobileAlchemyStrings);
        m_mobileAlchemyStrings=pStringService->OpenUsersStringFile("ItemsDescribe.txt");
        m_mobileAlchemyLanguage=pStringService->GetLanguage()?pStringService->GetLanguage():"";
    }
    const auto text=[&](const char *key)->std::string{
        if(!key||!*key||m_mobileAlchemyStrings<0)return {};
        const char *s=pStringService->TranslateFromUsers(m_mobileAlchemyStrings,key);return s?s:"";
    };
    for(const auto &[position,id]:pictures){
        const auto found=index.find(id);
        if(found==index.end()){source.missing.push_back("item:"+id);continue;}
        auto *item=items->GetAClass(found->second);
        storm::mobile::AlchemyRecipe recipe;recipe.id=id;recipe.index=position;
        recipe.name=text(AlchemyAttr(item,"name"));
        if(recipe.name.empty())source.missing.push_back("name:"+id);
        const char *group=AlchemyAttr(item,"groupID");recipe.group=group?group:"";
        const char *tex=AlchemyAttr(item,"picTexture"),*pic=AlchemyAttr(item,"picIndex");
        recipe.atlasGroup=tex?tex:"";recipe.atlasImage=pic?std::string("itm")+pic:"";
        const char *yield=AlchemyAttr(item,"multiobject.qty");recipe.yield=yield?std::max(1,std::atoi(yield)):1;
        source.recipes.push_back(std::move(recipe));
    }
    if(source.recipes.empty())source.missing.push_back("recipes");

    // The shown recipe's components, in the order the table lists them (AddToTable).
    const auto shown=index.find(source.current);
    auto *recipeItem=shown!=index.end()?items->GetAClass(shown->second):nullptr;
    auto *components=recipeItem?recipeItem->GetAttributeClass("component"):nullptr;
    if(!components)source.missing.push_back("component:"+source.current);
    else{
        source.description=text(AlchemyAttr(recipeItem,"describe"));
        for(uint32_t i=0;i<components->GetAttributesNum();++i){
            auto *part=components->GetAttributeClass(i);
            storm::mobile::AlchemyComponent c;
            const char *id=AlchemyAttr(part,"id"),*use=AlchemyAttr(part,"use"),*qty=AlchemyAttr(part,"qty");
            c.itemId=id?id:"";c.need=qty?std::atoi(qty):0;
            c.catalyst=use&&!storm::iEquals(use,"component");
            if(c.itemId.empty()||c.need<=0)source.missing.push_back("component:"+source.current+"#"+std::to_string(i+1));
            source.components.push_back(std::move(c));
        }
        source.busy=storm::mobile::AlchemyFillCounts(widgets,source.components);
    }

    // Ready only when the script shows what the scroll shows and nothing is in flight.
    auto *images=static_cast<CXI_SCROLLIMAGE*>(scroll);
    const auto seen=AlchemyInteger("nCurScrollNum"),window=AlchemyInteger("bShowChangeWin");
    const char *atScroll=AlchemyAttr(list,"pic"+std::to_string(images->MobileCurrent()+1)+".itemId");
    auto *tooltip=FindNode("tooltip_fader",nullptr);
    if(!m_bUse||!bActive)source.busy="inactive";
    else if(m_pEvents)source.busy="events";
    else if(images->MobileMoving())source.busy="scrolling";
    else if(!seen||*seen!=images->MobileCurrent()||!atScroll||source.current!=atScroll)source.busy="recipe-pending";
    else if(!window||*window!=0)source.busy="amount-window";
    else if(tooltip&&tooltip->m_bUse)source.busy="tooltip";
    return source;
}

void XINTERFACE::MobileCollectAlchemy(std::vector<MobileWidget> &widgets)
{
    storm::mobile::CollectAlchemy(MobileAlchemySource(widgets),widgets);
}

bool XINTERFACE::MobileApplyAlchemy(const std::string &node,const std::string &value)
{
    const uint64_t revision=storm::android::mobileScreenRevision;
    std::vector<MobileWidget> live;MobileCollectWidgets(live);
    const auto source=MobileAlchemySource(live);
    if(!source.busy.empty()||!source.missing.empty()){core.Trace("alchemy: %s refused: %s",value.c_str(),source.busy.c_str());return false;}
    if(node=="ANDROID_ALCHEMY_RECIPE"){
        const auto choose=storm::mobile::ParseAlchemyAction(value,"choose",revision,false);
        const auto recipe=choose?std::find_if(source.recipes.begin(),source.recipes.end(),[&](const auto &r){return r.id==choose->item;}):source.recipes.end();
        if(recipe==source.recipes.end()||recipe->id==source.current)return false;
        static_cast<CXI_SCROLLIMAGE*>(FindNode("ITEMS_SCROLL",nullptr))->MobileSelect(recipe->index);
        return true;
    }
    const auto craft=storm::mobile::ParseAlchemyAction(value,"craft",revision,true);
    if(!craft||craft->item!=source.current)return false;
    const auto changes=storm::mobile::AlchemyPotChanges(source.components,craft->batches);
    if(!changes){core.Trace("alchemy: %d batches of %s are not possible",craft->batches,craft->item.c_str());return false;}
    // The pot as the script keeps it (NullCharacter.alchemy.Items, read by GetCharacterFreeItem);
    // the table shows it a frame later.
    auto *pot=AlchemyObject("NullCharacter");
    const auto inPot=[&](const std::string &id){const char *n=AlchemyAttr(pot,"alchemy.Items."+id);return n?std::atoi(n):0;};
    for(size_t i=0;i<changes->size();++i){
        const auto &part=source.components[i];
        const int want=part.catalyst?part.need:part.need*craft->batches;
        const int change=want-inPot(part.itemId);
        if(change==0)continue;
        // The player's way: select the row (ShowGoodsInfo), type the amount, confirm. A negative
        // amount gives from the hero to the pot, a positive one takes back.
        core.Event("TableSelectChange","sll","TABLE_LIST",static_cast<int32_t>(i+1),1);
        ATTRIBUTES *edit=AttributesPointer->GetAttributeClass("qty_edit");
        if(!edit)edit=AttributesPointer->CreateSubAClass(AttributesPointer,"qty_edit");
        if(!edit)return false;
        edit->SetAttribute("str",std::to_string(-change));
        core.Event("TransactionOK");
        if(inPot(part.itemId)!=want){
            core.Trace("alchemy: the pot of %s holds %d, not %d",part.itemId.c_str(),inPot(part.itemId),want);
            return false;
        }
    }
    auto *result=FindNode("GETALL_BUTTON",nullptr);
    if(!result||!result->m_bUse||!result->m_bSelected||result->m_bLockedNode||result->m_bLockStatus){
        core.Trace("alchemy: «Результат» is not available after filling the pot");return false;
    }
    const int code=result->CheckCommandUsed(ACTION_MOUSECLICK)?ACTION_MOUSECLICK:ACTION_ACTIVATE;
    bool stop=false;if(auto *next=result->DoAction(code,stop,true))SetCurNode(next);
    return true;
}
