from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/libs/xinterface/src/nodes/xi_button.cpp').read_text()
method=source[source.index('void CXI_BUTTON::MobileCollect('):source.index('\nbool CXI_BUTTON::MobileApply(')]
fixture=r'''
#include "mobile_widget.hpp"
#include <cassert>
#include <iostream>
namespace storm::android { inline std::string mobileScreen; }
enum { ACTION_MOUSECLICK=1,ACTION_ACTIVATE=2 };
struct Strings {
 const char* GetString(int) { return "Ship"; }
 int GetStringNum(const char* key) { return std::string(key)=="buttonShip"?0:-1; }
};
struct CXI_BUTTON {
 const char *m_nodeName="MAP_EXIT_BTN";
 int m_idString=-1;
 Strings *pStringService=nullptr;
 bool m_bUse=true,m_bClickable=true,m_bLockedNode=false,m_bLockStatus=false;
 bool click=true,activate=false;
 struct {int top=20,left=731;} m_rect;
 bool CheckCommandUsed(int command) const {return command==ACTION_MOUSECLICK?click:activate;}
 void MobileCollect(std::vector<MobileWidget>& widgets) const;
};
'''
checks=r'''
int main() {
 storm::android::mobileScreen="items";
 CXI_BUTTON button;
 auto collect=[&](){std::vector<MobileWidget> widgets;button.MobileCollect(widgets);return widgets;};
 auto widgets=collect();
 assert(widgets.size()==1 && widgets[0].node=="MAP_EXIT_BTN" && widgets[0].label=="×");
 assert(widgets[0].type=="button" && widgets[0].index==-1 && widgets[0].enabled);
 assert(widgets[0].actions==std::vector<std::string>{"press"});
 button.m_bUse=false;assert(collect().empty());button.m_bUse=true;
 button.m_bClickable=false;assert(collect().empty());button.m_bClickable=true;
 button.m_bLockedNode=true;assert(collect().empty());button.m_bLockedNode=false;
 button.m_bLockStatus=true;assert(collect().empty());button.m_bLockStatus=false;
 button.click=false;assert(collect().empty());button.activate=true;assert(collect().size()==1);
 storm::android::mobileScreen="ship";assert(collect().empty());
 storm::android::mobileScreen="items";button.m_nodeName="UNLABELED_OTHER";assert(collect().empty());
 Strings strings;button.pStringService=&strings;button.m_nodeName="I_SHIP";
 widgets=collect();assert(widgets.size()==1 && widgets[0].label=="Ship");
 assert(widgets[0].actions.empty()); // unchanged preexisting button metadata
 button.pStringService=nullptr;
 for(const char *screen:{"store","hirecrew"}){storm::android::mobileScreen=screen;
 for(const char *name:{"QTY_ADD_BUTTON","QTY_REMOVE_BUTTON","QTY_ADD_ALL_BUTTON","QTY_REMOVE_ALL_BUTTON"}){
  button.m_nodeName=name;widgets=collect();assert(widgets.size()==1&&widgets[0].enabled&&!widgets[0].label.empty());
  button.m_bUse=false;assert(collect().empty());button.m_bUse=true;
  button.m_bLockedNode=true;assert(!collect()[0].enabled);button.m_bLockedNode=false;
  button.click=false;button.activate=false;assert(!collect()[0].enabled);button.click=true;
 }
 }
 storm::android::mobileScreen="save_load";assert(collect().empty());
 storm::android::mobileScreen="store";button.m_nodeName="QTY_UNKNOWN_BUTTON";assert(collect().empty());
 std::cout << "PASS actual MobileCollect map close hidden/disabled/screen/command boundaries and legacy navigation\n";
}
'''
with tempfile.TemporaryDirectory(prefix='storm-map-close-host-') as tmp:
 cpp=Path(tmp)/'check.cpp';cpp.write_text(fixture+method+checks)
 binary=Path(tmp)/'check'
 subprocess.run(['c++','-std=c++20','-fsanitize=address,undefined','-I'+str(root/'src/libs/xinterface/src'),str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
