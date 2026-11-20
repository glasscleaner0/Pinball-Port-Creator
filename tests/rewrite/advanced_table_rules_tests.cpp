#include "advanced_table_rules.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace pinball::rules;using pinball::runtime::Tick;
const Tick second=1000000000LL;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class E,class F>void throws(F f,const char* message){try{f();}catch(const E&){return;}throw std::runtime_error(message);}
AdvancedRulesConfig config(){
    AdvancedRulesConfig c;
    // Explicit synthetic durations test selection; production resolves WAVs.
    c.hyperspace_delays={{second,2*second,3*second,4*second,5*second}};
    c.hyperspace_reflex_delay=800000000;c.hyperspace_combined_delay=700000000;c.hyperspace_special_delay=900000000;
    return c;
}
void record(AdvancedTableRules& r,const AdvancedActions& a){
    if(!a.ordinary_recorded)r.record_ordinary(a.ordinary_points);
}
void fuel_and_pulses(){
    AdvancedTableRules r(config());
    check(r.snapshot().fuel_index==0&&!r.snapshot().fuel_countdown&&!r.snapshot().fuel_empty,"serve fuel index/countdown");
    for(unsigned i=0;i<6;++i)check(r.snapshot().fuel_lamps[i],"serve bar not visually full");
    r.advance(100*second);check(!r.snapshot().fuel_countdown,"serve started fuel countdown without gate");
    check(r.rollover(179,100*second).ordinary_points==500&&r.snapshot().fuel_index==1,"low fuel roller must compare source index0");
    r.advance(114*second);check(r.snapshot().fuel_index==0&&!r.snapshot().fuel_empty,"last flashing fuel interval");
    check(r.advance(117*second).fuel_empty&&r.snapshot().fuel_empty,"fuel did not end after index0 interval");
    check(!r.advance(118*second).fuel_empty,"fuel expiry emitted twice");
    r.skill_gate(118*second);check(r.snapshot().fuel_index==11&&r.snapshot().fuel_countdown,"gate did not refill/start fuel11");
    r.rollover(179,118*second);check(!r.snapshot().fuel_lamps[0]&&r.snapshot().fuel_index==11,"high fuel roller did not pulse only");
    r.advance(118*second+49999999);check(!r.snapshot().fuel_lamps[0],"fuel pulse ended early");
    r.advance(118*second+50000000);check(r.snapshot().fuel_lamps[0],"fuel pulse did not restore after50ms");
    check(r.advance(190*second).fuel_empty,"full source fuel sequence is72s");
    r.rollover(184,190*second);check(!r.snapshot().fuel_empty&&r.snapshot().fuel_index==11,"empty fuel did not refill");
}
void standing_targets_and_spots(){
    AdvancedTableRules r(config());
    check(r.target(10,0).ordinary_points==750&&!r.snapshot().target_enabled[10]&&r.snapshot().target_fired[10]&&
          r.snapshot().target_remaining[10]==100000000,"standing target image/cooldown");
    check(!r.target(10,99999999).accepted,"standing target repeat accepted inside100ms");
    r.advance(100000000);check(r.snapshot().target_enabled[10]&&!r.snapshot().target_fired[10],"standing target did not reenable");
    r.target(11,100000000);r.target(12,100000000);
    check(r.snapshot().fuel_index==11&&r.snapshot().fuel_countdown,"fuel trio did not refill");
    for(unsigned n=13;n<=15;++n)check(r.target(n,100000000).ordinary_points==1000,"mission target points");
    check(r.snapshot().mission_mask==7,"mission OR selection bits");
    for(unsigned n=16;n<=18;++n)r.target(n,100000000);
    check(r.snapshot().hazard_masks[0]==7&&r.snapshot().gates_open[0],"left hazard completion");
    for(unsigned n=19;n<=21;++n)r.target(n,100000000);
    check(r.snapshot().hazard_masks[1]==7&&r.snapshot().gates_open[1],"right hazard completion");
    r.advance(2100000000LL);
    for(unsigned i=0;i<3;++i)check(!r.snapshot().mission_lamps[i]&&!r.snapshot().fuel_spots[i],"completed trio did not turn off");
    check(r.snapshot().mission_mask==7&&r.snapshot().hazard_masks[1]==7,"light expiry cleared persistent source masks");
    r.clear_spots(2);check(r.snapshot().mission_mask==0&&r.snapshot().hazard_masks[1]==7,"family-specific spot clear");
    r.clear_spots(8);check(r.snapshot().hazard_masks[1]==0&&r.snapshot().gates_open[1],"spot clear improperly closed gate");
}
void booster_pools_and_hold(){
    AdvancedTableRules r(config());Tick now=0;
    for(unsigned completion=0;completion<5;++completion){
        record(r,r.target(1,now));check(!r.target(1,now).accepted,"down booster repeated");
        record(r,r.target(2,now));const AdvancedActions last=r.target(3,now);record(r,last);
        check(last.rearm_booster&&last.ordinary_points==(completion==4?10000U:5000U),"booster reward sequence/double award");
        check(!r.snapshot().target_enabled[1]&&r.snapshot().target_remaining[1]==250000000,"booster pending rearm");
        now+=250000000;r.advance(now);check(r.snapshot().target_enabled[1],"booster did not rise");
    }
    check(r.snapshot().flags_active&&r.snapshot().bonus_hold&&r.snapshot().bonus_pool==32000&&
          r.snapshot().jackpot_pool==48000,"base-only pool accrual/order");
    const AdvancedActions flag=r.flag_hit(1,false,now);check(flag.ordinary_points==2500&&flag.flag_hits==2,"flag bonus/notification");
    r.enable(AdvancedFeature::ReturnLanes,now);
    const AdvancedActions ret=r.rollover(6,now);record(r,ret);
    check(ret.ordinary_points==25000&&!r.light(59).visible&&r.snapshot().bonus_active,
          "return-lane indicator cancellation wrongly cleared source bonus flag");
    r.advance(62*second);check(!r.snapshot().jackpot_active&&r.snapshot().bonus_active,"pool expiry/cancelled timer semantics");
    r.record_ordinary((std::numeric_limits<std::uint32_t>::max)());
    check(r.snapshot().bonus_pool==5000000,"bonus cap overflow");
    r.ordinary_loss(62*second);check(r.snapshot().bonus_pool==5000000&&!r.snapshot().bonus_hold,"bonus hold not consumed/preserved");
    r.serve(false,62*second);r.ordinary_loss(63*second);
    check(r.snapshot().bonus_pool==25000,"unheld ordinary loss did not select subsequent bonus");
}
void skill_and_wormholes(){
    AdvancedTableRules r(config());r.skill_gate(0);
    const std::uint32_t points[6]={15000,30000,75000,30000,15000,7500};
    for(unsigned count=1;count<=6;++count){
        if(count>1)r.skill_tripwire(count,0);
        check(r.skill_award(0).ordinary_points==points[count-1],"source skill score array/count");
    }
    r.skill_clear(1);
    for(unsigned i=0;i<6;++i)check(!r.snapshot().skill_lamps[i],"deployment clear did not clear ALL skill lamps");
    r.skill_tripwire(2,2);check(!r.snapshot().skill_lamps[1],"disarmed skill tripwire relit");
    check(r.skill_award(2).ordinary_points==0,"empty skills awarded");
    AdvancedTableRules worm(config());
    AdvancedActions a=worm.wormhole(0,0);
    check(a.ordinary_points==2500&&a.wormhole_destination==0&&!a.replay,"unlit wormhole route");
    worm.target(22,100000000);check(worm.snapshot().wormhole_destination==1&&worm.light(4).frame==2,"destination first advance/frame");
    a=worm.wormhole(1,100000000);check(a.ordinary_points==7500&&a.wormhole_destination==0&&!a.replay,"wrong lit wormhole route");
    worm.advance(200000000);worm.target(22,200000000);a=worm.wormhole(0,200000000);
    check(a.ordinary_points==5000&&a.replay&&a.wormhole_destination==0&&worm.snapshot().wormhole_destination==0,"matching wormhole replay/consume");
    worm.set_mission_state(16);worm.advance(300000000);worm.target(22,300000000);
    check(worm.snapshot().wormhole_destination==0,"blocked mission advanced destination");
    worm.set_mission_state(0);worm.flag_hit(0,true,300000000);
    check(worm.snapshot().wormhole_destination==0,"spinner armed an unlit destination");
    worm.advance(400000000);worm.target(22,400000000);worm.flag_hit(0,true,400000000);
    check(worm.snapshot().wormhole_destination==2&&worm.light(2).frame==1,"spinner did not advance armed destination");
    worm.set_mission_state(16);
    check(worm.snapshot().wormhole_destination==0&&!worm.light(2).visible&&!worm.light(110).visible,
          "Secret Yellow start did not clear physical destination state");
}
void hyperspace_selection_and_atomic_unresolved(){
    AdvancedTableRules r(config());
    const std::uint32_t ordinary[5]={10000,0,20000,50000,150000};
    for(unsigned stage=0;stage<5;++stage){
        const Tick now=static_cast<Tick>(stage)*6*second;
        const AdvancedActions a=r.hyperspace(now);
        check(a.ordinary_points==ordinary[stage]&&a.hyperspace_delay==static_cast<Tick>(stage+1)*second,
              "hyperspace source stage/sound duration selection");
        check(a.special_points==(stage==1?20000U:0U),"hyperspace jackpot special/reflex boundary");
        check(a.ordinary_recorded==(ordinary[stage]!=0),"ordered hyperspace pool accounting metadata");
        if(stage==2)check(a.enable_blocker&&r.snapshot().blocker_on,"hyperspace blocker stage");
        if(stage==3)check(r.snapshot().extra_ball_lit,"hyperspace extra-ball-light stage");
        if(stage==4)check(a.enable_gravity_well&&r.snapshot().hyperspace_level==0,"hyperspace gravity stage/clear");
    }
    AdvancedTableRules reflex(config());reflex.skill_gate(0);
    const AdvancedActions shot=reflex.hyperspace(0);
    check(shot.hyperspace_delay==800000000&&shot.special_points==25000,"own hyper reflex sound/points");
    AdvancedTableRules super(config());super.enable(AdvancedFeature::HyperspaceSuper,0);
    const AdvancedActions special=super.hyperspace(0);
    check(special.set_multiplier_max&&special.set_medals_max&&special.enable_blocker&&special.enable_gravity_well&&
          special.hyperspace_delay==900000000&&super.snapshot().bonus_pool==100000&&super.snapshot().jackpot_pool==100000,
          "hyper super effects or pre-effect ordinary accrual order");
    AdvancedTableRules unresolved((AdvancedRulesConfig()));
    throws<std::logic_error>([&]{unresolved.hyperspace(second);},"unresolved sound used guessed delay");
    check(unresolved.time()==0&&unresolved.snapshot().hyperspace_level==0,"unresolved sound mutated state");
}
void mission_flags_and_gate_callbacks(){
    AdvancedTableRules r(config());r.mission_flags(true);
    check(r.flag_hit(0,false,0).ordinary_points==2500&&!r.snapshot().booster_lit,
          "mission flags incorrectly enabled booster61");
    r.enable(AdvancedFeature::Flags,0);r.advance(61*second);
    check(r.flag_hit(1,false,61*second).ordinary_points==2500&&!r.snapshot().booster_lit,
          "booster timeout erased forced mission flags");
    r.enable(AdvancedFeature::Flags,61*second);r.mission_flags(false);
    check(r.snapshot().booster_lit&&!r.snapshot().flags_active&&!r.light(19).visible&&!r.light(20).visible&&
          r.flag_hit(0,false,61*second).ordinary_points==500,"mission exit failed to force19/20OFF independently");
    r.enable(AdvancedFeature::Flags,61*second);
    check(r.flag_hit(0,false,61*second).ordinary_points==2500,"new booster flag reward failed to restore flags");
    r.close_gate(0,61*second);
    check(!r.snapshot().gates_open[0]&&!r.light(30).visible&&!r.light(196).visible,"kickback callback did not close pure gate feedback");
}
void mission_hyperspace_and_consumed_selector(){
    AdvancedTableRules r(config());r.mission_hyperspace(true,false);
    const AdvancedActions combined=r.hyperspace(0);
    check(combined.hyperspace_delay==700000000&&!combined.set_multiplier_max,"mission combined sound/effects");
    r.mission_hyperspace(false,true);
    check(!r.light(26).visible&&r.light(130).visible,"mission stage lamp transition");
    check(r.hyperspace(6*second).set_multiplier_max,"mission super stage did not enable source hyper reward");
    r.mission_hyperspace(false,false);check(!r.light(26).visible&&!r.light(130).visible,"mission exit retained hyper lamps");
    AdvancedTableRules selector(config());selector.target(13,0);selector.target(14,0);selector.target(15,0);
    selector.advance(100000000);const std::array<bool,3> lamps=selector.snapshot().mission_lamps;
    check(selector.snapshot().mission_mask==7,"selector setup");
    selector.consume_mission_selection();
    check(selector.snapshot().mission_mask==0&&selector.snapshot().mission_lamps==lamps,"selector consumption cancelled source lamp flash");
}
void control_sound_selection(){
    check(AdvancedActions().sound_control==0,"default action invented a sound");
    AdvancedTableRules booster(config());Tick now=0;
    const unsigned rewards[5]={47,45,46,48,48};
    for(unsigned reward=0;reward<5;++reward){
        check(booster.target(1,now).sound_control==0&&booster.target(2,now).sound_control==0,"incomplete booster invented control sound");
        check(booster.target(3,now).sound_control==rewards[reward],"booster selected wrong feature announcement");
        check(!booster.target(3,now).accepted&&booster.target(3,now).sound_control==0,"down target repeated announcement");
        now+=250000000;booster.advance(now);
    }
    const unsigned suppressed[2]={15,29};
    for(unsigned i=0;i<2;++i){AdvancedTableRules r(config());r.set_mission_state(suppressed[i]);r.target(1,0);r.target(2,0);
        check(r.target(3,0).sound_control==0,"mission-suppressed first booster reward sounded");}
    AdvancedTableRules spots(config());
    check(spots.target(10,0).sound_control==49&&spots.target(11,0).sound_control==49&&spots.target(12,0).sound_control==25,
          "fuel spot incomplete/completion control sound");
    check(spots.target(10,0).sound_control==0,"latched solo target repeated control sound");
    check(spots.target(13,0).sound_control==52,"selecting mission target must request original52");
    spots.set_mission_state(2);check(spots.target(14,0).sound_control==49,"steady active mission target must select49D");
    spots.set_mission_state(1);check(spots.target(15,0).sound_control==52,"nonactive mission target selected49D");
    check(spots.target(16,0).sound_control==49&&spots.target(17,0).sound_control==49&&spots.target(18,0).sound_control==14,
          "left hazard completion sound");
    check(spots.target(19,0).sound_control==49&&spots.target(20,0).sound_control==49&&spots.target(21,0).sound_control==14,
          "right hazard completion sound");
    check(spots.target(22,0).sound_control==0&&spots.rollover(179,0).sound_control==0&&spots.rollover(4,0).sound_control==0,
          "silent control branch invented duplicate audio");
    check(spots.rollover(5,0).sound_control==25,"unlit bonus lane sound");
    spots.enable(AdvancedFeature::BonusLane,0);check(spots.rollover(5,0).sound_control==50,"lit bonus lane sound");
    AdvancedTableRules hyper(config());const unsigned stages[5]={35,36,35,38,39};
    for(unsigned i=0;i<5;++i)check(hyper.hyperspace(static_cast<Tick>(i)*6*second).sound_control==stages[i],"hyperspace stage sound");
    AdvancedTableRules priority(config());priority.enable(AdvancedFeature::HyperspaceReflex,0);
    check(priority.hyperspace(0).sound_control==21,"hyperspace reflex sound");
    priority.enable(AdvancedFeature::HyperspaceCombined,0);check(priority.hyperspace(0).sound_control==40,"combined did not outrank reflex sound");
    priority.enable(AdvancedFeature::HyperspaceSuper,0);check(priority.hyperspace(0).sound_control==41,"super did not outrank combined/reflex sound");
}
void timed_rewards_and_wide_guards(){
    AdvancedTableRules r(config());r.enable(AdvancedFeature::ExtraBall,0);r.enable(AdvancedFeature::Blocker,0);
    r.advance(55*second);check(r.snapshot().extra_ball_lit&&r.snapshot().blocker_on,"55+5 reward flash missing");
    const AdvancedActions at_expiry=r.rollover(4,60*second);
    check(at_expiry.extra_ball&&!r.snapshot().extra_ball_lit,"equal-time outlane missed protected reward");
    r.advance(60*second);check(!r.snapshot().blocker_on&&!r.rollover(8,60*second).extra_ball,"reward expired or consumed twice");
    AdvancedRulesConfig c=config();c.target_duration=4294967296LL;
    AdvancedTableRules wide(c,2147483648LL);wide.target(10,2147483648LL);
    wide.advance(2147483648LL+4294967295LL);
    check(!wide.snapshot().target_enabled[10]&&wide.snapshot().target_remaining[10]==1,"wide target timer expired early");
    wide.advance(2147483648LL+4294967296LL);check(wide.snapshot().target_enabled[10],"wide target timer did not expire");
    const Tick maximum=(std::numeric_limits<Tick>::max)();
    AdvancedTableRules end(config(),maximum-10);end.target(10,maximum-9);end.advance(maximum);
    check(end.snapshot().target_remaining[10]==100000000-9,"near-max timer overflow");
    throws<std::invalid_argument>([&]{end.advance(maximum-1);},"backward time accepted");
    throws<std::invalid_argument>([&]{end.advance(-1);},"negative time accepted");
    throws<std::out_of_range>([&]{end.target(4,maximum);},"unsupported target accepted");
    throws<std::out_of_range>([&]{end.enable(static_cast<AdvancedFeature>(99),maximum);},"invalid feature accepted");
    AdvancedRulesConfig bad=config();bad.flash_off=maximum;bad.flash_on=1;
    throws<std::invalid_argument>([&]{AdvancedTableRules invalid(bad);},"flash sum overflow accepted");
    AdvancedTableRules jump(config());jump.skill_gate(0);jump.target(10,0);jump.enable(AdvancedFeature::Bonus,0);
    check(jump.advance(maximum).fuel_empty&&jump.snapshot().fuel_empty&&jump.snapshot().target_enabled[10]&&!jump.snapshot().bonus_active,
          "large advance did not bound/complete finite feature timers");
}
}
int main(){
    try{fuel_and_pulses();standing_targets_and_spots();booster_pools_and_hold();skill_and_wormholes();
        hyperspace_selection_and_atomic_unresolved();mission_flags_and_gate_callbacks();mission_hyperspace_and_consumed_selector();control_sound_selection();timed_rewards_and_wide_guards();
        std::cout<<"advanced_table_rules_tests PASS:fuel,targets,pools,skill,wormhole routing,hyperspace,timer boundaries\n";return 0;
    }catch(const std::exception& e){std::cerr<<"advanced_table_rules_tests FAIL: "<<e.what()<<'\n';return 1;}
}




