// XINTERFACE members (included by xinterface.cpp on Android): widgets the portable UI needs
// that the stock interfaces do not draw.

// The hero screens draw the PIRATES column next to the skills (mockup V2-Hero-Skills). The stock
// lscchar_all fills TABLE_SPECIAL but shows it only inside SPECIAL_WINDOW: while that window is
// hidden the rows are exported read-only (no actions) from the same table.
void XINTERFACE::MobileCollectHero(std::vector<MobileWidget> &widgets)
{
    if(std::any_of(widgets.begin(),widgets.end(),[](const auto &w){return storm::iEquals(w.node,"TABLE_SPECIAL");}))return;
    auto *table=FindNode("TABLE_SPECIAL",nullptr);
    if(!table||table->m_nNodeType!=NODETYPE_TABLE)return;
    std::vector<MobileWidget> rows;
    table->MobileCollect(rows);
    for(auto &row:rows){row.enabled=false;row.actions.clear();widgets.push_back(std::move(row));}
}

// Facts of the authored interface adapters (application mobile-ui/PROGRAM/interface/android_*.c):
// event AndroidInterfaceFacts(screen, subject, partner) fills GameInterface.AndroidFacts.<list>.<row>.<field>;
// subject is the index of the character the interface shows (the stock scripts' xi_refCharacter)
// or -1, partner the index of the character it deals with (the shipyard's master, shipyard.c
// refNPCShipyard: his prices) or -1. Every row becomes a read-only widget ANDROID_<LIST> of type "fact": index = row ordinal,
// rowId = row name, label and value = its fields of that name, every field in context.
void XINTERFACE::MobileCollectFacts(std::vector<MobileWidget> &widgets)
{
    // A ref is read only once it points somewhere (GetAClass of an unset ref is a script error); a
    // global of a segment that is not loaded is not found.
    const auto character_of=[](const char *name){
        if(auto *variable=static_cast<VDATA *>(core.GetScriptVariable(name)))
            if(variable->GetReference())
                if(auto *character=variable->GetAClass())
                    if(const char *index=character->GetAttribute("index"))return std::atoi(index);
        return -1;
    };
    const int subject=character_of("xi_refCharacter");
    const int partner=character_of("refNPCShipyard");
    const char *language=pStringService?pStringService->GetLanguage():nullptr;
    // Collected again only when its input may have changed (mobile_facts_cache.hpp).
    decltype(mobileFacts_)::Key key{storm::android::mobileScreenRevision,subject,language?language:"",storm::ScriptStateGeneration()};
    if(key.opening!=mobileFactsOpening_.first) {
        // How often the facts had to be collected (diagnostics: every frame would mean a script
        // changing state every frame while the interface is open).
        if(mobileFactsOpening_.first)
            spdlog::info("UI model {}: adapter facts collected {} times in {} interface frames",mobileFactsOpening_.second,
                         mobileFacts_.Collections(),mobileFactsFrames_);
        mobileFactsOpening_={key.opening,storm::android::mobileScreen};
        mobileFactsFrames_=0;
    }
    ++mobileFactsFrames_;
    const MobileFacts *facts=mobileFacts_.Find(key);
    if(!facts) {
        STORM_PROFILE_ZONE("UI model: adapter facts");
        MobileFacts fresh;
        {
            // The trees the adapter reads are the ones whose changes recollect (script_state.hpp).
            const storm::ScriptStateReads reads;
            core.Event("AndroidInterfaceFacts","sll",storm::android::mobileScreen.c_str(),subject,partner);
        }
        // The adapter's word on the open interface's script (mobile_interface_leaving.hpp): "1" loaded,
        // "0" unloaded, anything else unknown.
        {
            std::optional<bool> script;
            if(AttributesPointer)
                if(const char *value=AttributesPointer->GetAttribute("AndroidScript")) {
                    if(std::string_view(value)=="1")script=true;
                    else if(std::string_view(value)=="0")script=false;
                }
            static bool missingSaid=false;
            if(!script&&!missingSaid) {
                missingSaid=true;
                core.Trace("ERROR interface %s: the application's adapter does not say whether its script is loaded (GameInterface.AndroidScript): "
                           "an interface that is leaving may show a half-emptied screen",storm::android::mobileScreen.c_str());
            }
            fresh.left=mobileLeaving_.Update(storm::android::mobileScreenRevision,script);
        }
        auto *list_root=core.Entity_GetAttributeClass(g_idInterface,"AndroidFacts");
        for(size_t l=0;list_root&&l<list_root->GetAttributesNum();++l) {
            auto *list=list_root->GetAttributeClass(l);
            if(!list)continue;
            std::string node="ANDROID_"+std::string(list->GetThisName());
            for(auto &c:node)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            for(size_t r=0;r<list->GetAttributesNum();++r) {
                auto *row=list->GetAttributeClass(r);
                if(!row)continue;
                MobileWidget fact{node,"fact","","",static_cast<int>(r),-4,false};
                fact.rowId=row->GetThisName();
                for(size_t f=0;f<row->GetAttributesNum();++f)
                    if(auto *field=row->GetAttributeClass(f))
                        fact.context[field->GetThisName()]=field->GetThisAttr()?field->GetThisAttr():"";
                if(fact.context.count("label"))fact.label=fact.context["label"];
                if(fact.context.count("value"))fact.value=fact.context["value"];
                fresh.widgets.push_back(std::move(fact));
            }
        }
        key.state=storm::ScriptStateGeneration();
        facts=&mobileFacts_.Store(std::move(key),std::move(fresh));
        STORM_PROFILE_PLOT("UI model: adapter facts collections",static_cast<int64_t>(mobileFacts_.Collections()));
    }
    mobileLeft_=facts->left;
    widgets.insert(widgets.end(),facts->widgets.begin(),facts->widgets.end());
}
