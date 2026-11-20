#include "target_bank.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pinball::rules;
using pinball::runtime::Tick;
const Tick second=1000000000LL;
const Tick raise_delay=250000000LL;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Exception,class Operation>
void throws(Operation op,const char* message){
    try{op();}catch(const Exception&){return;}throw std::runtime_error(message);
}
TargetBankConfig config(){return TargetBankConfig(raise_delay,30*second,2*second);}
TargetHit complete(TargetBank& bank,Tick now){
    check(bank.hit(0,now).accepted,"first raised target rejected");
    check(bank.hit(1,now).accepted,"second raised target rejected");
    return bank.hit(2,now);
}
bool all_raised(const TargetBankSnapshot& s){
    return s.raised[0]&&s.raised[1]&&s.raised[2];
}
void unique_hits_and_rearm(){
    TargetBank bank(config());const TargetBankSnapshot initial=bank.snapshot();
    check(all_raised(initial)&&initial.multiplier==1&&initial.tier==0&&!initial.raise_pending,
          "initial bank not raised at x1");
    for(unsigned i=0;i<4;++i)
        check(initial.lamp_modes[i]==BankLampMode::Off&&!initial.lamp_visible[i],"initial lamp lit");
    const TargetHit first=bank.hit(1,0);
    check(first.accepted&&!first.completed&&first.base_points==500&&first.multiplier_before==1&&
          first.points==500&&!bank.snapshot().raised[1],"first target award/drop");
    check(!bank.hit(1,0).accepted&&bank.hit(1,0).points==0,"duplicate down target scored");
    check(bank.hit(0,1).points==500,"second unique award");
    const TargetHit third=bank.hit(2,2);
    check(third.completed&&third.base_points==1500&&third.multiplier_before==1&&third.points==1500&&
          bank.snapshot().tier==1&&bank.snapshot().multiplier==2,"completion used new multiplier");
    check(bank.snapshot().raise_pending&&!all_raised(bank.snapshot()),"completion immediately raised bank");
    bank.advance(raise_delay+1);
    check(!all_raised(bank.snapshot()),"bank raised before250ms");
    check(!bank.hit(0,raise_delay+2).accepted,"equal-time hit beat pending rearm timer");
    bank.advance(raise_delay+2);
    check(all_raised(bank.snapshot())&&!bank.snapshot().raise_pending,"250ms rearm did not raise all");
    check(bank.hit(0,raise_delay+2).points==1000,"rearmed target did not use x2");
}
void tier_awards_maximum_and_decay(){
    TargetBank bank(config());const std::uint32_t old_multipliers[5]={1,2,3,5,10};
    Tick now=0;
    for(unsigned level=0;level<5;++level){
        const TargetHit hit=complete(bank,now);
        check(hit.completed&&hit.multiplier_before==old_multipliers[level]&&
              hit.points==1500*old_multipliers[level],"completion award/tier progression");
        check(bank.snapshot().tier==(level<4?level+1:4),"tier did not saturate at4");
        now+=raise_delay;bank.advance(now);
    }
    bank.advance(5*second);
    for(unsigned i=0;i<4;++i)check(bank.snapshot().lamp_modes[i]==BankLampMode::Steady,"lamp did not settle");
    const TargetHit maximum=complete(bank,10*second);
    check(maximum.points==15000&&bank.snapshot().tier==4,"maximum completion invented extra award");
    for(unsigned i=0;i<4;++i)check(bank.snapshot().lamp_modes[i]==BankLampMode::Steady,
                                 "maximum completion restarted an existing lamp flash");
    bank.advance(40*second-1);check(bank.snapshot().tier==4,"maximum completion failed to restart decay");
    bank.advance(40*second);check(bank.snapshot().tier==3&&bank.snapshot().multiplier==5,
                                 "first30s decay tier");
    check(bank.snapshot().lamp_modes[3]==BankLampMode::Off,"decay did not extinguish last lamp");
    bank.advance(70*second);check(bank.snapshot().tier==2&&bank.snapshot().multiplier==3,"second decay tier");
    bank.advance(100*second);check(bank.snapshot().tier==1&&bank.snapshot().multiplier==2,"third decay tier");
    bank.advance(130*second);
    check(bank.snapshot().tier==0&&bank.snapshot().multiplier==1&&
          bank.until_boundary()==(std::numeric_limits<Tick>::max)(),"decay failed to stop at x1");
    for(unsigned i=0;i<4;++i)check(!bank.snapshot().lamp_visible[i],"decayed lamp still visible");
}
void independent_lamp_clocks(){
    TargetBank bank(config());complete(bank,0);
    check(bank.snapshot().lamp_modes[0]==BankLampMode::Flashing&&!bank.snapshot().lamp_visible[0],
          "new lamp did not start OFF");
    bank.advance(99999999);check(!bank.snapshot().lamp_visible[0],"OFF flash phase too short");
    bank.advance(100000000);check(bank.snapshot().lamp_visible[0],"100ms ON flash phase missing");
    bank.advance(199999999);check(bank.snapshot().lamp_visible[0],"ON flash phase too short");
    bank.advance(200000000);check(!bank.snapshot().lamp_visible[0],"second OFF phase missing");
    bank.advance(300000000);complete(bank,300000000);
    check(bank.snapshot().lamp_visible[0]&&!bank.snapshot().lamp_visible[1],
          "later completion restarted older lamp clock");
    bank.advance(2*second);
    check(bank.snapshot().lamp_modes[0]==BankLampMode::Steady&&bank.snapshot().lamp_visible[0]&&
          bank.snapshot().lamp_modes[1]==BankLampMode::Flashing,"individual flash duration lost");
    bank.advance(2300000000LL);
    check(bank.snapshot().lamp_modes[1]==BankLampMode::Steady&&bank.snapshot().lamp_visible[1],
          "newer lamp did not settle at its own2s deadline");
}
void deadline_priority_and_partial_bank(){
    TargetBank bank(config());complete(bank,0);bank.advance(raise_delay);
    bank.hit(0,29*second);bank.hit(1,29*second);
    const TargetHit at_deadline=bank.hit(2,30*second);
    check(at_deadline.points==3000&&at_deadline.multiplier_before==2&&bank.snapshot().tier==2,
          "equal-time completion lost collision-before-decay priority");
    bank.advance(30*second);check(bank.snapshot().tier==2,"cancelled equal-time decay still ran");
    bank.advance(60*second);check(bank.snapshot().tier==1,"completion did not restart notify deadline");

    TargetBank partial(config());complete(partial,0);partial.advance(raise_delay);
    partial.hit(0,29*second);
    partial.advance(30*second);
    check(partial.snapshot().tier==0&&!partial.snapshot().raised[0]&&partial.snapshot().raised[1],
          "partial hit restarted decay or decay raised partial bank");
    check(partial.hit(1,30*second).points==500,"decayed partial bank retained old scoring multiplier");
}
void reset_saved_and_pause_conventions(){
    TargetBank bank(config());complete(bank,0);bank.advance(raise_delay);
    bank.hit(0,second);const TargetBankSnapshot paused=bank.snapshot();
    // The caller freezes simulation time during pause; repeated same-time
    // updates and a saved loss must not reset partial progress.
    bank.advance(second);const TargetBankSnapshot saved=bank.snapshot();
    check(saved.raised==paused.raised&&saved.tier==paused.tier,"same-clock/saved state changed");
    bank.ordinary_loss(second);
    check(all_raised(bank.snapshot())&&bank.snapshot().tier==0&&
          bank.until_boundary()==(std::numeric_limits<Tick>::max)(),"ordinary loss retained bank/timers");
    complete(bank,second);bank.reset(0);
    check(bank.time()==0&&all_raised(bank.snapshot())&&bank.snapshot().multiplier==1,
          "new game retained target/tier/clock");
    bank.advance(100*second);
    check(bank.snapshot().tier==0&&all_raised(bank.snapshot()),"cancelled reset timers returned");
}
void medal_awards_and_extra_credits(){
    const std::array<std::uint32_t,3> points={{10000,50000,0}};
    TargetBank bank(TargetBankConfig(raise_delay,30*second,2*second,100000000,100000000,1500,points,3));
    const std::uint32_t expected[4]={10000,50000,0,0};Tick now=0;
    for(unsigned completion=0;completion<4;++completion){
        check(bank.hit(0,now).points==1500&&bank.hit(1,now).points==1500,"medal incomplete award");
        const TargetHit hit=bank.hit(2,now);
        check(hit.completed&&hit.points==expected[completion]&&hit.extra_ball==(completion>=2)&&
              hit.multiplier_before==1,"medal completion/credit policy");
        check(bank.snapshot().tier==(completion<3?completion+1:3)&&bank.snapshot().multiplier==1&&
              !bank.snapshot().lamp_visible[3]&&bank.snapshot().lamp_modes[3]==BankLampMode::Off,
              "medal invented fourth tier/global multiplier");
        now+=raise_delay;bank.advance(now);
    }
    bank.advance(30*second+3*raise_delay);
    check(bank.snapshot().tier==2,"medal decay did not lower one stage");
    const TargetHit repeated=complete(bank,31*second);
    check(repeated.extra_ball&&repeated.points==0&&bank.snapshot().tier==3,"medal stage3 re-earned credit");
    throws<std::invalid_argument>([&]{TargetBankConfig c(1,1,1,1,1,1500,points,4);},"invalid medal credit stage");
}
void mission_rearm_preserves_progress(){
    TargetBank bank(config());complete(bank,0);bank.advance(raise_delay);
    bank.hit(0,second);bank.request_rearm(second);
    check(!bank.snapshot().raised[0]&&bank.snapshot().raised[1]&&bank.snapshot().raised[2]&&
          bank.snapshot().tier==1,"requested rearm lowered raised targets or changed tier");
    bank.advance(second+raise_delay-1);check(!bank.snapshot().raised[0],"requested rearm was early");
    bank.advance(second+raise_delay);check(all_raised(bank.snapshot())&&bank.snapshot().tier==1,
                                         "requested rearm did not preserve tier");
}
void direct_feature_tiers(){
    TargetBank bank(config());bank.hit(0,0);bank.set_tier(4,0);
    check(!bank.snapshot().raised[0]&&bank.snapshot().multiplier==10,"feature tier reset popup state");
    for(unsigned i=0;i<4;++i)check(bank.snapshot().lamp_modes[i]==BankLampMode::Steady&&bank.snapshot().lamp_visible[i],
                                 "feature max tier must light steadily without new flash");
    bank.advance(30*second);check(bank.snapshot().tier==3,"feature multiplier max did not restart decay");
    const std::array<std::uint32_t,3> points={{10000,50000,0}};
    TargetBank medal(TargetBankConfig(raise_delay,30*second,2*second,100000000,100000000,1500,points,3));
    medal.set_tier(3,0,false);
    check(medal.snapshot().tier==3&&medal.until_boundary()==(std::numeric_limits<Tick>::max)(),
          "medal lights-only max invented notify restart");
    medal.advance(100*second);check(medal.snapshot().tier==3,"medal lights-only max decayed without original notify");
    throws<std::out_of_range>([&]{medal.set_tier(4,medal.time());},"medal fourth tier accepted");
}
void wide_clocks_and_validation(){
    const Tick maximum=(std::numeric_limits<Tick>::max)(),wide=4294967296LL;
    TargetBank high(TargetBankConfig(wide,wide,wide,1,1),2147483648LL);
    complete(high,2147483648LL);
    high.advance(2147483648LL+wide-1);
    check(high.snapshot().tier==1&&!all_raised(high.snapshot())&&high.until_boundary()==1,
          "positive bit31/high-word timer rejected or compared early");
    high.advance(2147483648LL+wide);
    check(high.snapshot().tier==0&&all_raised(high.snapshot()),"wide equal timers did not finish");
    TargetBank near_end(config(),maximum-10);complete(near_end,maximum-9);near_end.advance(maximum);
    check(near_end.snapshot().tier==1&&near_end.until_boundary()==raise_delay-9,
          "near-max deadline arithmetic overflowed");
    throws<std::invalid_argument>([&]{near_end.advance(maximum-1);},"backward clock accepted");
    throws<std::invalid_argument>([&]{near_end.advance(-1);},"negative clock accepted");
    throws<std::invalid_argument>([]{TargetBank bad(config(),-4294967296LL);},"negative high-word clock accepted");
    throws<std::out_of_range>([&]{near_end.hit(3,maximum);},"invalid target accepted");
    check(near_end.time()==maximum&&near_end.snapshot().tier==1,"invalid operation partially changed bank");
    throws<std::invalid_argument>([]{TargetBankConfig c(0,1,1,1,1);},"zero rearm accepted");
    throws<std::invalid_argument>([]{TargetBankConfig c(1,-1,1,1,1);},"negative decay accepted");
    throws<std::invalid_argument>([]{TargetBankConfig c(1,1,1,0,1);},"zero OFF interval accepted");
    throws<std::invalid_argument>([&]{TargetBankConfig c(1,1,1,maximum,1);},"flash period overflow accepted");
    throws<std::invalid_argument>([]{TargetBankConfig c(1,1,1,1,1,429496730U,1500);},
                                  "scaled award overflow accepted");
    TargetBankConfig changed=config();changed.flash_duration=0;
    throws<std::invalid_argument>([&]{TargetBank bad(changed);},"mutated invalid configuration accepted");

    // Huge jumps need no periodic blink callbacks and at most four decays.
    TargetBank jump(config());Tick now=0;
    for(unsigned tier=0;tier<4;++tier){complete(jump,now);now+=raise_delay;jump.advance(now);}
    jump.advance(maximum);
    check(jump.snapshot().tier==0&&all_raised(jump.snapshot())&&
          jump.until_boundary()==maximum,"large advance failed bounded timer exhaustion");
}
}
int main(){
    try{
        unique_hits_and_rearm();tier_awards_maximum_and_decay();independent_lamp_clocks();
        deadline_priority_and_partial_bank();reset_saved_and_pause_conventions();medal_awards_and_extra_credits();mission_rearm_preserves_progress();direct_feature_tiers();wide_clocks_and_validation();
        std::cout<<"target_bank_tests PASS:3unique targets,old-multiplier awards,flash/rearm/decay,wide clocks\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"target_bank_tests FAIL: "<<error.what()<<'\n';return 1;
    }
}



