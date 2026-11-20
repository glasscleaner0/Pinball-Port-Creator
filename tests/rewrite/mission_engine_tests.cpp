#include "mission_engine.hpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace pinball::rules;
void check(bool b,const char* what){if(!b)throw std::runtime_error(what);}
MissionEngine active(MissionId id){
 MissionEngine m;m.event(MissionEvent(MissionFeature::LaunchExit));m.select(id);
 MissionActions start=m.event(MissionEvent(MissionFeature::Ramp));
 check(start.started&&m.snapshot().phase==MissionPhase::Active,"selected mission did not start on ramp");
 return m;
}
MissionActions repeat(MissionEngine& m,MissionFeature feature,unsigned index,unsigned count,const MissionContext& context=MissionContext()){
 MissionActions last;
 for(unsigned i=0;i<count;++i)last=m.event(MissionEvent(feature,index),context);
 return last;
}
void check_done(const MissionEngine& m,const MissionActions& a,unsigned points,unsigned completions=1){
 check(a.completed&&a.special_points==points&&m.snapshot().completed==completions,"mission completion reward/count");
 check(m.snapshot().phase==MissionPhase::Selecting&&m.snapshot().active==MissionId::None,"completed mission still owns table");
}
void selection_and_launch(){
 MissionEngine m;check(m.snapshot().phase==MissionPhase::Waiting,"initial waiting state");
 m.event(MissionEvent(MissionFeature::Target,13));check(m.snapshot().selected==MissionId::None,"pre-deployment target selected");
 m.event(MissionEvent(MissionFeature::LaunchExit));m.event(MissionEvent(MissionFeature::Target,13));
 check(m.snapshot().selected==MissionId::LaunchTraining,"cadet first target selection");
 m.fuel_available(false);check(!m.event(MissionEvent(MissionFeature::Ramp)).started,"empty fuel starts mission");
 m.fuel_available(true);check(m.event(MissionEvent(MissionFeature::Ramp)).special_points==10000,"launch selection award");
 check(m.snapshot().remaining==3,"accepting ramp also counted as objective shot");
 m.event(MissionEvent(MissionFeature::Target,15));check(m.snapshot().active==MissionId::LaunchTraining,"target changed active mission");
 MissionEngine fourth;fourth.event(MissionEvent(MissionFeature::LaunchExit));
 for(unsigned i=13;i<=15;++i)fourth.event(MissionEvent(MissionFeature::Target,i));
 check(fourth.snapshot().selected==MissionId::Science&&fourth.snapshot().selection_mask==0,"all three selectors fail fourth option");
 fourth.event(MissionEvent(MissionFeature::Target,15));check(fourth.snapshot().selected==MissionId::Practice,"selection mask did not reset");
}
void basic_objectives(){
 {MissionEngine m=active(MissionId::Practice);repeat(m,MissionFeature::Bumper,5,20);check(m.snapshot().remaining==8,"launch bumpers count as attack");repeat(m,MissionFeature::Bumper,2,7);check(!m.event(MissionEvent(MissionFeature::Flag,1)).completed,"wrong final event completed mission");check_done(m,repeat(m,MissionFeature::Bumper,4,1),500000);}
 {MissionEngine m=active(MissionId::LaunchTraining);repeat(m,MissionFeature::Ramp,0,2);check_done(m,repeat(m,MissionFeature::Ramp,0,1),500000);}
 {MissionEngine m=active(MissionId::ReentryTraining);repeat(m,MissionFeature::Rollover,110,3);check(m.snapshot().remaining==3,"launch lanes count as reentry");check_done(m,repeat(m,MissionFeature::Rollover,1,3),500000);}
 {MissionEngine m=active(MissionId::Science);check_done(m,repeat(m,MissionFeature::Target,7,9),750000);}
 {MissionEngine m=active(MissionId::BugHunt);check_done(m,repeat(m,MissionFeature::Target,22,15),750000);}
 {MissionEngine m=active(MissionId::Satellite);repeat(m,MissionFeature::Bumper,1,10);check(m.snapshot().remaining==3,"wrong bumper counts satellite");check_done(m,repeat(m,MissionFeature::Bumper,4,3),1250000);}
 {MissionEngine m=active(MissionId::Reconnaissance);repeat(m,MissionFeature::Rollover,9,10);check(m.snapshot().remaining==15,"space warp counts reconnaissance");check_done(m,repeat(m,MissionFeature::Rollover,111,15),1250000);}
 {MissionEngine m=active(MissionId::Doomsday);repeat(m,MissionFeature::Rollover,5,3);check(m.snapshot().remaining==3,"return lane counts outlane");check_done(m,repeat(m,MissionFeature::Rollover,8,3),1250000);}
}
void conditions_and_sequences(){
 {MissionEngine m=active(MissionId::BlackHoleThreat);check(!m.event(MissionEvent(MissionFeature::BlackHole)).completed,"black hole before upgrade");MissionContext c;c.launch_level=1;check_done(m,m.event(MissionEvent(MissionFeature::BlackHole),c),1000000);}
 {MissionEngine m=active(MissionId::AlienMenace);repeat(m,MissionFeature::Bumper,1,8);check(m.snapshot().stage==0,"attack hits bypass upgrade");MissionContext c;c.attack_level=1;check(m.event(MissionEvent(MissionFeature::LevelChange,0),c).stage_changed,"attack upgrade failed stage transition");check_done(m,repeat(m,MissionFeature::Bumper,1,8),750000);}
 {MissionEngine m=active(MissionId::Rescue);check(!m.event(MissionEvent(MissionFeature::Hyperspace)).completed,"unlit rescue hyperspace");MissionContext c;c.booster_lit=true;check_done(m,m.event(MissionEvent(MissionFeature::Hyperspace),c),750000);}
 for(unsigned family=0;family<2;++family){
  MissionEngine m=active(family?MissionId::StrayComet:MissionId::SpaceRadiation);unsigned first=family?19:16;
  repeat(m,MissionFeature::Target,first,3);check(m.snapshot().remaining==2,"repeated hazard spot satisfied distinct set");
  m.event(MissionEvent(MissionFeature::Target,first+1));check(m.snapshot().stage==0,"hazard set completed too early");
  check(m.event(MissionEvent(MissionFeature::Target,first+2)).stage_changed&&m.snapshot().remaining==1,"hazard set failed transition");
  check_done(m,m.event(MissionEvent(family?MissionFeature::Hyperspace:MissionFeature::Sink,2)),1000000);
 }
 {MissionEngine m=active(MissionId::CosmicPlague);repeat(m,MissionFeature::Flag,1,74);check(m.snapshot().stage==0,"plague stage early");check(m.event(MissionEvent(MissionFeature::Flag,2)).stage_changed,"75 flags failed");check_done(m,m.event(MissionEvent(MissionFeature::Rollover,9)),1750000);}
 {MissionEngine m=active(MissionId::Secret);m.event(MissionEvent(MissionFeature::Sink,1));check(m.snapshot().stage==0,"secret starts wrong colour");for(unsigned i: {3U,1U})check(m.event(MissionEvent(MissionFeature::Sink,i)).stage_changed,"secret colour sequence failed");check_done(m,m.event(MissionEvent(MissionFeature::Sink,2)),1500000);}
}
void maelstrom_and_time_warp(){
 MissionEngine m=active(MissionId::Maelstrom);
 check(repeat(m,MissionFeature::Target,2,3).stage_changed,"maelstrom popup");
 check(repeat(m,MissionFeature::Target,12,3).stage_changed,"maelstrom spots");
 check(repeat(m,MissionFeature::Rollover,7,5).stage_changed,"maelstrom lanes");
 check(m.event(MissionEvent(MissionFeature::FuelTop)).stage_changed,"maelstrom fuel");
 check(m.event(MissionEvent(MissionFeature::Ramp)).stage_changed,"maelstrom ramp");
 check(m.event(MissionEvent(MissionFeature::Flag,2)).stage_changed,"maelstrom flag");
 check(m.event(MissionEvent(MissionFeature::Sink,3)).stage_changed,"maelstrom sink");
 MissionActions done=m.event(MissionEvent(MissionFeature::Hyperspace));check_done(m,done,5000000);
 check(done.rank_changed&&m.snapshot().rank==2&&m.snapshot().rank_progress==0,"full outer circle must advance one rank");
 m.select(MissionId::TimeWarp);m.event(MissionEvent(MissionFeature::Ramp));repeat(m,MissionFeature::Rebound,3,25);
 check_done(m,m.event(MissionEvent(MissionFeature::Hyperspace)),2000000,2);
 check(m.snapshot().rank==1&&m.snapshot().rank_progress==0,"hyperspace warp added progress or failed rank down");
 m.select(MissionId::TimeWarp);m.event(MissionEvent(MissionFeature::Ramp));repeat(m,MissionFeature::Rebound,1,25);
 check_done(m,m.event(MissionEvent(MissionFeature::Ramp)),2000000,3);
 check(m.snapshot().rank==2&&m.snapshot().rank_progress==12,"ramp warp rank/progress");
}
void lifecycle_and_rank_selection(){
 MissionEngine m=active(MissionId::Practice);repeat(m,MissionFeature::Bumper,1,7);
 check(m.fuel_available(false).failed&&m.snapshot().phase==MissionPhase::Selecting,"fuel exhaustion failed to cancel");
 check(!m.event(MissionEvent(MissionFeature::Bumper,1)).completed,"cancelled mission scored");
 m.fuel_available(true);m.select(MissionId::Maelstrom);m.event(MissionEvent(MissionFeature::Ramp));m.complete_for_test();
 unsigned rank=m.snapshot().rank;m.ordinary_loss();check(m.snapshot().rank==rank&&m.snapshot().phase==MissionPhase::Waiting,"ordinary loss changed rank");
 m.event(MissionEvent(MissionFeature::LaunchExit));m.event(MissionEvent(MissionFeature::Target,13));check(m.snapshot().selected==MissionId::BugHunt,"rank2 selection matrix");
 m.game_over();check(!m.event(MissionEvent(MissionFeature::Ramp)).started&&m.objective_text()=="Game Over","game over accepted events");
 m.reset();check(m.snapshot().rank==1&&m.snapshot().rank_progress==0&&m.snapshot().completed==0,"new game retained progress");
 bool rejected=false;try{m.select(static_cast<MissionId>(19));}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid mission accepted");
}
}
int main(){try{selection_and_launch();basic_objectives();conditions_and_sequences();maelstrom_and_time_warp();lifecycle_and_rank_selection();std::cout<<"Mission objective tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Mission objective test failed: "<<e.what()<<'\n';return 1;}}