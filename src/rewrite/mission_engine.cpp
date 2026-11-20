#include "mission_engine.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
namespace pinball { namespace rules {
namespace {
enum class Objective { AttackHits, Ramp, Reentry, Popup, AnyTarget, LeftHazard,
 RightHazard, BlackHole, AttackUpgrade, LitHyperspace, Satellite, Lanes,
 Outlanes, Flags, SpaceWarp, SinkYellow, SinkRed, SinkGreen, Rebounds,
 WarpChoice, Standing, Fuel, AnyFlag, AnySink, Hyperspace };
struct Stage { Objective objective; unsigned count; };
struct Definition { MissionId id; const char* title; std::uint32_t reward; unsigned rank; };
const Definition definitions[]={
 {MissionId::Practice,"Practice Mission",500000,6},
 {MissionId::LaunchTraining,"Launch Training",500000,6},
 {MissionId::ReentryTraining,"Reentry Training",500000,6},
 {MissionId::Science,"Science Mission",750000,9},
 {MissionId::StrayComet,"Stray Comet",1000000,8},
 {MissionId::BlackHoleThreat,"Black Hole Threat",1000000,8},
 {MissionId::SpaceRadiation,"Space Radiation",1000000,8},
 {MissionId::BugHunt,"Bug Hunt",750000,7},
 {MissionId::AlienMenace,"Alien Menace",750000,7},
 {MissionId::Rescue,"Rescue Mission",750000,7},
 {MissionId::Satellite,"Satellite",1250000,9},
 {MissionId::Reconnaissance,"Reconnaissance",1250000,9},
 {MissionId::Doomsday,"Doomsday Machine",1250000,9},
 {MissionId::CosmicPlague,"Cosmic Plague",1750000,11},
 {MissionId::Secret,"Secret Mission",1500000,10},
 {MissionId::TimeWarp,"Time Warp",2000000,12},
 {MissionId::Maelstrom,"Maelstrom",5000000,18}
};
const Definition& definition(MissionId id){
 for(const Definition& d:definitions)if(d.id==id)return d;
 throw std::invalid_argument("invalid playable mission");
}
Stage objective(MissionId id,unsigned stage){
 switch(id){
 case MissionId::Practice:return {Objective::AttackHits,8};
 case MissionId::LaunchTraining:return {Objective::Ramp,3};
 case MissionId::ReentryTraining:return {Objective::Reentry,3};
 case MissionId::Science:return {Objective::Popup,9};
 case MissionId::StrayComet:return stage?Stage{Objective::Hyperspace,1}:Stage{Objective::RightHazard,3};
 case MissionId::BlackHoleThreat:return {Objective::BlackHole,1};
 case MissionId::SpaceRadiation:return stage?Stage{Objective::AnySink,1}:Stage{Objective::LeftHazard,3};
 case MissionId::BugHunt:return {Objective::AnyTarget,15};
 case MissionId::AlienMenace:return stage?Stage{Objective::AttackHits,8}:Stage{Objective::AttackUpgrade,1};
 case MissionId::Rescue:return {Objective::LitHyperspace,1};
 case MissionId::Satellite:return {Objective::Satellite,3};
 case MissionId::Reconnaissance:return {Objective::Lanes,15};
 case MissionId::Doomsday:return {Objective::Outlanes,3};
 case MissionId::CosmicPlague:return stage?Stage{Objective::SpaceWarp,1}:Stage{Objective::Flags,75};
 case MissionId::Secret:{const Stage list[]={{Objective::SinkYellow,1},{Objective::SinkRed,1},{Objective::SinkGreen,1}};return list[stage];}
 case MissionId::TimeWarp:return stage?Stage{Objective::WarpChoice,1}:Stage{Objective::Rebounds,25};
 case MissionId::Maelstrom:{const Stage list[]={{Objective::Popup,3},{Objective::Standing,3},{Objective::Lanes,5},{Objective::Fuel,1},{Objective::Ramp,1},{Objective::AnyFlag,1},{Objective::AnySink,1},{Objective::Hyperspace,1}};return list[stage];}
 default:throw std::invalid_argument("mission has no objective");
 }
}
unsigned stages(MissionId id){
 if(id==MissionId::Maelstrom)return 8;
 if(id==MissionId::Secret)return 3;
 if(id==MissionId::StrayComet||id==MissionId::SpaceRadiation||id==MissionId::AlienMenace||id==MissionId::CosmicPlague||id==MissionId::TimeWarp)return 2;
 return 1;
}
bool lane(unsigned index){return (index>=1&&index<=8)||(index>=110&&index<=112);}
bool eligible(Objective objective,const MissionEvent& e,const MissionContext& c){
 switch(objective){
 case Objective::AttackHits:return e.feature==MissionFeature::Bumper&&e.index>=1&&e.index<=4;
 case Objective::Ramp:return e.feature==MissionFeature::Ramp;
 case Objective::Reentry:return e.feature==MissionFeature::Rollover&&e.index>=1&&e.index<=3;
 case Objective::Popup:return e.feature==MissionFeature::Target&&e.index>=1&&e.index<=9;
 case Objective::AnyTarget:return e.feature==MissionFeature::Target&&e.index>=1&&e.index<=22;
 case Objective::LeftHazard:return e.feature==MissionFeature::Target&&e.index>=16&&e.index<=18;
 case Objective::RightHazard:return e.feature==MissionFeature::Target&&e.index>=19&&e.index<=21;
 case Objective::BlackHole:return e.feature==MissionFeature::BlackHole&&c.launch_level>0;
 case Objective::AttackUpgrade:return e.feature==MissionFeature::LevelChange&&e.index==0&&c.attack_level>0;
 case Objective::LitHyperspace:return e.feature==MissionFeature::Hyperspace&&c.booster_lit;
 case Objective::Satellite:return e.feature==MissionFeature::Bumper&&e.index==4;
 case Objective::Lanes:return e.feature==MissionFeature::Rollover&&lane(e.index);
 case Objective::Outlanes:return e.feature==MissionFeature::Rollover&&(e.index==4||e.index==8);
 case Objective::Flags:case Objective::AnyFlag:return e.feature==MissionFeature::Flag&&(e.index==1||e.index==2);
 case Objective::SpaceWarp:return e.feature==MissionFeature::Rollover&&e.index==9;
 case Objective::SinkYellow:return e.feature==MissionFeature::Sink&&e.index==3;
 case Objective::SinkRed:return e.feature==MissionFeature::Sink&&e.index==1;
 case Objective::SinkGreen:return e.feature==MissionFeature::Sink&&e.index==2;
 case Objective::Rebounds:return e.feature==MissionFeature::Rebound&&e.index>=1&&e.index<=4;
 case Objective::WarpChoice:return e.feature==MissionFeature::Ramp||e.feature==MissionFeature::Hyperspace;
 case Objective::Standing:return e.feature==MissionFeature::Target&&e.index>=10&&e.index<=22;
 case Objective::Fuel:return e.feature==MissionFeature::FuelTop;
 case Objective::AnySink:return e.feature==MissionFeature::Sink&&e.index>=1&&e.index<=3;
 case Objective::Hyperspace:return e.feature==MissionFeature::Hyperspace;
 }
 return false;
}
MissionId selection(unsigned rank,unsigned target){
 const MissionId choices[5][4]={
 {MissionId::LaunchTraining,MissionId::ReentryTraining,MissionId::Practice,MissionId::Science},
 {MissionId::BugHunt,MissionId::Rescue,MissionId::AlienMenace,MissionId::Secret},
 {MissionId::StrayComet,MissionId::SpaceRadiation,MissionId::BlackHoleThreat,MissionId::CosmicPlague},
 {MissionId::Satellite,MissionId::Reconnaissance,MissionId::Doomsday,MissionId::TimeWarp},
 {MissionId::CosmicPlague,MissionId::Secret,MissionId::TimeWarp,MissionId::Maelstrom}};
 unsigned row=rank==1?0:rank<=3?1:rank<=5?2:rank<=7?3:4;
 return choices[row][target];
}
const char* instruction(Objective o){
 switch(o){
 case Objective::AttackHits:return "Hit attack bumpers"; case Objective::Ramp:return "Shoot the launch ramp";
 case Objective::Reentry:return "Pass reentry lanes";case Objective::Popup:return "Hit drop targets";
 case Objective::AnyTarget:return "Hit targets";case Objective::LeftHazard:return "Complete left hazard targets";
 case Objective::RightHazard:return "Complete right hazard targets";case Objective::BlackHole:return "Upgrade launch bumpers, then enter black hole";
 case Objective::AttackUpgrade:return "Upgrade attack bumpers";case Objective::LitHyperspace:return "Enable boosters, then enter hyperspace";
 case Objective::Satellite:return "Hit the satellite bumper";case Objective::Lanes:return "Pass lanes";
 case Objective::Outlanes:return "Pass outlanes";case Objective::Flags:return "Hit flags";
 case Objective::SpaceWarp:return "Pass space warp";case Objective::SinkYellow:return "Enter yellow wormhole";
 case Objective::SinkRed:return "Enter red wormhole";case Objective::SinkGreen:return "Enter green wormhole";
 case Objective::Rebounds:return "Hit rebounders";case Objective::WarpChoice:return "Ramp: rank up / hyperspace: rank down";
 case Objective::Standing:return "Hit spot targets";case Objective::Fuel:return "Fill fuel through the top fuel lane";
 case Objective::AnyFlag:return "Hit a flag";case Objective::AnySink:return "Enter a wormhole";
 case Objective::Hyperspace:return "Enter hyperspace";
 }
 return "";
}
}
MissionActions::MissionActions():special_points(0),started(false),stage_changed(false),completed(false),failed(false),rank_changed(false),reset_attack(false),reset_launch(false),rearm_popups(false),clear_spots(false),rank_delta(0),clear_spot_families(0){}
MissionEngine::MissionEngine(){reset();}
void MissionEngine::reset(){state_={MissionPhase::Waiting,MissionId::None,MissionId::None,0,0,1,0,0,0,true};objective_mask_=0;}
void MissionEngine::ordinary_loss(){state_.phase=MissionPhase::Waiting;state_.active=state_.selected=MissionId::None;state_.stage=state_.remaining=state_.selection_mask=0;objective_mask_=0;}
bool MissionEngine::promote_rank(){if(state_.rank>=9)return false;++state_.rank;return true;}
void MissionEngine::game_over(){ordinary_loss();state_.phase=MissionPhase::GameOver;}
MissionActions MissionEngine::fuel_available(bool available){
 state_.fuel_available=available;MissionActions actions;
 if(!available&&state_.phase==MissionPhase::Active){state_.phase=MissionPhase::Selecting;state_.active=state_.selected=MissionId::None;state_.remaining=state_.stage=0;objective_mask_=0;actions.failed=true;}
 return actions;
}
const char* MissionEngine::name(MissionId id){return id==MissionId::None?"":definition(id).title;}
MissionActions MissionEngine::select(MissionId id){
 definition(id);MissionActions actions;
 if(state_.phase!=MissionPhase::Active&&state_.phase!=MissionPhase::GameOver){state_.phase=MissionPhase::Selecting;state_.selected=id;}
 return actions;
}
void MissionEngine::set_stage(unsigned stage,MissionActions& actions){
 state_.stage=stage;state_.remaining=objective(state_.active,stage).count;objective_mask_=0;actions.stage_changed=true;
 if(!stage){
  actions.reset_attack=state_.active==MissionId::AlienMenace;
  actions.reset_launch=state_.active==MissionId::BlackHoleThreat;
  actions.rearm_popups=state_.active==MissionId::Science||state_.active==MissionId::BugHunt;
  actions.clear_spot_families=state_.active==MissionId::BugHunt?15U:state_.active==MissionId::SpaceRadiation?4U:state_.active==MissionId::StrayComet?8U:0U;
  actions.clear_spots=actions.clear_spot_families!=0;
 }
}
MissionActions MissionEngine::begin(MissionId id){
 MissionActions actions;const unsigned value=static_cast<unsigned>(id);definition(id);
 state_.phase=MissionPhase::Active;state_.active=id;state_.selected=MissionId::None;state_.selection_mask=0;actions.started=true;
 actions.special_points=value<=5?10000:value<=14?20000:30000;
 set_stage(0,actions);return actions;
}
void MissionEngine::add_rank(unsigned amount,MissionActions& actions){
 state_.rank_progress=std::min(18U,state_.rank_progress+amount);
 if(state_.rank_progress==18){state_.rank_progress=0;if(state_.rank<9){++state_.rank;actions.rank_changed=true;++actions.rank_delta;}}
}
MissionActions MissionEngine::advance_objective(bool rank_reward){
 MissionActions actions;
 if(state_.stage+1<stages(state_.active)){set_stage(state_.stage+1,actions);return actions;}
 const Definition& d=definition(state_.active);actions.special_points=d.reward;actions.completed=true;
 if(rank_reward)add_rank(d.rank,actions);++state_.completed;state_.phase=MissionPhase::Selecting;state_.active=state_.selected=MissionId::None;state_.remaining=state_.stage=state_.selection_mask=0;return actions;
}
MissionActions MissionEngine::complete_for_test(){
 if(state_.phase!=MissionPhase::Active)return MissionActions();
 while(state_.stage+1<stages(state_.active)){MissionActions ignored;set_stage(state_.stage+1,ignored);}
 return advance_objective();
}
MissionActions MissionEngine::event(const MissionEvent& e,const MissionContext& context){
 MissionActions actions;
 if(state_.phase==MissionPhase::GameOver)return actions;
 if(state_.phase==MissionPhase::Waiting){if(e.feature==MissionFeature::LaunchExit)state_.phase=MissionPhase::Selecting;return actions;}
 if(state_.phase==MissionPhase::Selecting){
  if(e.feature==MissionFeature::Target&&e.index>=13&&e.index<=15){
   const unsigned bit=1U<<(e.index-13);state_.selection_mask|=bit;
   const unsigned choice=state_.selection_mask==7?3:e.index-13;
   state_.selected=selection(state_.rank,choice);if(choice==3)state_.selection_mask=0;
  }else if(e.feature==MissionFeature::Ramp&&state_.selected!=MissionId::None&&state_.fuel_available)return begin(state_.selected);
  return actions;
 }
 const Stage current=objective(state_.active,state_.stage);
 if(!eligible(current.objective,e,context))return actions;
 if(current.objective==Objective::LeftHazard||current.objective==Objective::RightHazard){
  const unsigned bit=1U<<(e.index-(current.objective==Objective::LeftHazard?16:19));
  if(objective_mask_&bit)return actions;objective_mask_|=bit;
 }
 if(state_.remaining)--state_.remaining;
 if(state_.remaining)return actions;
 if(current.objective==Objective::WarpChoice){
  if(e.feature==MissionFeature::Hyperspace){if(state_.rank>1){--state_.rank;actions.rank_changed=true;--actions.rank_delta;}}
  else if(state_.rank<9){++state_.rank;actions.rank_changed=true;++actions.rank_delta;}
 }
 MissionActions next=advance_objective(!(current.objective==Objective::WarpChoice&&e.feature==MissionFeature::Hyperspace));
 next.rank_changed=next.rank_changed||actions.rank_changed;next.rank_delta+=actions.rank_delta;
 return next;
}
MissionSnapshot MissionEngine::snapshot()const{return state_;}
std::string MissionEngine::objective_text()const{
 if(state_.phase==MissionPhase::GameOver)return "Game Over";
 if(state_.phase==MissionPhase::Waiting)return "Awaiting Deployment";
 if(state_.phase==MissionPhase::Selecting){
  if(!state_.fuel_available)return "Refuel Ship";
  if(state_.selected==MissionId::None)return "Select a Mission";
  return std::string("Shoot Ramp to Accept ")+name(state_.selected);
 }
 std::ostringstream text;text<<instruction(objective(state_.active,state_.stage).objective);
 if(state_.remaining>1)text<<": "<<state_.remaining;
 return text.str();
}
std::vector<unsigned> MissionEngine::guidance_lights(const MissionContext& context) const {
 if(state_.phase==MissionPhase::Selecting)return {state_.fuel_available?(state_.selected==MissionId::None?319U:317U):318U};
 if(state_.phase!=MissionPhase::Active)return {};
 switch(state_.active){
 case MissionId::Practice:return {308,311};
 case MissionId::LaunchTraining:return {317};
 case MissionId::ReentryTraining:return {307};
 case MissionId::Science:return {303,309,315};
 case MissionId::StrayComet:return state_.stage?std::vector<unsigned>{304}:std::vector<unsigned>{306};
 case MissionId::BlackHoleThreat:return {context.launch_level?314U:316U};
 case MissionId::SpaceRadiation:return state_.stage?std::vector<unsigned>{}:std::vector<unsigned>{313};
 case MissionId::BugHunt:return {306,308,310,313,319};
 case MissionId::AlienMenace:return state_.stage?std::vector<unsigned>{308,311}:std::vector<unsigned>{307};
 case MissionId::Rescue:return {context.booster_lit?304U:303U};
 case MissionId::Satellite:return {308};
 case MissionId::Reconnaissance:return {301,302,307,316,320,321};
 case MissionId::Doomsday:return {301,320};
 case MissionId::CosmicPlague:return state_.stage?std::vector<unsigned>{310}:std::vector<unsigned>{305,312};
 case MissionId::Secret:return {};
 case MissionId::TimeWarp:return state_.stage?std::vector<unsigned>{304,317}:std::vector<unsigned>{300,322};
 case MissionId::Maelstrom:switch(state_.stage){
  case 0:return {303,309,315};case 1:return {306,308,310,313,319};
  case 2:return {301,302,307,316,320,321};case 3:return {318};case 4:return {317};
  case 5:return {305,312};case 6:return {};case 7:return {304};default:return {};
 }
 default:return {};
 }
}
} }