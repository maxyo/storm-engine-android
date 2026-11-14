#include <platform/android_combat_input.hpp>
#include <cassert>
#include <iostream>
using namespace storm::android;
std::string packet(const CombatInput &c,unsigned seq,const char *action,const char *phase="tap")
{
 return "2\t"+std::to_string(c.generation())+"\t"+std::to_string(c.actionGeneration(action))+"\t"+std::to_string(seq)+"\t"+action+"\t"+phase;
}
int main()
{
 CombatControlAvailability allowed;
 assert(!allowed.contains("ChrFire"));
 std::string descriptorName="ChrFire";
 allowed.allow(descriptorName);
 descriptorName.assign(1000,'x'); // script callback may replace the controls table
 assert(allowed.contains("ChrFire"));
 assert(!allowed.contains(descriptorName));
 allowed.allow("UnrelatedControl");assert(!allowed.contains("UnrelatedControl"));
 for(const char *alias:{"ChrFightMode","BOAL_UsePotion","ChrAction","ChrCamSpecMode",
     "ChrAttackForce2","ChrAttackForce","ChrAttackFast2","ChrAttackFast",
     "ChrAttackBreak","ChrAttackBreak2","ChrParry","ChrParry2","ChrAttackFient",
     "ChrAttackFient2","ChrAttackRound","ChrAttackRound2","ChrFire","ChrBlock","ChrBlock2"}) {
     allowed.allow(alias);assert(allowed.contains(alias));
 }
 CombatControlAvailability nextFrame;assert(!nextFrame.contains("ChrFire"));
 CombatInput c; bool valid=true;c.guard=[&]{return valid;};c.resetActivity();
 const std::vector<CombatBinding> bindings={{"force","ChrAttackForce2",false},{"fast","ChrAttackFast2",false},{"block","ChrBlock",true},{"potion","BOAL_UsePotion",false}};
 c.update(10,"town","player1","melee",bindings);
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
 assert(!c.offer("2\t"+std::to_string(c.generation())+"\tbad\t7\tforce\ttap",114));
 assert(!c.offer("1"+packet(c,7,"force").substr(1),114)); // the old wire version is refusedassert(!c.offer(packet(c,7,"force")+"\textra",114));
 assert(c.offer(packet(c,7,"force"),100));c.advance(1101);assert(c.merge("ChrAttackForce2",0)!=3);
 assert(c.offer(packet(c,8,"force"),0xfffffff0u));c.advance(0x10u);assert(c.merge("ChrAttackForce2",0)==3);
 const auto stale=packet(c,9,"force");c.update(11,"town","player2","melee",bindings);assert(!c.offer(stale,100));
 const auto oldOwner=packet(c,9,"force");c.update(11,"town","player3","melee",bindings);assert(!c.offer(oldOwner,100));
 auto remapped=bindings;remapped[0].control="ChrAttackForce";
 const auto oldMapping=packet(c,9,"force");c.update(11,"town","player3","melee",remapped);assert(!c.offer(oldMapping,100));
 c.update(11,"town","player3","melee",bindings);
 assert(c.offer(packet(c,9,"block","down"),100));c.advance(101);valid=false;assert(c.merge("ChrBlock",0)==2);valid=true;
 const auto oldActivity=packet(c,10,"force");c.resetActivity();assert(!c.offer(oldActivity,100));
 for(unsigned i=1;i<=16;++i)assert(c.offer(packet(c,i,"force"),100));assert(!c.offer(packet(c,17,"force"),100));
 assert(c.offer(packet(c,18,"block","up"),100));assert(c.offer(packet(c,19,"all","cancel"),100));c.advance(101);assert(c.merge("ChrAttackForce2",0)!=3);
 // Granularity: availability changes fence only the action whose availability changed.
 c.resetActivity();c.update(13,"town","player3","melee",bindings);
 const auto inFlight=packet(c,3,"force");assert(c.offer(packet(c,2,"fast"),200));
 auto noPotion=bindings;noPotion.pop_back();c.update(14,"town","player3","melee",noPotion); // potion gone, new revision
 assert(c.offer(inFlight,200)); // stamped before the change, still valid
 c.update(15,"town","player3","melee",bindings); // potion back: other actions keep their generation
 c.advance(201);assert(c.merge("ChrAttackFast2",0)==3); // the queued fast tap survived both changes
 c.advance(202);assert(c.merge("ChrAttackForce2",0)==3);c.advance(203);c.advance(204);
 assert(c.offer(packet(c,4,"block","down"),206));c.advance(207);assert(c.merge("ChrBlock",0)==3);
 const auto oldBlock=packet(c,5,"block","up");const auto queued=packet(c,6,"force");assert(c.offer(queued,207));
 auto noBlock=bindings;noBlock.erase(noBlock.begin()+2);c.update(16,"town","player3","melee",noBlock);
 assert(c.merge("ChrBlock",0)==2); // a vanished action lets go of its hold at once
 assert(c.merge("ChrAttackForce2",0)!=2); // … and only of its own control
 c.update(17,"town","player3","melee",bindings); // block back with a new generation
 assert(!c.offer(oldBlock,208)); // a command stamped with the old availability is refused
 c.advance(208);assert(c.merge("ChrAttackForce2",0)==3); // the unrelated queued tap still fires
 const auto oldScene=packet(c,7,"force");c.update(17,"harbour","player3","melee",bindings);assert(!c.offer(oldScene,209)); // a new scene ends the session
 c.update(12,"","","unknown",{});assert(!c.offer(packet(c,20,"force"),101));
 std::cout<<"PASS combat semantic pulses, physical merge, held release, atomic cancel, per-action availability and scene/activity/age/sequence guards\n";
}
