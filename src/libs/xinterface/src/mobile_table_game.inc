// Included inside xinterface.cpp's Android implementation: the facts of the tavern mini-games
// (PROGRAM/interface/boal_dice.c, boal_cards.c) for mobile_table_game.hpp. All rules stay in script.
namespace {
std::optional<std::string> TableInteger(const char *name){
    auto *v=static_cast<VDATA*>(core.GetScriptVariable(name));int32_t n;
    return v&&v->Get(n)?std::optional<std::string>(std::to_string(n)):std::nullopt;
}
std::optional<std::string> TableAttribute(ATTRIBUTES *root,const char *path){
    // An attribute the script set to "" (DiceState at the start of a game) is a fact; only an
    // absent one is missing.
    auto *a=root?root->FindAClass(root,path):nullptr;
    if(!a)return std::nullopt;const char *s=a->GetThisAttr();return std::string(s?s:"");
}
// DiceState.<side>.d1..d5 (faces) and .Mix (marked for the cup) as "3,3,3,6,1" / "0,0,0,1,1".
void TableDice(ATTRIBUTES *state,const char *side,std::vector<std::pair<std::string,std::optional<std::string>>> &facts){
    std::optional<std::string> faces=std::string(),mix=std::string();
    for(int i=1;i<=5;++i){
        const std::string die=std::string(side)+".d"+std::to_string(i);
        const auto face=TableAttribute(state,die.c_str());const auto marked=TableAttribute(state,(die+".Mix").c_str());
        if(!face)faces.reset();else if(faces)*faces+=(i>1?",":"")+*face;
        if(!marked)mix.reset();else if(mix)*mix+=(i>1?",":"")+std::string(*marked=="0"||marked->empty()?"0":"1");
    }
    const std::string key=std::string(side)=="Hero"?"hero":"comp";
    facts.emplace_back(key+"Dice",faces);facts.emplace_back(key+"Mix",mix);
    facts.emplace_back(key+"Type",TableAttribute(state,(std::string(side)+".Result.Type").c_str()));
}
}

void XINTERFACE::MobileCollectTableGame(std::vector<MobileWidget> &widgets)
{
    storm::mobile::TableGameSource source;
    for(auto *p=m_imgLists;p;p=p->next){
        if(!p->sImageName)continue;
        storm::mobile::TableGameImage image{p->sImageName,static_cast<int>(p->position.left),static_cast<int>(p->position.top)};
        if(p->sImageListName&&*p->sImageListName&&p->sPicture&&*p->sPicture){
            image.image=storm::mobile::AtlasReference(pPictureService,p->sImageListName,p->sPicture);
            image.image.source={"atlas",p->sImageListName,p->sPicture,{},{},-1};
        }
        source.images.push_back(std::move(image));
    }
    if(auto *strings=core.Entity_GetAttributeClass(g_idInterface,"strings"))
        for(size_t i=0;i<strings->GetAttributesNum();++i){
            const char *name=strings->GetAttributeName(i);const char *text=strings->GetAttribute(i);
            if(name)source.strings.emplace_back(name,text?text:"");
        }
    auto &facts=source.facts;
    for(const auto &[key,name]:std::initializer_list<std::pair<const char*,const char*>>{
        {"rate","iRate"},{"moneyHero","iMoneyP"},{"moneyOpponent","iMoneyN"},{"bank","iChest"},{"open","openExit"},
        {"stage","bStartGame"},{"turn","dir_i"},{"first","dir_i_start"}})facts.emplace_back(key,TableInteger(name));
    auto *npc=static_cast<VDATA*>(core.GetScriptVariable("npchar"));
    auto *person=npc?npc->GetAClass():nullptr;
    std::optional<std::string> name=TableAttribute(person,"name");
    if(name)for(const char *field:{"lastname"})if(auto part=TableAttribute(person,field);part&&!part->empty())*name+=" "+*part;
    facts.emplace_back("opponent",name);facts.emplace_back("opponentFace",TableAttribute(person,"FaceId"));
    if(storm::android::mobileScreen=="boal_dice"){
        facts.emplace_back("locked",TableInteger("bLockClick"));
        auto *dice=static_cast<VDATA*>(core.GetScriptVariable("DiceState"));
        auto *state=dice?dice->GetAClass():nullptr;
        TableDice(state,"Hero",facts);TableDice(state,"Comp",facts);
    }else facts.emplace_back("cardMove",TableInteger("cardMove"));
    storm::mobile::CollectTableGame(source,widgets);
}
