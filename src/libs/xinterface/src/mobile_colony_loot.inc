// Included inside xinterface.cpp's Android implementation: «Забрать самое ценное» of a looted town
// (PROGRAM/interface/ColonyCapture.c). The touch UI decides which goods and how much of each (the most
// valuable per unit of hold space first) and asks for all of them in one action; the engine makes the
// player's own moves for each good in one script call: select the row (CS_TableSelectChange →
// ShowGoodsInfo), type the amount, confirm (TransactionOK, which clamps to the hold, moves the goods and
// refreshes the table). No game rules here.
//
// The action: node ANDROID_LOOT, value "take@<opening>:<row>=<amount>,<row>=<amount>…", rows being the
// table's 1-based tr numbers of that opening.
bool XINTERFACE::MobileApplyColonyLoot(const std::string &value)
{
    const std::string head="take@"+std::to_string(storm::android::mobileScreenRevision)+":";
    if(value.rfind(head,0)!=0){core.Trace("colony loot: %s refused (another opening or verb)",value.c_str());return false;}
    ATTRIBUTES *table=AttributesPointer?AttributesPointer->GetAttributeClass("TABLE_LIST"):nullptr;
    if(!table){core.Trace("colony loot: no TABLE_LIST");return false;}
    int taken=0;
    for(size_t at=head.size();at<value.size();){
        size_t end=value.find(',',at);if(end==std::string::npos)end=value.size();
        const std::string item=value.substr(at,end-at);at=end+1;
        const size_t equals=item.find('=');
        const int row=std::atoi(item.c_str()),amount=equals==std::string::npos?0:std::atoi(item.c_str()+equals+1);
        if(row<1||amount<1||!table->GetAttributeClass(("tr"+std::to_string(row)).c_str())){
            core.Trace("colony loot: bad item '%s'",item.c_str());return false;
        }
        core.Event("TableSelectChange","sl","TABLE_LIST",static_cast<int32_t>(row));
        ATTRIBUTES *edit=AttributesPointer->GetAttributeClass("qty_edit");
        if(!edit)edit=AttributesPointer->CreateSubAClass(AttributesPointer,"qty_edit");
        if(!edit)return false;
        edit->SetAttribute("str",std::to_string(amount));
        core.Event("TransactionOK");
        ++taken;
    }
    return taken>0;
}
