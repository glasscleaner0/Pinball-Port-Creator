#include "flag_spinner.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
using namespace pinball::rules;using pinball::runtime::Tick;
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
SpinnerConfig config(){return SpinnerConfig(5,50000,20,.649999976f,8);}
void lifecycle(){
 FlagSpinner s(config());check(s.snapshot().frame==0&&s.snapshot().pending==0,"initial spinner frame/timer");
 SpinnerEvents hit=s.hit(1,false,0);check(hit.frames==1&&hit.wraps==0&&s.snapshot().frame==1,"first frame must advance immediately");
 check(std::fabs(s.snapshot().speed-13)<.0001f&&s.until_boundary()==76000000,"DAT decay must precede integer-ms scheduling");
 check(s.advance(75999999).frames==0,"spinner frame before timer");
 check(s.advance(76000000).frames==1&&s.snapshot().frame==2,"spinner equal-time callback");
 SpinnerEvents rest=s.advance(2000000000LL);check(rest.frames==2&&s.snapshot().pending==0,"spinner decay cutoff");
 check(s.snapshot().frame==4&&s.snapshot().speed<5,"spinner final image/speed");
 s.reset();hit=s.hit(0,true,0);check(hit.frames==1&&s.snapshot().frame==7&&s.snapshot().pending==0,"reverse/min-speed immediate frame");
 hit=s.hit(0,false,0);check(hit.frames==1&&hit.wraps==1&&s.snapshot().frame==0,"frame-zero wrap event");
}
void retrigger_and_high_speed(){
 FlagSpinner s(config());s.hit(1,false,0);s.hit(1,true,1000000);
 check(s.snapshot().pending==2&&s.snapshot().frame==0,"retrigger must preserve old timer");
 s.advance(76000000);check(s.snapshot().frame==7,"old callback must use current direction");
 s.reset();SpinnerEvents hit=s.hit(1000000,false,0);check(hit.frames==1&&s.snapshot().speed<=50000,"maximum spin speed clamp");
 SpinnerEvents zero=s.advance(0);check(zero.frames>0&&s.snapshot().pending==1,"zero-ms callbacks should run with bounded decay");
 SpinnerEvents all=s.advance(10000000000LL);check(all.frames>0&&s.snapshot().pending==0,"high-speed spinner did not settle");
 s.reset();check(s.advance(20000000000LL).frames==0,"reset left stale callbacks");
}
void wide_clock_and_validation(){
 const Tick epoch=0x100000000LL+1000000000;FlagSpinner s(config(),epoch);s.hit(1,false,epoch);
 check(s.advance(epoch+75999999).frames==0&&s.advance(epoch+76000000).frames==1,"wide-clock boundary");
 bool rejected=false;try{s.hit(-1,false,epoch+76000000);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"negative speed accepted");
 rejected=false;try{s.advance(epoch);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"backward clock accepted");
 rejected=false;try{SpinnerConfig bad(5,10,20,1,8);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"nondecaying spinner accepted");
}
}
int main(){try{lifecycle();retrigger_and_high_speed();wide_clock_and_validation();std::cout<<"flag_spinner tests PASS\n";return 0;}catch(const std::exception& e){std::cerr<<"flag_spinner tests FAIL: "<<e.what()<<'\n';return 1;}}