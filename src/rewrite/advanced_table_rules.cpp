#include "advanced_table_rules.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace pinball { namespace rules {
namespace {
const runtime::Tick second=1000000000LL;
bool before(runtime::Tick a,runtime::Tick b){
    volatile std::uint32_t ah=static_cast<std::uint32_t>(static_cast<std::uint64_t>(a)>>32);
    volatile std::uint32_t al=static_cast<std::uint32_t>(a);
    volatile std::uint32_t bh=static_cast<std::uint32_t>(static_cast<std::uint64_t>(b)>>32);
    volatile std::uint32_t bl=static_cast<std::uint32_t>(b);
    return ah==bh?al<bl:ah<bh;
}
bool zero(runtime::Tick v){
    volatile std::uint32_t h=static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)>>32);
    volatile std::uint32_t l=static_cast<std::uint32_t>(v);return h==0&&l==0;
}
void require_time(runtime::Tick v){
    volatile std::uint32_t h=static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)>>32);
    if((h&0x80000000U)!=0)throw std::invalid_argument("advanced time must be nonnegative");
}
runtime::Tick left(runtime::Tick now,runtime::Tick start,runtime::Tick duration){
    const runtime::Tick age=now-start;return before(age,duration)?duration-age:0;
}
bool target_number(std::size_t n){return (n>=1&&n<=3)||(n>=10&&n<=22);}
}
AdvancedRulesConfig::AdvancedRulesConfig()
    :fuel_durations{{3*second,14*second,3*second,12*second,3*second,10*second,
                      3*second,8*second,3*second,6*second,3*second,4*second}},
     hyperspace_delays(),hyperspace_reflex_delay(0),hyperspace_combined_delay(0),hyperspace_special_delay(0),
     hyperspace_awards{{10000,0,20000,50000,150000}},skill_awards{{15000,30000,75000,30000,15000,7500}},
     target_duration(100000000),booster_rearm(250000000),spot_flash(2*second),destination_flash(3*second),
     feature_duration(60*second),extra_on(55*second),extra_flash(5*second),hyper_flash(2*second),
     hyper_decay(60*second),skill_flash(second),reflex_window(5*second),fuel_pulse(50000000),
     flash_off(100000000),flash_on(100000000),reflex_points(25000),bonus_cap(5000000),jackpot_cap(5000000){}
AdvancedActions::AdvancedActions()
    :accepted(false),ordinary_recorded(false),ordinary_points(0),special_points(0),extra_ball(false),replay(false),rearm_booster(false),
     enable_blocker(false),enable_gravity_well(false),set_multiplier_max(false),set_medals_max(false),
     fuel_empty(false),bonus_collect(false),flag_hits(0),wormhole_destination(-1),hyperspace_delay(0),sound_control(0){}
AdvancedTableRules::AdvancedTableRules(const AdvancedRulesConfig& config,runtime::Tick now):config_(config){
    validate_config();reset(now);
}
void AdvancedTableRules::validate_config() const{
    const runtime::Tick durations[]={config_.target_duration,config_.booster_rearm,config_.spot_flash,
        config_.destination_flash,config_.feature_duration,config_.extra_on,config_.extra_flash,
        config_.hyper_flash,config_.hyper_decay,config_.skill_flash,config_.reflex_window,
        config_.fuel_pulse,config_.flash_off,config_.flash_on};
    for(std::size_t i=0;i<sizeof(durations)/sizeof(durations[0]);++i){
        require_time(durations[i]);if(zero(durations[i]))throw std::invalid_argument("zero advanced duration");
    }
    for(unsigned i=0;i<12;++i){require_time(config_.fuel_durations[i]);if(zero(config_.fuel_durations[i]))
        throw std::invalid_argument("zero fuel duration");}
    for(unsigned i=0;i<5;++i){require_time(config_.hyperspace_delays[i]);if(config_.hyperspace_awards[i]>1000000000U)throw std::invalid_argument("hyperspace award range");}
    for(unsigned i=0;i<6;++i)if(config_.skill_awards[i]>1000000000U)throw std::invalid_argument("skill award range");
    if(config_.reflex_points>1000000000U)throw std::invalid_argument("reflex award range");
    require_time(config_.hyperspace_reflex_delay);require_time(config_.hyperspace_combined_delay);
    require_time(config_.hyperspace_special_delay);
    if(before((std::numeric_limits<runtime::Tick>::max)()-config_.flash_off,config_.flash_on)||
       config_.bonus_cap<10000||config_.jackpot_cap<20000||config_.bonus_cap>1000000000U||
       config_.jackpot_cap>1000000000U)throw std::invalid_argument("invalid advanced cap/flash period");
}
void AdvancedTableRules::validate_now(runtime::Tick now) const{
    require_time(now);if(before(now,now_))throw std::invalid_argument("advanced clock moved backward");
}
void AdvancedTableRules::on(Lamp& lamp,runtime::Tick duration){
    lamp=Lamp();lamp.on=true;lamp.started=now_;lamp.duration=duration;lamp.timed=!zero(duration);
}
void AdvancedTableRules::off(Lamp& lamp){lamp=Lamp();}
void AdvancedTableRules::flash(Lamp& lamp,runtime::Tick duration,bool then_on){
    if(then_on)lamp.on=true;
    if(!lamp.flashing){lamp.flash_started=now_;lamp.starts_on=!lamp.on;}
    lamp.flashing=true;lamp.after_on=then_on;lamp.started=now_;lamp.duration=duration;lamp.timed=!zero(duration);
}
bool AdvancedTableRules::active(const Lamp& lamp) const{return lamp.on||lamp.flashing;}
bool AdvancedTableRules::visible(const Lamp& lamp) const{
    if(!lamp.flashing)return lamp.on;
    const runtime::Tick phase=(now_-lamp.flash_started)%(config_.flash_off+config_.flash_on);
    return lamp.starts_on?before(phase,config_.flash_on):!before(phase,config_.flash_off);
}
void AdvancedTableRules::reset(runtime::Tick now){
    require_time(now);now_=fuel_started_=hyper_started_=booster_started_=skill_started_=now;
    fuel_index_=0;fuel_countdown_=false;fuel_full_=true;fuel_empty_=false;booster_pending_=false;
    skill_armed_=true;skill_flashing_=false;skill_on_.fill(false);skill_on_[0]=true;skill_flash_mask_.fill(false);
    targets_.fill(Lamp());booster_raised_.fill(true);fuel_spots_.fill(Lamp());mission_lamps_.fill(Lamp());
    for(unsigned b=0;b<2;++b)hazards_[b].fill(Lamp());
    fuel_pulses_.fill(Lamp());fuel_flash_cancelled_.fill(false);hyper_lamps_.fill(Lamp());features_.fill(Lamp());return_lamps_.fill(Lamp());
    gate_lamps_.fill(Lamp());gate_secondary_.fill(Lamp());flag_lamps_.fill(Lamp());extra_=blocker_=bonus_lane_=destination_lamp_=hyper_reflex_=hyper_combined_=hyper_super_=Lamp();
    worm_lamps_.fill(Lamp());worm_arrows_.fill(Lamp());mission_mask_=mission_state_=hyper_level_=destination_=worm_frame_=0;
    bonus_pool_=10000;jackpot_pool_=20000;hazard_masks_.fill(0);gates_open_.fill(true);
    hyper_decay_=bonus_accum_=jackpot_accum_=mission_flags_=false;gate_set(0,true);gate_set(1,true);
}
runtime::Tick AdvancedTableRules::next_boundary(bool& pending) const{
    runtime::Tick best=(std::numeric_limits<runtime::Tick>::max)();pending=false;
    const auto consider=[&](runtime::Tick delay){if(!pending||before(delay,best))best=delay;pending=true;};
    const auto lamp=[&](const Lamp& l){if(l.timed)consider(left(now_,l.started,l.duration));};
    for(unsigned i=0;i<23;++i)lamp(targets_[i]);
    for(unsigned i=0;i<3;++i){lamp(fuel_spots_[i]);lamp(mission_lamps_[i]);lamp(worm_lamps_[i]);lamp(worm_arrows_[i]);}
    for(unsigned b=0;b<2;++b){for(unsigned i=0;i<3;++i)lamp(hazards_[b][i]);lamp(return_lamps_[b]);lamp(gate_lamps_[b]);lamp(gate_secondary_[b]);lamp(flag_lamps_[b]);}
    for(unsigned i=0;i<6;++i)lamp(fuel_pulses_[i]);
    for(unsigned i=0;i<4;++i){lamp(hyper_lamps_[i]);lamp(features_[i]);}
    lamp(extra_);lamp(blocker_);lamp(bonus_lane_);lamp(destination_lamp_);lamp(hyper_reflex_);lamp(hyper_combined_);lamp(hyper_super_);
    if(fuel_countdown_)consider(left(now_,fuel_started_,config_.fuel_durations[static_cast<unsigned>(fuel_index_)]));
    if(hyper_decay_)consider(left(now_,hyper_started_,config_.hyper_decay));
    if(booster_pending_)consider(left(now_,booster_started_,config_.booster_rearm));
    if(skill_flashing_)consider(left(now_,skill_started_,config_.skill_flash));
    return best;
}
void AdvancedTableRules::expire_at_now(AdvancedActions& actions){
    const auto due=[&](const Lamp& l){return l.timed&&!before(now_-l.started,l.duration);};
    const auto expire=[&](Lamp& l){
        if(!due(l))return;
        if(l.flashing){l.on=l.after_on;l.flashing=l.timed=false;}
        else off(l);
    };
    if(due(features_[1]))jackpot_accum_=false;
    if(due(features_[2]))bonus_accum_=false;
    if(due(extra_)&&!extra_.flashing){extra_.on=false;flash(extra_,config_.extra_flash,false);}
    if(due(blocker_)&&!blocker_.flashing){blocker_.on=false;flash(blocker_,config_.extra_flash,false);}
    for(unsigned i=0;i<23;++i)expire(targets_[i]);
    for(unsigned i=0;i<3;++i){expire(fuel_spots_[i]);expire(mission_lamps_[i]);expire(worm_lamps_[i]);expire(worm_arrows_[i]);}
    for(unsigned b=0;b<2;++b){for(unsigned i=0;i<3;++i)expire(hazards_[b][i]);expire(return_lamps_[b]);expire(gate_lamps_[b]);expire(gate_secondary_[b]);expire(flag_lamps_[b]);}
    for(unsigned i=0;i<6;++i)expire(fuel_pulses_[i]);
    for(unsigned i=0;i<4;++i){expire(hyper_lamps_[i]);expire(features_[i]);}
    expire(extra_);expire(blocker_);expire(bonus_lane_);expire(destination_lamp_);expire(hyper_reflex_);expire(hyper_combined_);expire(hyper_super_);
    if(booster_pending_&&!before(now_-booster_started_,config_.booster_rearm)){
        booster_raised_.fill(true);booster_pending_=false;
    }
    if(skill_flashing_&&!before(now_-skill_started_,config_.skill_flash)){
        skill_flashing_=false;skill_flash_mask_.fill(false);
    }
    if(fuel_countdown_&&!before(now_-fuel_started_,config_.fuel_durations[static_cast<unsigned>(fuel_index_)])){
        if(fuel_index_>0){--fuel_index_;fuel_started_=now_;fuel_flash_cancelled_.fill(false);}
        else{fuel_countdown_=false;fuel_empty_=true;fuel_full_=false;actions.fuel_empty=true;}
    }
    if(hyper_decay_&&!before(now_-hyper_started_,config_.hyper_decay)){
        --hyper_level_;off(hyper_lamps_[hyper_level_]);
        if(hyper_level_==0)hyper_decay_=false;else hyper_started_=now_;
    }
}
AdvancedActions AdvancedTableRules::update(runtime::Tick now,bool inclusive){
    AdvancedActions actions;actions.accepted=true;
    for(;;){
        bool pending=false;const runtime::Tick delay=next_boundary(pending);
        if(!pending)break;const runtime::Tick available=now-now_;
        if(before(available,delay)||(!inclusive&&!before(delay,available)))break;
        now_+=delay;expire_at_now(actions);
    }
    now_=now;return actions;
}
AdvancedActions AdvancedTableRules::advance(runtime::Tick now){validate_now(now);return update(now,true);}
void AdvancedTableRules::refill(int index){
    fuel_index_=index;fuel_started_=now_;fuel_countdown_=true;fuel_full_=false;fuel_empty_=false;fuel_flash_cancelled_.fill(false);
}
AdvancedActions AdvancedTableRules::serve(bool saved,runtime::Tick now){
    validate_now(now);AdvancedActions a=update(now,false);
    if(!saved){
        fuel_index_=0;fuel_countdown_=false;fuel_full_=true;fuel_empty_=false;
        skill_armed_=true;skill_on_.fill(false);skill_on_[0]=true;skill_flash_mask_.fill(false);skill_flashing_=false;
        gate_set(0,true);gate_set(1,true);jackpot_accum_=bonus_accum_=false;
    }
    return a;
}
AdvancedActions AdvancedTableRules::ordinary_loss(runtime::Tick now){
    validate_now(now);update(now,false);
    const std::uint32_t bonus=active(features_[3])?bonus_pool_:25000U,jackpot=jackpot_pool_;
    reset(now);bonus_pool_=bonus;jackpot_pool_=jackpot;fuel_full_=false;fuel_empty_=true;
    skill_armed_=false;skill_on_.fill(false);gate_set(0,false);gate_set(1,false);
    AdvancedActions a;a.accepted=true;return a;
}
void AdvancedTableRules::gate_set(std::size_t index,bool open){
    gates_open_[index]=open;
    if(open){flash(gate_lamps_[index],config_.reflex_window,true);flash(gate_secondary_[index],config_.reflex_window,false);}
    else{off(gate_lamps_[index]);off(gate_secondary_[index]);}
}
AdvancedActions AdvancedTableRules::set_gate(std::size_t index,bool open,runtime::Tick now){
    if(index>=2)throw std::out_of_range("gate index");
    validate_now(now);AdvancedActions a=update(now,false);gate_set(index,open);return a;
}
AdvancedActions AdvancedTableRules::close_gate(std::size_t index,runtime::Tick now){return set_gate(index,false,now);}
void AdvancedTableRules::mission_hyperspace(bool combined,bool super){
    if(combined){if(!active(hyper_combined_)){on(hyper_combined_);flash(hyper_combined_,0,true);}}
    else off(hyper_combined_);
    if(super)on(hyper_super_);else off(hyper_super_);
}
void AdvancedTableRules::consume_mission_selection(){mission_mask_=0;}
void AdvancedTableRules::mission_flags(bool enabled){
    mission_flags_=enabled;
    for(unsigned i=0;i<2;++i){if(enabled)on(flag_lamps_[i]);else off(flag_lamps_[i]);}
}
void AdvancedTableRules::record_ordinary(std::uint32_t base){
    if(jackpot_accum_)jackpot_pool_=static_cast<std::uint32_t>((std::min)(std::uint64_t(config_.jackpot_cap),std::uint64_t(jackpot_pool_)+base));
    if(bonus_accum_)bonus_pool_=static_cast<std::uint32_t>((std::min)(std::uint64_t(config_.bonus_cap),std::uint64_t(bonus_pool_)+base));
}
void AdvancedTableRules::set_mission_state(std::uint32_t state){
    if(state==16&&mission_state_!=16){destination_=0;worm_lamps_.fill(Lamp());worm_arrows_.fill(Lamp());off(destination_lamp_);}
    mission_state_=state;
}
void AdvancedTableRules::request_booster_rearm(runtime::Tick now){
    validate_now(now);update(now,false);booster_pending_=true;booster_started_=now_;
}
void AdvancedTableRules::clear_spots(std::uint32_t families){
    if((families&~15U)!=0)throw std::invalid_argument("unknown spot family");
    if((families&1U)!=0)fuel_spots_.fill(Lamp());
    if((families&2U)!=0){mission_lamps_.fill(Lamp());mission_mask_=0;}
    for(unsigned b=0;b<2;++b)if((families&(4U<<b))!=0){hazards_[b].fill(Lamp());hazard_masks_[b]=0;}
}
void AdvancedTableRules::feature_enable(AdvancedFeature f){
    switch(f){
    case AdvancedFeature::Flags:
        on(features_[0],config_.feature_duration);
        for(unsigned i=0;i<2;++i)on(flag_lamps_[i],mission_flags_?0:config_.feature_duration);break;
    case AdvancedFeature::Jackpot:on(features_[1],config_.feature_duration);jackpot_accum_=true;break;
    case AdvancedFeature::Bonus:on(features_[2],config_.feature_duration);bonus_accum_=true;break;
    case AdvancedFeature::BonusHold:on(features_[3]);break;
    case AdvancedFeature::ExtraBall:on(extra_,config_.extra_on);break;
    case AdvancedFeature::BonusLane:on(bonus_lane_);break;
    case AdvancedFeature::ReturnLanes:on(return_lamps_[0]);on(return_lamps_[1]);break;
    case AdvancedFeature::Blocker:on(blocker_,config_.extra_on);break;
    case AdvancedFeature::HyperspaceReflex:flash(hyper_reflex_,config_.reflex_window,false);break;
    case AdvancedFeature::HyperspaceCombined:on(hyper_combined_);break;
    case AdvancedFeature::HyperspaceSuper:on(hyper_super_);break;
    }
}
AdvancedActions AdvancedTableRules::enable(AdvancedFeature f,runtime::Tick now){
    if(static_cast<unsigned>(f)>static_cast<unsigned>(AdvancedFeature::HyperspaceSuper))throw std::out_of_range("advanced feature");
    validate_now(now);AdvancedActions a=update(now,false);feature_enable(f);
    a.enable_blocker=f==AdvancedFeature::Blocker;return a;
}
void AdvancedTableRules::destination_advance(bool force){
    if(mission_state_==16||mission_state_==22||mission_state_==23)return;
    if(force||destination_!=0){
        destination_=destination_%3+1;worm_frame_=3-destination_;
        for(unsigned i=0;i<3;++i){on(worm_lamps_[i]);on(worm_arrows_[i]);}
    }
}
AdvancedActions AdvancedTableRules::target(std::size_t number,runtime::Tick now){
    if(!target_number(number))throw std::out_of_range("advanced target number");
    validate_now(now);AdvancedActions a=update(now,false);
    if(number<=3){
        const unsigned index=static_cast<unsigned>(number-1);
        if(!booster_raised_[index]){a.accepted=false;return a;}
        booster_raised_[index]=false;
        const bool complete=!booster_raised_[0]&&!booster_raised_[1]&&!booster_raised_[2];
        if(!complete){a.ordinary_points=500;return a;}
        unsigned reward=0;while(reward<4&&active(features_[reward]))++reward;
        a.ordinary_points=reward==4?10000:5000;
        if(reward<4&&(reward!=0||(mission_state_!=15&&mission_state_!=29)))
            feature_enable(static_cast<AdvancedFeature>(reward));
        const unsigned sounds[5]={47,45,46,48,48};
        if(reward!=0||(mission_state_!=15&&mission_state_!=29))a.sound_control=sounds[reward];
        booster_pending_=true;booster_started_=now_;a.rearm_booster=true;return a;
    }
    if(active(targets_[number])){a.accepted=false;return a;}
    on(targets_[number],config_.target_duration);
    if(number==22){
        if(!active(destination_lamp_))flash(destination_lamp_,config_.destination_flash,true);
        destination_advance(true);a.ordinary_points=750;return a;
    }
    const unsigned family=static_cast<unsigned>((number-10)/3),index=static_cast<unsigned>((number-10)%3);
    std::array<Lamp,3>& lamps=family==0?fuel_spots_:family==1?mission_lamps_:hazards_[family-2];
    flash(lamps[index],config_.spot_flash,true);
    a.ordinary_points=family==1?1000:750;
    a.sound_control=family==1?((mission_state_>=2&&mission_state_<=31)?49U:52U):49U;
    if(family==1)mission_mask_|=1U<<index;
    if(family>=2)hazard_masks_[family-2]|=1U<<index;
    if(lamps[0].on&&lamps[1].on&&lamps[2].on){
        for(unsigned i=0;i<3;++i)flash(lamps[i],config_.spot_flash,false);
        if(family==0){refill(11);a.sound_control=25;}
        else if(family>=2){const unsigned side=family-2;gate_set(side,true);a.sound_control=14;}
    }
    return a;
}
AdvancedActions AdvancedTableRules::rollover(std::size_t number,runtime::Tick now){
    if(!((number>=4&&number<=9)||(number>=179&&number<=184)))throw std::out_of_range("advanced rollover number");
    validate_now(now);AdvancedActions a=update(now,false);
    if(number>=179){
        const unsigned index=static_cast<unsigned>(number-179);const int threshold=static_cast<int>(index*2+1);
        if(fuel_index_<=threshold)refill(threshold);else{on(fuel_pulses_[index],config_.fuel_pulse);fuel_flash_cancelled_[index]=true;}
        a.ordinary_points=500;return a;
    }
    if(number==9){on(return_lamps_[0]);on(return_lamps_[1]);return a;}
    if(number==4||number==8){
        a.ordinary_points=20000;
        if(active(extra_)){a.extra_ball=true;off(extra_);}
        const unsigned side=number==4?0:1;
        if(gates_open_[side]){flash(gate_lamps_[side],0,true);flash(gate_secondary_[side],0,false);}
    }else if(number==5){
        if(active(bonus_lane_)){a.special_points=bonus_pool_;a.bonus_collect=true;off(bonus_lane_);a.sound_control=50;}
        else{a.ordinary_points=10000;a.sound_control=25;}
        refill(11);
    }else{
        const unsigned side=number==6?0:1;a.ordinary_points=active(return_lamps_[side])?25000:5000;
        if(active(return_lamps_[side])){
            // Literal source behavior: cancelling the indicator timeout does
            // not send BonusLightControl's expiry, so its pool flag survives.
            off(features_[2]);off(return_lamps_[side]);
        }
    }
    return a;
}
AdvancedActions AdvancedTableRules::skill_gate(runtime::Tick now){
    validate_now(now);AdvancedActions a=update(now,false);
    if(skill_armed_){
        skill_on_.fill(false);skill_on_[0]=true;skill_flashing_=false;skill_flash_mask_.fill(false);
        refill(11);flash(hyper_reflex_,config_.reflex_window,false);
    }
    return a;
}
AdvancedActions AdvancedTableRules::skill_tripwire(std::size_t number,runtime::Tick now){
    if(number<2||number>6)throw std::out_of_range("skill tripwire number");
    validate_now(now);AdvancedActions a=update(now,false);
    if(skill_armed_){skill_on_[number-1]=true;skill_flash_mask_[number-1]=false;}return a;
}
AdvancedActions AdvancedTableRules::skill_award(runtime::Tick now){
    validate_now(now);AdvancedActions a=update(now,false);unsigned count=0;
    for(unsigned i=0;i<6;++i)if(skill_on_[i])++count;
    if(count){a.ordinary_points=config_.skill_awards[count-1];skill_flashing_=true;skill_started_=now_;skill_flash_mask_=skill_on_;}
    return a;
}
AdvancedActions AdvancedTableRules::skill_clear(runtime::Tick now){
    validate_now(now);AdvancedActions a=update(now,false);skill_armed_=false;skill_on_.fill(false);
    skill_flash_mask_.fill(false);skill_flashing_=false;return a;
}
AdvancedActions AdvancedTableRules::flag_hit(std::size_t index,bool completed_turn,runtime::Tick now){
    if(index>=2)throw std::out_of_range("flag spinner index");
    validate_now(now);AdvancedActions a=update(now,false);
    a.ordinary_points=active(flag_lamps_[1])?2500:500;a.flag_hits=1U<<index;
    if(completed_turn)destination_advance(false);return a;
}
AdvancedActions AdvancedTableRules::wormhole(std::size_t index,runtime::Tick now){
    if(index>=3)throw std::out_of_range("wormhole index");
    validate_now(now);AdvancedActions a=update(now,false);
    const unsigned destination=destination_;
    const unsigned outgoing=destination?destination-1:static_cast<unsigned>(index);
    a.ordinary_points=destination?(outgoing==index?5000:7500):2500;a.replay=destination&&outgoing==index;
    a.wormhole_destination=static_cast<int>(outgoing);
    destination_=0;off(destination_lamp_);worm_lamps_.fill(Lamp());worm_arrows_.fill(Lamp());
    worm_frame_=2-outgoing;flash(worm_lamps_[outgoing],config_.spot_flash,false);flash(worm_arrows_[outgoing],config_.spot_flash,false);
    return a;
}
AdvancedActions AdvancedTableRules::hyperspace(runtime::Tick now,bool reflex_active){
    validate_now(now);AdvancedTableRules next=*this;AdvancedActions a=next.update(now,false);
    const unsigned stage=next.hyper_level_;
    const bool reflex=reflex_active||next.active(next.hyper_reflex_);
    const bool combo=next.active(next.hyper_combined_),super=next.active(next.hyper_super_);
    const runtime::Tick delay=super?config_.hyperspace_special_delay:combo?config_.hyperspace_combined_delay:
        reflex?config_.hyperspace_reflex_delay:config_.hyperspace_delays[stage];
    if(zero(delay))throw std::logic_error("hyperspace selected sound duration unresolved");
    const unsigned stage_sounds[5]={35,36,35,38,39};
    a.sound_control=super?41U:combo?40U:reflex?21U:stage_sounds[stage];
    if(super&&before((std::numeric_limits<runtime::Tick>::max)()-delay,config_.reflex_window))
        throw std::overflow_error("hyperspace reflex duration overflow");
    if(next.hyper_level_<4){next.flash(next.hyper_lamps_[next.hyper_level_],config_.hyper_flash,true);++next.hyper_level_;}
    next.hyper_started_=now;next.hyper_decay_=true;
    if(stage==1){a.special_points=next.jackpot_pool_;next.jackpot_pool_=20000;}
    else a.ordinary_points=config_.hyperspace_awards[stage];
    if(stage==2){next.feature_enable(AdvancedFeature::Blocker);a.enable_blocker=true;}
    if(stage==3)next.feature_enable(AdvancedFeature::ExtraBall);
    if(stage==4){next.hyper_lamps_.fill(Lamp());next.hyper_level_=0;next.hyper_decay_=false;a.enable_gravity_well=true;}
    if(a.ordinary_points){next.record_ordinary(a.ordinary_points);a.ordinary_recorded=true;}
    if(reflex)a.special_points+=config_.reflex_points;
    if(super){
        next.off(next.hyper_super_);a.set_multiplier_max=a.set_medals_max=true;
        for(unsigned i=0;i<4;++i)next.feature_enable(static_cast<AdvancedFeature>(i));
        next.feature_enable(AdvancedFeature::ReturnLanes);next.feature_enable(AdvancedFeature::ExtraBall);
        next.feature_enable(AdvancedFeature::Blocker);a.enable_blocker=a.enable_gravity_well=true;
        next.bonus_pool_=(std::max)(next.bonus_pool_,100000U);next.jackpot_pool_=(std::max)(next.jackpot_pool_,100000U);
    }
    next.flash(next.hyper_reflex_,super?delay+config_.reflex_window:config_.reflex_window,false);
    a.hyperspace_delay=delay;*this=next;return a;
}
AdvancedSnapshot AdvancedTableRules::snapshot() const{
    AdvancedSnapshot s;s.target_enabled.fill(false);s.target_visible.fill(false);s.target_fired.fill(false);s.target_remaining.fill(0);
    for(unsigned n=1;n<23;++n)if(target_number(n)){
        s.target_enabled[n]=n<=3?booster_raised_[n-1]:!active(targets_[n]);
        s.target_visible[n]=n<=3?booster_raised_[n-1]:true;s.target_fired[n]=n>=10&&active(targets_[n]);
        if(n<=3&&booster_pending_)s.target_remaining[n]=left(now_,booster_started_,config_.booster_rearm);
        else if(n>=10&&targets_[n].timed)s.target_remaining[n]=left(now_,targets_[n].started,targets_[n].duration);
    }
    const runtime::Tick period=config_.flash_off+config_.flash_on;
    for(unsigned i=0;i<6;++i){
        bool lamp=fuel_full_||(!fuel_empty_&&i<=static_cast<unsigned>(fuel_index_/2));
        if(!fuel_full_&&!fuel_empty_&&fuel_countdown_&&(fuel_index_&1)==0&&!fuel_flash_cancelled_[i]&&i==static_cast<unsigned>(fuel_index_/2))
            lamp=!before((now_-fuel_started_)%period,config_.flash_off);
        s.fuel_lamps[i]=lamp&&!active(fuel_pulses_[i]);
        const bool skill_phase=!skill_flashing_||!skill_flash_mask_[i]||
            !before((now_-skill_started_)%period,config_.flash_off);
        s.skill_lamps[i]=skill_on_[i]&&skill_phase;
    }
    for(unsigned i=0;i<3;++i){
        s.fuel_spots[i]=visible(fuel_spots_[i]);s.mission_lamps[i]=visible(mission_lamps_[i]);
        s.wormhole_lamps[i]=visible(worm_lamps_[i]);s.wormhole_arrows[i]=visible(worm_arrows_[i]);
    }
    for(unsigned b=0;b<2;++b){
        for(unsigned i=0;i<3;++i)s.hazard_lamps[b][i]=visible(hazards_[b][i]);
        s.return_lamps[b]=visible(return_lamps_[b]);
    }
    for(unsigned i=0;i<4;++i)s.hyperspace_lamps[i]=visible(hyper_lamps_[i]);
    s.gates_open=gates_open_;s.mission_mask=mission_mask_;s.hazard_masks=hazard_masks_;s.fuel_index=fuel_index_;
    s.fuel_countdown=fuel_countdown_;s.fuel_empty=fuel_empty_;s.skill_armed=skill_armed_;
    s.flags_active=active(flag_lamps_[1]);s.jackpot_active=jackpot_accum_;s.bonus_active=bonus_accum_;s.bonus_hold=active(features_[3]);
    s.extra_ball_lit=active(extra_);s.blocker_on=active(blocker_);s.booster_lit=active(features_[0]);
    s.bonus_pool=bonus_pool_;s.jackpot_pool=jackpot_pool_;s.wormhole_destination=destination_;s.hyperspace_level=hyper_level_;s.wormhole_frame=worm_frame_;
    return s;
}
AdvancedLightState AdvancedTableRules::light(std::uint32_t number) const{
    const AdvancedSnapshot s=snapshot();AdvancedLightState result={false,0};
    if(number>=70&&number<=72)result.visible=s.fuel_spots[number-70];
    else if(number>=101&&number<=103)result.visible=s.mission_lamps[number-101];
    else if(number>=104&&number<=109)result.visible=s.hazard_lamps[(number-104)/3][(number-104)%3];
    else if(number>=21&&number<=24)result.visible=s.hyperspace_lamps[number-21];
    else if(number>=5&&number<=7)result.visible=s.wormhole_lamps[number-5];
    else if(number==4||number==2||number==3){const unsigned i=number==4?0:number==2?1:2;result.visible=s.wormhole_arrows[i];result.frame=worm_frame_;}
    else if(number==19||number==20)result.visible=visible(flag_lamps_[number-19]);
    else if(number==61)result.visible=visible(features_[0]);
    else if(number==60)result.visible=visible(features_[1]);
    else if(number==59)result.visible=visible(features_[2]);
    else if(number==58)result.visible=visible(features_[3]);
    else if(number==17||number==18)result.visible=visible(extra_);
    else if(number==27||number==28)result.visible=visible(return_lamps_[number-27]);
    else if(number==16)result.visible=visible(bonus_lane_);
    else if(number==110)result.visible=visible(destination_lamp_);
    else if(number==25)result.visible=visible(hyper_reflex_);
    else if(number==26)result.visible=visible(hyper_combined_);
    else if(number==130)result.visible=visible(hyper_super_);
    else if(number==30)result.visible=visible(gate_lamps_[0]);
    else if(number==196)result.visible=visible(gate_secondary_[0]);
    else if(number==29)result.visible=visible(gate_lamps_[1]);
    else if(number==195)result.visible=visible(gate_secondary_[1]);
    else if(number==1)result.visible=visible(blocker_);
    else{
        const std::uint32_t skill[6]={67,68,69,131,132,133};
        for(unsigned i=0;i<6;++i)if(number==skill[i])result.visible=s.skill_lamps[i];
    }
    return result;
}
runtime::Tick AdvancedTableRules::until_boundary() const{bool pending=false;return next_boundary(pending);}
runtime::Tick AdvancedTableRules::time() const{return now_;}
} }






