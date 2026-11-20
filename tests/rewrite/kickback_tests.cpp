#include "kickback.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace pinball::rules;using pinball::runtime::Tick;
const Tick second=1000000000LL;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class E,class F>void throws(F f,const char* m){try{f();}catch(const E&){return;}throw std::runtime_error(m);}
KickbackConfig config(){return KickbackConfig(700000000,100000000,1e9f,0);}
void delayed_arm_kick_and_cleanup(){
    Kickback k(config());check(k.snapshot().visible&&k.snapshot().frame==0&&!k.snapshot().kick_active,"initial kickback");
    KickbackEvents e=k.collision(false,0);
    check(e.started&&e.contact_processed&&!e.kicked&&k.snapshot().threshold==1e9f&&
          k.until_boundary()==700000000,"any first collision must wait700ms");
    check(!k.collision(false,300000000).started&&k.until_boundary()==400000000,"waiting collision restarted delay");
    k.advance(699999999);check(!k.snapshot().armed,"armed early");
    e=k.advance(700000000);check(e.pulses==1&&k.snapshot().armed&&k.snapshot().frame==1&&k.snapshot().threshold==0,"arm pulse");
    e=k.advance(800000000);check(e.pulses==1&&!e.gate_close&&k.snapshot().kick_active,"ready pulse loop");
    e=k.collision(true,850000000);check(e.kicked&&!k.snapshot().kick_active&&k.snapshot().threshold==1e9f&&
                                      k.snapshot().frame==1,"strict kick must retain art until callback");
    e=k.advance(900000000);check(e.gate_close&&e.pulses==0&&k.snapshot().frame==0&&k.snapshot().pending_callbacks==0,
                               "next queued callback must close gate and restore idle");
}
void equal_time_and_queued_retrigger(){
    Kickback k(config());k.collision(false,0);
    check(!k.collision(false,700000000).kicked&&!k.snapshot().armed,"contact beat equal arm expiry");
    k.advance(700000000);
    check(k.collision(true,800000000).kicked,"equal pulse contact did not use already armed state");
    const KickbackEvents cleanup=k.advance(800000000);
    check(cleanup.gate_close&&cleanup.pulses==0,"equal pulse ran before strict collision");

    Kickback queued(config());queued.collision(false,0);queued.advance(700000000);
    queued.collision(true,750000000);queued.collision(false,760000000);
    check(queued.snapshot().pending_callbacks==2&&!queued.snapshot().armed,"retrigger cancelled older pulse");
    const KickbackEvents early=queued.advance(800000000);
    check(early.pulses==1&&queued.snapshot().armed&&queued.snapshot().pending_callbacks==2,
          "old100ms callback did not arm new700ms attempt early");
    queued.collision(true,810000000);
    const KickbackEvents both=queued.advance(1460000000LL);
    check(both.callbacks==2&&both.gate_close&&both.pulses==0&&queued.snapshot().pending_callbacks==0,
          "shared inactive flag did not drain both queued callbacks");
}
void reset_tilt_and_budget(){
    Kickback idle(config());idle.reset();check(idle.snapshot().frame==0,"reset without pending timer hid idle image");
    Kickback k(config());k.collision(false,0);k.advance(700000000);k.collision(true,750000000);k.collision(false,760000000);
    k.reset(0);check(!k.snapshot().visible&&k.snapshot().pending_callbacks==0&&!k.snapshot().kick_active,"safe reset did not cancel every callback");
    check(!k.advance(2*second).gate_close,"old callback survived reset");
    k.collision(false,2*second);k.set_tilted(true,2100000000LL);
    check(k.snapshot().tilted&&!k.snapshot().visible&&k.snapshot().threshold==0&&
          k.snapshot().pending_callbacks==0,"tilt reset/physical threshold");
    check(!k.collision(true,2200000000LL).started&&!k.snapshot().kick_active,"tilted contact armed controller");
    k.set_tilted(false,2300000000LL);
    check(k.snapshot().threshold==1e9f&&k.collision(false,2300000000LL).started,"untilted controller did not wait anew");

    Kickback budget(config());budget.collision(false,0);
    KickbackEvents e=budget.advance(10*second,2);
    check(e.callbacks==2&&e.pulses==2&&e.due_remaining&&budget.until_boundary()==0,"explicit timer budget lost overdue work");
    e=budget.collision(true,10*second,0);
    check(e.due_remaining&&!e.contact_processed&&!e.kicked,"contact processed before earlier callbacks drained");
    e=budget.advance(10*second,256);check(!e.due_remaining&&budget.snapshot().armed,"bounded continuation failed");
    check(budget.collision(true,10*second).kicked,"contact retry after catchup failed");
}
void wide_and_invalid(){
    const Tick wide=4294967296LL,maximum=(std::numeric_limits<Tick>::max)();
    Kickback k(KickbackConfig(wide,wide,1e9f,0),2147483648LL);k.collision(false,k.time());
    k.advance(2147483648LL+wide-1);check(!k.snapshot().armed&&k.until_boundary()==1,"wide delay expired early");
    k.advance(2147483648LL+wide);check(k.snapshot().armed,"wide arm callback failed");
    Kickback end(config(),maximum-10);end.collision(false,maximum-9);end.advance(maximum);
    check(!end.snapshot().armed&&end.until_boundary()==700000000-9,"future-beyond-max callback wrapped");
    throws<std::invalid_argument>([&]{end.advance(maximum-1);},"backward clock accepted");
    throws<std::invalid_argument>([&]{end.advance(-1);},"negative clock accepted");
    throws<std::invalid_argument>([]{KickbackConfig c(0,1,1,0);},"zero arm delay accepted");
    throws<std::invalid_argument>([]{KickbackConfig c(1,-1,1,0);},"negative pulse accepted");
    throws<std::invalid_argument>([]{KickbackConfig c(1,1,(std::numeric_limits<float>::infinity)(),0);},"infinite threshold accepted");
    throws<std::invalid_argument>([]{KickbackConfig c(1,1,0,1);},"inverted thresholds accepted");
}
}
int main(){try{delayed_arm_kick_and_cleanup();equal_time_and_queued_retrigger();reset_tilt_and_budget();wide_and_invalid();
    std::cout<<"kickback_tests PASS:delayed arm,queued early rearm,kick cleanup,safe reset,budgets,wide clocks\n";return 0;
}catch(const std::exception& e){std::cerr<<"kickback_tests FAIL: "<<e.what()<<'\n';return 1;}}
