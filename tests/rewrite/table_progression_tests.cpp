#include "table_progression.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace pinball::rules;using pinball::runtime::Tick;
const Tick second=1000000000LL;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class E,class F>void throws(F f,const char* m){try{f();}catch(const E&){return;}throw std::runtime_error(m);}
std::array<LaneProgressionConfig,2> config(){
    return {{LaneProgressionConfig(5*second,60*second,100000000,100000000),
             LaneProgressionConfig(5*second,60*second,100000000,100000000)}};
}
LaneHit collect(TableProgression& p,unsigned bank,Tick now){
    p.hit(bank,0,now);p.hit(bank,1,now);return p.hit(bank,2,now);
}
void lane_toggle_flash_and_rotation(){
    TableProgression p(config());p.hit(0,0,0);
    check(p.snapshot().groups[0].logical_on[0],"lane did not light");
    p.hit(0,0,0);check(!p.snapshot().groups[0].logical_on[0],"lit lane did not toggle off");
    p.hit(0,0,0);p.rotate(-1,0);
    check(p.snapshot().groups[0].logical_on[2]&&!p.snapshot().groups[0].logical_on[0],"backward lane change");
    p.rotate(1,0);check(p.snapshot().groups[0].logical_on[0],"forward lane change inverse");
    p.hit(0,1,0);const LaneHit hit=p.hit(0,2,0);
    check(hit.completed&&hit.level_changed_mask==1&&p.snapshot().groups[0].level==1,"first lane collection upgrade");
    for(unsigned i=0;i<3;++i)
        check(!p.snapshot().groups[0].logical_on[i]&&!p.snapshot().groups[0].lamp_visible[i],"collection did not start OFF/logical off");
    check(!p.hit(0,0,1).completed&&!p.snapshot().groups[0].logical_on[0],"flashing hit changed collection");
    p.hit(1,0,1);p.rotate(-1,2);
    check(p.snapshot().groups[1].logical_on[2]&&!p.snapshot().groups[0].logical_on[2],
          "lane change did not skip flashing group independently");
    p.advance(100000000);for(unsigned i=0;i<3;++i)check(p.snapshot().groups[0].lamp_visible[i],"100ms ON phase");
    p.advance(200000000);for(unsigned i=0;i<3;++i)check(!p.snapshot().groups[0].lamp_visible[i],"second OFF phase");
    p.hit(0,0,5*second);check(!p.snapshot().groups[0].logical_on[0],"equal-time hit beat flash expiry");
    p.advance(5*second);check(!p.snapshot().groups[0].flashing,"5s flash did not expire");
    p.hit(0,0,5*second);check(p.snapshot().groups[0].logical_on[0],"lane did not rearm after flash");
}
void levels_decay_and_deadline_priority(){
    TableProgression p(config());Tick now=0;
    for(unsigned level=1;level<=4;++level){
        const LaneHit hit=collect(p,0,now);
        check(hit.completed&&p.snapshot().groups[0].level==(level<4?level:3),"bumper level cap");
        check(hit.level_changed_mask==(level<4?1U:0U),"unchanged maximum level requested Fire");
        now+=5*second;p.advance(now);
    }
    p.advance(75*second-1);check(p.snapshot().groups[0].level==3,"max collection did not refresh60s timer");
    check(p.advance(75*second)==1&&p.snapshot().groups[0].level==2,"first decay did not report level change");
    check(p.advance(135*second)==1&&p.snapshot().groups[0].level==1,"second60s decay");
    check(p.advance(195*second)==1&&p.snapshot().groups[0].level==0,"third60s decay");
    check(p.until_boundary()==(std::numeric_limits<Tick>::max)(),"unobservable level-zero no-op still scheduled");

    TableProgression equal(config());collect(equal,0,0);equal.advance(5*second);
    equal.hit(0,0,59*second);equal.hit(0,1,59*second);
    const LaneHit completion=equal.hit(0,2,60*second);
    check(completion.completed&&equal.snapshot().groups[0].level==2,"completion lost equal-time decay priority");
    equal.advance(60*second);check(equal.snapshot().groups[0].level==2,"cancelled decay still fired");
    equal.advance(120*second);check(equal.snapshot().groups[0].level==1,"refreshed decay deadline");
}
void independent_groups_reset_and_validation(){
    TableProgression p(config());collect(p,0,0);collect(p,1,second);
    p.advance(5*second);
    check(!p.snapshot().groups[0].flashing&&p.snapshot().groups[1].flashing,"groups share flash epoch");
    p.advance(60*second);
    check(p.snapshot().groups[0].level==0&&p.snapshot().groups[1].level==1,"groups share decay epoch");
    p.hit(0,0,60*second);
    check(p.set_level(1,0,60*second)==2&&p.snapshot().groups[0].logical_on[0],
          "mission level reset cleared lamps or missed Fire signal");
    check(p.set_level(1,0,60*second)==0,"unchanged level reset requested Fire");
    p.ordinary_loss(60*second);
    check(p.snapshot().groups[1].level==0&&p.until_boundary()==(std::numeric_limits<Tick>::max)(),"ordinary loss retained progression");
    p.hit(1,0,60*second);const ProgressionSnapshot saved=p.snapshot();p.advance(60*second);
    check(p.snapshot().groups[1].logical_on==saved.groups[1].logical_on,"same-clock/pause changed saved progress");
    throws<std::out_of_range>([&]{p.hit(2,0,p.time());},"invalid bank accepted");
    throws<std::out_of_range>([&]{p.hit(0,3,p.time());},"invalid lane accepted");
    throws<std::invalid_argument>([&]{p.rotate(0,p.time());},"invalid direction accepted");
    throws<std::invalid_argument>([&]{p.advance(p.time()-1);},"backward clock accepted");
    throws<std::invalid_argument>([&]{p.advance(-1);},"negative clock accepted");
    throws<std::invalid_argument>([]{LaneProgressionConfig c(0,1,1,1);},"zero flash accepted");
    const Tick maximum=(std::numeric_limits<Tick>::max)();
    throws<std::invalid_argument>([&]{LaneProgressionConfig c(1,1,maximum,1);},"flash period overflow accepted");
    p.reset();check(p.time()==0&&!p.snapshot().groups[1].logical_on[0],"new game retained progress");
}
void wide_clocks(){
    const Tick wide=4294967296LL,maximum=(std::numeric_limits<Tick>::max)();
    const std::array<LaneProgressionConfig,2> c={{LaneProgressionConfig(wide,wide,1,1),LaneProgressionConfig(wide,wide,1,1)}};
    TableProgression p(c,2147483648LL);collect(p,0,p.time());
    p.advance(2147483648LL+wide-1);check(p.snapshot().groups[0].level==1&&p.until_boundary()==1,"wide timer expired early");
    p.advance(2147483648LL+wide);check(p.snapshot().groups[0].level==0&&!p.snapshot().groups[0].flashing,"wide equal timers failed");
    TableProgression end(config(),maximum-10);collect(end,1,maximum-9);end.advance(maximum);
    check(end.snapshot().groups[1].level==1&&end.until_boundary()==5*second-9,"near-max age overflow");
    throws<std::invalid_argument>([&]{TableProgression invalid(config(),-4294967296LL);},"negative high-word epoch accepted");
    TableProgression jump(config());Tick now=0;
    for(unsigned level=0;level<3;++level){collect(jump,0,now);collect(jump,1,now);now+=5*second;jump.advance(now);}
    check(jump.advance(maximum)==3&&jump.snapshot().groups[0].level==0&&jump.snapshot().groups[1].level==0,
          "large jump did not bound observable decays");
}
}
int main(){
    try{lane_toggle_flash_and_rotation();levels_decay_and_deadline_priority();independent_groups_reset_and_validation();wide_clocks();
        std::cout<<"table_progression_tests PASS:lane toggles/rotation,5s flash,bumper upgrades/60s decay,wide clocks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"table_progression_tests FAIL: "<<e.what()<<'\n';return 1;}
}

