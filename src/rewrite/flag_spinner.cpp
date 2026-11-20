#include "flag_spinner.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace pinball { namespace rules {
namespace {
bool before(runtime::Tick a,runtime::Tick b){
 volatile std::uint32_t ah=static_cast<std::uint32_t>(static_cast<std::uint64_t>(a)>>32U),al=static_cast<std::uint32_t>(a);
 volatile std::uint32_t bh=static_cast<std::uint32_t>(static_cast<std::uint64_t>(b)>>32U),bl=static_cast<std::uint32_t>(b);
 return ah==bh?al<bl:ah<bh;
}
void require_time(runtime::Tick t){if((static_cast<std::uint64_t>(t)>>63U)!=0)throw std::invalid_argument("negative spinner clock");}
void add(std::uint32_t& n){if(n==(std::numeric_limits<std::uint32_t>::max)())throw std::overflow_error("spinner event counter overflow");++n;}
}
SpinnerConfig::SpinnerConfig(float low,float high,float factor,float slowdown,unsigned images):minimum(low),maximum(high),scale(factor),decay(slowdown),frames(images){
 if(!std::isfinite(low)||!std::isfinite(high)||!std::isfinite(factor)||!std::isfinite(slowdown)||low<=0||high<low||factor<=0||slowdown<=0||slowdown>=1||images<2||images>256)throw std::invalid_argument("invalid spinner configuration");
}
FlagSpinner::FlagSpinner(const SpinnerConfig& config,runtime::Tick now):config_(config.minimum,config.maximum,config.scale,config.decay,config.frames){reset(now);}
void FlagSpinner::reset(runtime::Tick now){require_time(now);now_=now;frame_=0;speed_=0;direction_=1;pending_.clear();}
void FlagSpinner::validate_now(runtime::Tick now)const{require_time(now);if(before(now,now_))throw std::invalid_argument("spinner clock moved backward");}
void FlagSpinner::frame(SpinnerEvents& events){
 frame_=direction_>0?(frame_+1)%config_.frames:(frame_+config_.frames-1)%config_.frames;
 add(events.frames);if(frame_==0)add(events.wraps);
 speed_*=config_.decay;
 if(speed_>=config_.minimum){
  const double milliseconds=std::floor(double(1.0f/speed_)*1000.0);
  if(milliseconds>double((std::numeric_limits<runtime::Tick>::max)()/1000000))throw std::overflow_error("spinner delay overflow");
  const runtime::Tick delay=static_cast<runtime::Tick>(milliseconds)*1000000;
  if(before((std::numeric_limits<runtime::Tick>::max)()-now_,delay))throw std::overflow_error("spinner deadline overflow");
  const runtime::Tick deadline=now_+delay;
  pending_.insert(std::upper_bound(pending_.begin(),pending_.end(),deadline,[](runtime::Tick a,runtime::Tick b){return before(a,b);}),deadline);
 }
}
SpinnerEvents FlagSpinner::update(runtime::Tick now,bool inclusive){
 validate_now(now);SpinnerEvents events;std::size_t callbacks=0;
 while(!pending_.empty()&&(inclusive?!before(now,pending_.front()):before(pending_.front(),now))){
  if(++callbacks>65536)throw std::runtime_error("spinner callback budget exhausted");
  now_=pending_.front();pending_.erase(pending_.begin());frame(events);
 }
 now_=now;return events;
}
SpinnerEvents FlagSpinner::hit(float incoming_speed,bool reverse,runtime::Tick now){
 validate_now(now);if(!std::isfinite(incoming_speed)||incoming_speed<0)throw std::invalid_argument("invalid spinner incoming speed");
 SpinnerEvents events=update(now,false);speed_=(std::max)(config_.minimum,(std::min)(config_.maximum,incoming_speed*config_.scale));direction_=reverse?-1:1;
 frame(events);return events;
}
SpinnerEvents FlagSpinner::advance(runtime::Tick now){return update(now,true);}
SpinnerSnapshot FlagSpinner::snapshot()const{const SpinnerSnapshot s={frame_,speed_,direction_,pending_.size()};return s;}
runtime::Tick FlagSpinner::until_boundary()const{return pending_.empty()?(std::numeric_limits<runtime::Tick>::max)():pending_.front()-now_;}
runtime::Tick FlagSpinner::time()const{return now_;}
} }