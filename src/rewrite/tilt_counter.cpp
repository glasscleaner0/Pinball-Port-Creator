#include "tilt_counter.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace pinball { namespace rules {
namespace {
bool before(runtime::Tick a,runtime::Tick b){
    volatile std::uint32_t ah=static_cast<std::uint32_t>(static_cast<std::uint64_t>(a)>>32);
    volatile std::uint32_t al=static_cast<std::uint32_t>(a);
    volatile std::uint32_t bh=static_cast<std::uint32_t>(static_cast<std::uint64_t>(b)>>32);
    volatile std::uint32_t bl=static_cast<std::uint32_t>(b);
    return ah==bh?al<bl:ah<bh;
}
bool zero(runtime::Tick v){
    volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)>>32);
    volatile std::uint32_t low=static_cast<std::uint32_t>(v);return high==0&&low==0;
}
void require_time(runtime::Tick v){
    volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)>>32);
    if((high&0x80000000U)!=0)throw std::invalid_argument("tilt counter time must be nonnegative");
}
void validate(const TiltCounterConfig& c){
    require_time(c.drain_timeout);
    if(!std::isfinite(c.active_rate)||!std::isfinite(c.decay_rate)||!std::isfinite(c.warning_threshold)||
       !std::isfinite(c.tilt_threshold)||!std::isfinite(c.after_clear)||c.active_rate<=0||c.decay_rate<0||
       c.warning_threshold<0||c.tilt_threshold<=c.warning_threshold||c.after_clear>c.warning_threshold||zero(c.drain_timeout))
        throw std::invalid_argument("invalid tilt counter configuration");
}
}
TiltCounterConfig::TiltCounterConfig(double rise,double decay,double warning,double tilt,runtime::Tick timeout,double clear_value)
    :active_rate(rise),decay_rate(decay),warning_threshold(warning),tilt_threshold(tilt),after_clear(clear_value),drain_timeout(timeout){validate(*this);}
TiltCounter::TiltCounter(const TiltCounterConfig& config,runtime::Tick now):config_(config){
    validate(config_);reset(now);
}
void TiltCounter::validate_now(runtime::Tick now) const{
    require_time(now);if(before(now,now_))throw std::invalid_argument("tilt counter clock moved backward");
}
void TiltCounter::reset(runtime::Tick now){
    require_time(now);now_=tilted_at_=now;pressure_=0;locked_=timeout_pending_=false;
}
TiltEvents TiltCounter::advance(runtime::Tick now,bool nudge_active,bool may_tilt){
    validate_now(now);const runtime::Tick elapsed=now-now_;
    double next=pressure_;
    if(!zero(elapsed)){
        const double seconds=static_cast<double>(elapsed)/runtime::ticks_per_second;
        next=nudge_active?pressure_+config_.active_rate*seconds:pressure_-config_.decay_rate*seconds;
        if(!std::isfinite(next))throw std::overflow_error("tilt pressure overflow");
        if(!nudge_active&&next<0)next=0;
    }
    pressure_=next;now_=now;TiltEvents events;
    // The reference timer check occurs before the warning/tilt checks.
    if(timeout_pending_&&!before(now_-tilted_at_,config_.drain_timeout)){
        timeout_pending_=false;events.force_drain=locked_;
    }
    if(!locked_){
        events.warning=pressure_>config_.warning_threshold;
        if(may_tilt&&pressure_>config_.tilt_threshold){
            locked_=timeout_pending_=true;tilted_at_=now;events.tilted=true;
        }
    }
    return events;
}
void TiltCounter::clear_lock(runtime::Tick now){
    validate_now(now);now_=now;locked_=timeout_pending_=false;
}
void TiltCounter::tilt_no_more(runtime::Tick now){
    validate_now(now);now_=now;locked_=timeout_pending_=false;pressure_=config_.after_clear;
}
runtime::Tick TiltCounter::until_boundary() const{
    if(!timeout_pending_)return (std::numeric_limits<runtime::Tick>::max)();
    const runtime::Tick elapsed=now_-tilted_at_;
    return before(elapsed,config_.drain_timeout)?config_.drain_timeout-elapsed:0;
}
TiltSnapshot TiltCounter::snapshot() const{
    const TiltSnapshot s={pressure_,locked_,timeout_pending_,until_boundary()};return s;
}
runtime::Tick TiltCounter::time() const{return now_;}
} }
