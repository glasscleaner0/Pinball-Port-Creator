#include "target_bank.hpp"
#include <limits>
#include <stdexcept>

namespace pinball {
namespace rules {
namespace {
bool before(runtime::Tick a,runtime::Tick b) {
    volatile std::uint32_t ah=static_cast<std::uint32_t>(static_cast<std::uint64_t>(a)>>32);
    volatile std::uint32_t al=static_cast<std::uint32_t>(a);
    volatile std::uint32_t bh=static_cast<std::uint32_t>(static_cast<std::uint64_t>(b)>>32);
    volatile std::uint32_t bl=static_cast<std::uint32_t>(b);
    return ah==bh?al<bl:ah<bh;
}
bool zero(runtime::Tick value) {
    volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)>>32);
    volatile std::uint32_t low=static_cast<std::uint32_t>(value);
    return high==0&&low==0;
}
void require_time(runtime::Tick value) {
    volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)>>32);
    if((high&0x80000000U)!=0)throw std::invalid_argument("target-bank time must be nonnegative");
}
void validate(const TargetBankConfig& c) {
    require_time(c.raise_delay);require_time(c.decay_duration);require_time(c.flash_duration);
    require_time(c.flash_off);require_time(c.flash_on);
    const runtime::Tick maximum=(std::numeric_limits<runtime::Tick>::max)();
    if(zero(c.raise_delay)||zero(c.decay_duration)||zero(c.flash_duration)||
       zero(c.flash_off)||zero(c.flash_on)||before(maximum-c.flash_off,c.flash_on)||
       c.hit_points>(std::numeric_limits<std::uint32_t>::max)()/10U||
       c.completion_points>(std::numeric_limits<std::uint32_t>::max)()/10U)
        throw std::invalid_argument("invalid target-bank configuration");
    if(c.medal){
        if(c.extra_ball_level==0||c.extra_ball_level>3)throw std::invalid_argument("invalid medal credit level");
        for(unsigned i=0;i<3;++i)if(c.medal_completion_points[i]>(std::numeric_limits<std::uint32_t>::max)()/10U)
            throw std::invalid_argument("medal award exceeds scaled range");
    }
}
std::uint32_t multiplier(std::uint32_t tier) {
    static const std::uint32_t values[5]={1,2,3,5,10};
    return values[tier];
}
runtime::Tick remaining(runtime::Tick now,runtime::Tick started,runtime::Tick duration) {
    const runtime::Tick elapsed=now-started;
    return before(elapsed,duration)?duration-elapsed:0;
}
}
TargetBankConfig::TargetBankConfig(runtime::Tick raise,runtime::Tick decay,runtime::Tick flash,
    runtime::Tick off,runtime::Tick on,std::uint32_t hit,std::uint32_t completion)
    :raise_delay(raise),decay_duration(decay),flash_duration(flash),flash_off(off),flash_on(on),
     hit_points(hit),completion_points(completion),medal(false),medal_completion_points(),extra_ball_level(0) {
    validate(*this);
}
TargetBankConfig::TargetBankConfig(runtime::Tick raise,runtime::Tick decay,runtime::Tick flash,
    runtime::Tick off,runtime::Tick on,std::uint32_t hit,
    const std::array<std::uint32_t,3>& completions,std::uint32_t credit_level)
    :raise_delay(raise),decay_duration(decay),flash_duration(flash),flash_off(off),flash_on(on),
     hit_points(hit),completion_points(0),medal(true),medal_completion_points(completions),extra_ball_level(credit_level) {
    validate(*this);
}
TargetHit::TargetHit():accepted(false),completed(false),extra_ball(false),base_points(0),multiplier_before(1),points(0) {}
TargetBank::TargetBank(const TargetBankConfig& config,runtime::Tick now):config_(config) {
    validate(config_);reset(now);
}
void TargetBank::reset(runtime::Tick now) {
    require_time(now);now_=raise_started_=decay_started_=now;
    raised_.fill(true);flashing_.fill(false);flash_started_.fill(now);
    tier_=0;raise_pending_=decay_active_=false;
}
void TargetBank::validate_now(runtime::Tick now) const {
    require_time(now);
    if(before(now,now_))throw std::invalid_argument("target-bank clock moved backward");
}
runtime::Tick TargetBank::next_boundary(unsigned& kind) const {
    runtime::Tick best=(std::numeric_limits<runtime::Tick>::max)();kind=0;
    if(raise_pending_){best=remaining(now_,raise_started_,config_.raise_delay);kind=1;}
    for(unsigned i=0;i<4;++i)if(flashing_[i]){
        const runtime::Tick value=remaining(now_,flash_started_[i],config_.flash_duration);
        if(kind==0||before(value,best)){best=value;kind=i+2;}
    }
    if(decay_active_){
        const runtime::Tick value=remaining(now_,decay_started_,config_.decay_duration);
        if(kind==0||before(value,best)){best=value;kind=6;}
    }
    return best;
}
void TargetBank::update(runtime::Tick now,bool inclusive) {
    // At most one rearm, four flash ends and four tier decays can occur
    // without a new external hit; a large clock advance stays bounded.
    for(;;){
        unsigned kind=0;const runtime::Tick delay=next_boundary(kind);
        if(kind==0)break;
        const runtime::Tick available=now-now_;
        if(before(available,delay)||(!inclusive&&!before(delay,available)))break;
        // The comparison proves this sum is <=now and cannot overflow.
        now_+=delay;
        if(kind==1){raised_.fill(true);raise_pending_=false;}
        else if(kind<6)flashing_[kind-2]=false;
        else{
            --tier_;flashing_[tier_]=false;
            if(tier_==0)decay_active_=false;
            else decay_started_=now_;
        }
    }
    now_=now;
}
TargetHit TargetBank::hit(std::size_t target,runtime::Tick now) {
    if(target>=raised_.size())throw std::out_of_range("target-bank target index");
    validate_now(now);update(now,false);
    TargetHit result;result.multiplier_before=config_.medal?1:multiplier(tier_);
    if(!raised_[target])return result;
    raised_[target]=false;result.accepted=true;
    result.completed=!raised_[0]&&!raised_[1]&&!raised_[2];
    result.base_points=!result.completed?config_.hit_points:config_.medal?
        config_.medal_completion_points[tier_<3?tier_:2]:config_.completion_points;
    result.points=result.base_points*result.multiplier_before;
    if(result.completed){
        if(tier_<(config_.medal?3U:4U)){flash_started_[tier_]=now;flashing_[tier_]=true;++tier_;}
        result.extra_ball=config_.medal&&tier_>=config_.extra_ball_level;
        raise_started_=decay_started_=now;raise_pending_=decay_active_=true;
    }
    return result;
}
void TargetBank::advance(runtime::Tick now) {
    validate_now(now);update(now,true);
}
void TargetBank::set_tier(std::uint32_t tier,runtime::Tick now,bool restart_decay) {
    if(tier>(config_.medal?3U:4U))throw std::out_of_range("target-bank tier");
    validate_now(now);update(now,false);tier_=tier;flashing_.fill(false);
    if(tier==0)decay_active_=false;
    else if(restart_decay){decay_active_=true;decay_started_=now;}
}
void TargetBank::request_rearm(runtime::Tick now) {
    validate_now(now);update(now,false);raise_started_=now;raise_pending_=true;
}
void TargetBank::ordinary_loss(runtime::Tick now) {
    validate_now(now);reset(now);
}
TargetBankSnapshot TargetBank::snapshot() const {
    TargetBankSnapshot result;result.raised=raised_;result.tier=tier_;
    result.multiplier=config_.medal?1:multiplier(tier_);result.raise_pending=raise_pending_;
    const runtime::Tick period=config_.flash_off+config_.flash_on;
    for(unsigned i=0;i<4;++i){
        const bool lit=i<tier_;
        result.lamp_modes[i]=!lit?BankLampMode::Off:
            flashing_[i]?BankLampMode::Flashing:BankLampMode::Steady;
        result.lamp_visible[i]=lit&&(!flashing_[i]||
            !before((now_-flash_started_[i])%period,config_.flash_off));
    }
    return result;
}
runtime::Tick TargetBank::time() const {return now_;}
runtime::Tick TargetBank::until_boundary() const {
    unsigned kind=0;return next_boundary(kind);
}

} // namespace rules
} // namespace pinball



