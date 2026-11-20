#include "tilt_counter.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace pinball::rules;using pinball::runtime::Tick;
const Tick second=1000000000LL;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void near(double a,double b,const char* m){check(std::fabs(a-b)<1e-11,m);}
template<class E,class F>void throws(F f,const char* m){try{f();}catch(const E&){return;}throw std::runtime_error(m);}
void strict_thresholds_and_timeout(){
    TiltCounter c;
    TiltEvents e=c.advance(125000000,true);near(c.snapshot().pressure,.5,"active rate4");
    check(!e.warning&&!e.tilted,"warning at exactly.5");
    e=c.advance(250000000,true);near(c.snapshot().pressure,1,"active quarter-second pressure");
    check(e.warning&&!e.tilted,"tilt at exactly1");
    e=c.advance(250000001,true);check(e.tilted&&e.warning&&c.snapshot().tilt_lock,"strict tilt crossing");
    const Tick tilted=c.time();
    e=c.advance(tilted+30*second-1,false);check(!e.force_drain&&c.snapshot().tilt_lock,"timeout fired early");
    e=c.advance(tilted+30*second,false);
    check(e.force_drain&&c.snapshot().tilt_lock&&!c.snapshot().timeout_pending,"30s must force drain, not clear tilt");
    check(!c.advance(c.time()+second,false).force_drain,"timeout repeated");
    c.clear_lock(c.time());check(!c.snapshot().tilt_lock,"drain cleanup did not clear lock");
}
void decay_clear_feed_and_drain_gate(){
    TiltCounter c;c.advance(200000000,true);near(c.snapshot().pressure,.8,"active integration");
    c.advance(300000000,false);near(c.snapshot().pressure,.7,"passive decay1");
    c.advance(second,false);near(c.snapshot().pressure,0,"passive clamp0");
    c.tilt_no_more(second);near(c.snapshot().pressure,-2,"feed clear must set-2");
    c.advance(second,false);near(c.snapshot().pressure,-2,"same-clock call integrated pressure");
    c.advance(second+1,false);near(c.snapshot().pressure,0,"inactive frame did not clamp negative clear");
    c.tilt_no_more(c.time());const Tick now=c.time();
    c.advance(now+250000000,true);near(c.snapshot().pressure,-1,"active negative clear debt");
    c.advance(now+500000000,true);near(c.snapshot().pressure,0,"negative clear debt second interval");
    check(!c.advance(now+750000000,true).tilted,"postclear exactly1 tilted");
    check(c.advance(now+750000001,true).tilted,"postclear strict crossing missing");
    c.clear_lock(c.time());
    const TiltEvents in_drain=c.advance(c.time()+second,true,false);
    check(in_drain.warning&&!in_drain.tilted&&!c.snapshot().tilt_lock,"BallInDrain tilt guard ignored");
    c.tilt_no_more(c.time());check(c.snapshot().pressure==-2&&!c.snapshot().timeout_pending,"feed did not cancel old timeout");
}
void subdivision_wide_clocks_and_validation(){
    TiltCounter whole,split;
    whole.advance(200000000,true);
    for(unsigned i=1;i<=20;++i)split.advance(static_cast<Tick>(i)*10000000,true);
    near(whole.snapshot().pressure,split.snapshot().pressure,"constant-input subdivision drift");
    const Tick maximum=(std::numeric_limits<Tick>::max)(),epoch=4294967296LL;
    TiltCounter wide(TiltCounterConfig(),epoch);
    check(wide.advance(epoch+250000001,true).tilted,"positive high-word time rejected");
    TiltCounter end(TiltCounterConfig(),maximum-300000000);
    check(end.advance(maximum,true).tilted&&end.until_boundary()==30*second,"near-max timeout overflow");
    throws<std::invalid_argument>([&]{end.advance(maximum-1,false);},"backward clock accepted");
    throws<std::invalid_argument>([&]{end.advance(-1,false);},"negative clock accepted");
    throws<std::invalid_argument>([]{TiltCounter c(TiltCounterConfig(),-4294967296LL);},"negative high-word epoch accepted");
    throws<std::invalid_argument>([]{TiltCounterConfig c(0);},"zero rise rate accepted");
    throws<std::invalid_argument>([]{TiltCounterConfig c(4,1,.5,.5);},"inverted strict thresholds accepted");
    throws<std::invalid_argument>([]{TiltCounterConfig c(4,1,.5,1,0);},"zero timeout accepted");
    const TiltCounterConfig extreme((std::numeric_limits<double>::max)());
    TiltCounter overflow(extreme);
    throws<std::overflow_error>([&]{overflow.advance(2*second,true);},"pressure overflow accepted");
    check(overflow.time()==0&&overflow.snapshot().pressure==0,"overflow partially committed counter");
    wide.reset();check(wide.time()==0&&wide.snapshot().pressure==0&&!wide.snapshot().tilt_lock,"new-game counter reset");
}
}
int main(){try{strict_thresholds_and_timeout();decay_clear_feed_and_drain_gate();subdivision_wide_clocks_and_validation();
    std::cout<<"tilt_counter_tests PASS:strict thresholds,decay,30s force-drain,feed reset,wide clocks\n";return 0;
}catch(const std::exception& e){std::cerr<<"tilt_counter_tests FAIL: "<<e.what()<<'\n';return 1;}}

