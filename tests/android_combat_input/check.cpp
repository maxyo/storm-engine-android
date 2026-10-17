#include <platform/android_combat_input.hpp>
#include <cassert>
#include <iostream>
using namespace storm::android;
std::string packet(const CombatInput &c,unsigned seq,const char *action,const char *phase="tap")
{
 return "1\t"+std::to_string(c.revision())+"\t"+std::to_string(c.generation())+"\t"+std::to_string(seq)+"\t"+action+"\t"+phase;
}
int main()
{
 CombatInput c; bool valid=true;c.guard=[&]{return valid;};c.resetActivity();
 const std::vector<CombatBinding> bindings={{"force","ChrAttackForce2",false},{"fast","ChrAttackFast2",false},{"block","ChrBlock",true},{"potion","BOAL_UsePotion",false}};
 c.update(10,"player1","melee",bindings);
 assert(c.offer(packet(c,1,"force"),100)); assert(!c.offer(packet(c,1,"force"),100));
 c.advance(101);assert(c.merge("ChrAttackForce2",0)==3);assert(c.merge("ChrAttackForce2",0)==3); // polling is stable
 assert(c.merge("ChrAttackForce2",1)==1);assert(c.merge("ChrAttackForce2",3)==3); // physical source is not reactivated
 assert(c.offer(packet(c,2,"force"),102));c.advance(102);assert(c.merge("ChrAttackForce2",0)==2);
 c.advance(103);assert(c.merge("ChrAttackForce2",0)==3);c.advance(104);assert(c.merge("ChrAttackForce2",0)==2);
 c.advance(105);assert(c.merge("ChrAttackForce2",0)==0);
 assert(c.offer(packet(c,3,"block","down"),106));c.advance(107);assert(c.merge("ChrBlock",0)==3);
 c.advance(108);assert(c.merge("ChrBlock",0)==1);assert(c.offer(packet(c,4,"block","up"),109));assert(c.merge("ChrBlock",0)==2);
 assert(c.offer(packet(c,5,"fast"),110));cancelAndroidCombatInput();c.advance(111);assert(c.merge("ChrAttackFast2",0)!=3);
 assert(c.offer(packet(c,6,"block","down"),112));c.advance(113);cancelAndroidCombatInput();assert(c.merge("ChrBlock",0)==2);
 const auto drainedEpoch=combatCancelEpoch.load();const auto drained=packet(c,7,"block","down");
 cancelAndroidCombatInput();assert(!c.offer(drained,114,drainedEpoch));c.advance(114);assert(c.merge("ChrBlock",0)!=3);
 assert(!c.offer(packet(c,7,"force","down"),114));assert(!c.offer(packet(c,7,"BOAL_UsePotion"),114));
 assert(!c.offer("1\t10\tbad\t7\tforce\ttap",114));assert(!c.offer(packet(c,7,"force")+"\textra",114));
 assert(c.offer(packet(c,7,"force"),100));c.advance(1101);assert(c.merge("ChrAttackForce2",0)!=3);
 assert(c.offer(packet(c,8,"force"),0xfffffff0u));c.advance(0x10u);assert(c.merge("ChrAttackForce2",0)==3);
 const auto stale=packet(c,9,"force");c.update(11,"player2","melee",bindings);assert(!c.offer(stale,100));
 const auto oldOwner=packet(c,9,"force");c.update(11,"player3","melee",bindings);assert(!c.offer(oldOwner,100));
 auto remapped=bindings;remapped[0].control="ChrAttackForce";
 const auto oldMapping=packet(c,9,"force");c.update(11,"player3","melee",remapped);assert(!c.offer(oldMapping,100));
 c.update(11,"player3","melee",bindings);
 assert(c.offer(packet(c,9,"block","down"),100));c.advance(101);valid=false;assert(c.merge("ChrBlock",0)==2);valid=true;
 const auto oldActivity=packet(c,10,"force");c.resetActivity();assert(!c.offer(oldActivity,100));
 for(unsigned i=1;i<=16;++i)assert(c.offer(packet(c,i,"force"),100));assert(!c.offer(packet(c,17,"force"),100));
 assert(c.offer(packet(c,18,"block","up"),100));assert(c.offer(packet(c,19,"all","cancel"),100));c.advance(101);assert(c.merge("ChrAttackForce2",0)!=3);
 c.update(12,"","unknown",{});assert(!c.offer(packet(c,20,"force"),101));
 std::cout<<"PASS combat semantic pulses, physical merge, held release, atomic cancel, scene/activity/age/sequence guards\n";
}
