#include "kickback.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace pinball { namespace rules {
namespace {
bool before(std::uint64_t a,std::uint64_t b){
    volatile std::uint32_t ah=static_cast<std::uint32_t>(a>>32),al=static_cast<std::uint32_t>(a);
    volatile std::uint32_t bh=static_cast<std::uint32_t>(b>>32),bl=static_cast<std::uint32_t>(b);
    return ah==bh?al<bl:ah<bh;
}
bool equal(std::uint64_t a,std::uint64_t b){return !before(a,b)&&!before(b,a);}
void require_time(runtime::Tick v){
    volatile std::uint32_t h=static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)>>32);
    if((h&0x80000000U)!=0)throw std::invalid_argument("kickback time must be nonnegative");
}
void validate(const KickbackConfig& c){
    require_time(c.arm_delay);require_time(c.pulse_period);
    if(equal(static_cast<std::uint64_t>(c.arm_delay),0)||equal(static_cast<std::uint64_t>(c.pulse_period),0)||
       !std::isfinite(c.inactive_threshold)||!std::isfinite(c.armed_threshold)||c.inactive_threshold<0||
       c.armed_threshold<0||c.armed_threshold>c.inactive_threshold||c.idle_frame< -1||c.fired_frame< -1||c.reset_frame< -1)
        throw std::invalid_argument("invalid kickback configuration");
}
}
KickbackConfig::KickbackConfig(runtime::Tick arm,runtime::Tick pulse,float inactive,float armed,int idle,int fired,int reset)
    :arm_delay(arm),pulse_period(pulse),inactive_threshold(inactive),armed_threshold(armed),
     idle_frame(idle),fired_frame(fired),reset_frame(reset){validate(*this);}
KickbackEvents::KickbackEvents():contact_processed(false),started(false),kicked(false),gate_close(false),
    due_remaining(false),callbacks(0),pulses(0){}
Kickback::Kickback(const KickbackConfig& config,runtime::Tick now)
    :config_(config),now_(now),active_(false),armed_(false),tilted_(false),frame_(config.idle_frame),
     sequence_(0),tracked_(0),pending_(){validate(config_);require_time(now);}
void Kickback::validate_now(runtime::Tick now) const{
    require_time(now);if(before(static_cast<std::uint64_t>(now),static_cast<std::uint64_t>(now_)))
        throw std::invalid_argument("kickback clock moved backward");
}
void Kickback::schedule(std::uint64_t base,runtime::Tick delay){
    if(equal(sequence_,(std::numeric_limits<std::uint64_t>::max)()))
        throw std::overflow_error("kickback callback sequence exhausted");
    // Both operands are <=maxTick, so their sum fits uint64 even when a
    // future callback lies beyond the representable public clock horizon.
    const Pending p={base+static_cast<std::uint64_t>(delay),++sequence_};
    pending_.push_back(p);tracked_=p.id;
}
std::size_t Kickback::first_pending() const{
    std::size_t best=pending_.size();
    for(std::size_t i=0;i<pending_.size();++i)
        if(best==pending_.size()||before(pending_[i].deadline,pending_[best].deadline)||
           (equal(pending_[i].deadline,pending_[best].deadline)&&before(pending_[i].id,pending_[best].id)))best=i;
    return best;
}
KickbackEvents Kickback::dispatch(runtime::Tick now,bool inclusive,std::size_t budget){
    KickbackEvents result;const std::uint64_t end=static_cast<std::uint64_t>(now);
    for(;;){
        const std::size_t index=first_pending();
        if(index==pending_.size())break;
        const Pending next=pending_[index];
        if(before(end,next.deadline)||(!inclusive&&equal(end,next.deadline)))break;
        if(result.callbacks==budget){result.due_remaining=true;break;}
        pending_.erase(pending_.begin()+index);++result.callbacks;
        if(active_&&!tilted_){
            armed_=true;frame_=config_.fired_frame;++result.pulses;schedule(next.deadline,config_.pulse_period);
        }else{
            armed_=false;frame_=config_.idle_frame;tracked_=0;result.gate_close=true;
        }
    }
    now_=now;return result;
}
KickbackEvents Kickback::advance(runtime::Tick now,std::size_t budget){
    validate_now(now);return dispatch(now,true,budget);
}
KickbackEvents Kickback::collision(bool strict_activated,runtime::Tick now,std::size_t budget){
    validate_now(now);KickbackEvents result=dispatch(now,false,budget);
    if(result.due_remaining)return result;
    result.contact_processed=true;
    if(tilted_)return result;
    if(!active_){
        active_=true;armed_=false;schedule(static_cast<std::uint64_t>(now),config_.arm_delay);result.started=true;
    }else if(strict_activated){
        active_=false;armed_=false;result.kicked=true;
    }
    return result;
}
void Kickback::reset(runtime::Tick now){
    require_time(now);
    if(!equal(tracked_,0))frame_=config_.reset_frame;
    pending_.clear();tracked_=0;active_=armed_=tilted_=false;now_=now;
}
void Kickback::set_tilted(bool tilted,runtime::Tick now){
    validate_now(now);
    if(tilted&&!tilted_){reset(now);tilted_=true;}
    else{tilted_=tilted;now_=now;}
}
runtime::Tick Kickback::until_boundary() const{
    const std::size_t index=first_pending();
    if(index==pending_.size())return (std::numeric_limits<runtime::Tick>::max)();
    const std::uint64_t now=static_cast<std::uint64_t>(now_),deadline=pending_[index].deadline;
    if(!before(now,deadline))return 0;
    return static_cast<runtime::Tick>(deadline-now);
}
KickbackSnapshot Kickback::snapshot() const{
    const KickbackSnapshot s={active_,armed_,tilted_,frame_>=0,
        tilted_?config_.armed_threshold:(active_&&armed_?config_.armed_threshold:config_.inactive_threshold),
        frame_,pending_.size(),until_boundary()};return s;
}
runtime::Tick Kickback::time() const{return now_;}
} }
