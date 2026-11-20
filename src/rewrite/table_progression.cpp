#include "table_progression.hpp"
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
    volatile std::uint32_t low=static_cast<std::uint32_t>(v);
    return high==0&&low==0;
}
void require_time(runtime::Tick v){
    volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)>>32);
    if((high&0x80000000U)!=0)throw std::invalid_argument("progression time must be nonnegative");
}
void validate(const LaneProgressionConfig& c){
    require_time(c.completion_flash);require_time(c.decay_period);require_time(c.flash_off);require_time(c.flash_on);
    if(zero(c.completion_flash)||zero(c.decay_period)||zero(c.flash_off)||zero(c.flash_on)||
       before((std::numeric_limits<runtime::Tick>::max)()-c.flash_off,c.flash_on))
        throw std::invalid_argument("invalid lane progression configuration");
}
runtime::Tick remaining(runtime::Tick now,runtime::Tick started,runtime::Tick duration){
    const runtime::Tick age=now-started;return before(age,duration)?duration-age:0;
}
}
LaneProgressionConfig::LaneProgressionConfig(runtime::Tick flash,runtime::Tick decay,runtime::Tick off,runtime::Tick on)
    :completion_flash(flash),decay_period(decay),flash_off(off),flash_on(on){validate(*this);}
TableProgression::TableProgression(const std::array<LaneProgressionConfig,2>& config,runtime::Tick now):config_(config){
    for(unsigned i=0;i<2;++i)validate(config_[i]);reset(now);
}
void TableProgression::reset(runtime::Tick now){
    require_time(now);now_=now;
    for(unsigned i=0;i<2;++i){
        Group& g=groups_[i];g.lamps.fill(false);g.level=0;g.flashing=g.decay_active=false;
        g.flash_started=g.decay_started=now;
    }
}
void TableProgression::validate_now(runtime::Tick now) const{
    require_time(now);if(before(now,now_))throw std::invalid_argument("progression clock moved backward");
}
runtime::Tick TableProgression::next_boundary(unsigned& kind) const{
    runtime::Tick best=(std::numeric_limits<runtime::Tick>::max)();kind=0;
    for(unsigned i=0;i<2;++i){
        const Group& g=groups_[i];
        if(g.flashing){
            const runtime::Tick value=remaining(now_,g.flash_started,config_[i].completion_flash);
            if(kind==0||before(value,best)){best=value;kind=i*2+1;}
        }
        if(g.decay_active){
            const runtime::Tick value=remaining(now_,g.decay_started,config_[i].decay_period);
            if(kind==0||before(value,best)){best=value;kind=i*2+2;}
        }
    }
    return best;
}
std::uint32_t TableProgression::update(runtime::Tick now,bool inclusive){
    std::uint32_t changed=0;
    // No external hits: at most two flash ends and six observable level decays.
    for(;;){
        unsigned kind=0;const runtime::Tick delay=next_boundary(kind);
        if(kind==0)break;const runtime::Tick available=now-now_;
        if(before(available,delay)||(!inclusive&&!before(delay,available)))break;
        now_+=delay;const unsigned bank=(kind-1)/2;Group& g=groups_[bank];
        if((kind&1U)!=0)g.flashing=false;
        else{
            --g.level;changed|=1U<<bank;
            if(g.level==0)g.decay_active=false;else g.decay_started=now_;
        }
    }
    now_=now;return changed;
}
LaneHit TableProgression::hit(std::size_t bank,std::size_t lane,runtime::Tick now){
    if(bank>=2||lane>=3)throw std::out_of_range("lane progression owner");
    validate_now(now);LaneHit result={false,update(now,false)};Group& g=groups_[bank];
    if(g.flashing)return result;
    g.lamps[lane]=!g.lamps[lane];
    if(g.lamps[0]&&g.lamps[1]&&g.lamps[2]){
        g.lamps.fill(false);g.flashing=true;g.flash_started=now;
        g.decay_active=true;g.decay_started=now;result.completed=true;
        if(g.level<3){++g.level;result.level_changed_mask|=1U<<bank;}
    }
    return result;
}
std::uint32_t TableProgression::rotate(int direction,runtime::Tick now){
    if(direction!=-1&&direction!=1)throw std::invalid_argument("lane rotation direction");
    validate_now(now);const std::uint32_t changed=update(now,false);
    for(unsigned i=0;i<2;++i)if(!groups_[i].flashing){
        const std::array<bool,3> old=groups_[i].lamps;
        for(unsigned lane=0;lane<3;++lane)
            groups_[i].lamps[lane]=old[(lane+(direction<0?1U:2U))%3];
    }
    return changed;
}
std::uint32_t TableProgression::advance(runtime::Tick now){validate_now(now);return update(now,true);}
std::uint32_t TableProgression::set_level(std::size_t bank,std::uint32_t level,runtime::Tick now){
    if(bank>=2||level>3)throw std::out_of_range("progression bumper level");
    validate_now(now);std::uint32_t changed=update(now,false);
    if(groups_[bank].level!=level){groups_[bank].level=level;changed|=1U<<bank;}
    if(level==0)groups_[bank].decay_active=false;
    return changed;
}
void TableProgression::ordinary_loss(runtime::Tick now){validate_now(now);reset(now);}
ProgressionSnapshot TableProgression::snapshot() const{
    ProgressionSnapshot result;
    for(unsigned i=0;i<2;++i){
        const Group& g=groups_[i];LaneGroupSnapshot& s=result.groups[i];
        s.logical_on=g.lamps;s.level=g.level;s.flashing=g.flashing;
        const LaneProgressionConfig& c=config_[i];
        const bool phase=g.flashing&&!before((now_-g.flash_started)%(c.flash_off+c.flash_on),c.flash_off);
        for(unsigned lane=0;lane<3;++lane)s.lamp_visible[lane]=g.flashing?phase:g.lamps[lane];
    }
    return result;
}
runtime::Tick TableProgression::until_boundary() const{unsigned kind=0;return next_boundary(kind);}
runtime::Tick TableProgression::time() const{return now_;}
} }

