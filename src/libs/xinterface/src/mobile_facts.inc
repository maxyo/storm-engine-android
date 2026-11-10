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
// event AndroidInterfaceFacts(screen, subject) fills GameInterface.AndroidFacts.<list>.<row>.<field>;
// subject is the index of the character the interface shows (the stock scripts' xi_refCharacter)
// or -1. Every row becomes a read-only widget ANDROID_<LIST> of type "fact": index = row ordinal,
// rowId = row name, label and value = its fields of that name, every field in context.
void XINTERFACE::MobileCollectFacts(std::vector<MobileWidget> &widgets)
{
    int subject=-1;
    // A ref is read only once it points somewhere (GetAClass of an unset ref is a script error).
    if(auto *variable=static_cast<VDATA *>(core.GetScriptVariable("xi_refCharacter")))
        if(variable->GetReference())
            if(auto *character=variable->GetAClass())
                if(const char *index=character->GetAttribute("index"))subject=std::atoi(index);
    core.Event("AndroidInterfaceFacts","sl",storm::android::mobileScreen.c_str(),subject);
    auto *facts=core.Entity_GetAttributeClass(g_idInterface,"AndroidFacts");
    for(size_t l=0;facts&&l<facts->GetAttributesNum();++l) {
        auto *list=facts->GetAttributeClass(l);
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
            widgets.push_back(std::move(fact));
        }
    }
}
