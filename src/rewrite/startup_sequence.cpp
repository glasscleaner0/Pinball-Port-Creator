#include "startup_sequence.hpp"
#include <limits>
#include <stdexcept>
namespace pinball { namespace startup {
namespace {
bool before(runtime::Tick a,runtime::Tick b) {
    volatile std::uint32_t ah=static_cast<std::uint32_t>(static_cast<std::uint64_t>(a)>>32U),al=static_cast<std::uint32_t>(a);
    volatile std::uint32_t bh=static_cast<std::uint32_t>(static_cast<std::uint64_t>(b)>>32U),bl=static_cast<std::uint32_t>(b);
    return ah==bh?al<bl:ah<bh;
}
void nonnegative(runtime::Tick value) {
    volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)>>32U);
    if(high&0x80000000U)throw std::invalid_argument("startup clock/duration must be nonnegative");
}
bool zero(runtime::Tick value) {
    volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)>>32U),low=static_cast<std::uint32_t>(value);
    return high==0&&low==0;
}
void validate(const SequenceConfig& c) {
    nonnegative(c.intro_duration);nonnegative(c.feed_delay);nonnegative(c.stall_timeout);
    if(zero(c.intro_duration)||zero(c.feed_delay)||zero(c.stall_timeout)||
       before((std::numeric_limits<runtime::Tick>::max)()-c.intro_duration,c.stall_timeout))
        throw std::invalid_argument("invalid startup duration or hard-limit overflow");
}
runtime::Tick capped_add(runtime::Tick value,runtime::Tick delta,runtime::Tick limit) {
    return !before(delta,limit-value)?limit:value+delta;
}
bool passed(std::uint32_t current,std::uint32_t tail) {
    const std::uint32_t distance=current-tail;
    return distance!=0&&distance<0x80000000U;
}
}
SequenceConfig::SequenceConfig(runtime::Tick intro,runtime::Tick feed,runtime::Tick stalled)
    :intro_duration(intro),feed_delay(feed),stall_timeout(stalled) {validate(*this);}
Sequence::Sequence(const SequenceConfig& config,runtime::Tick now)
    :config_(config),now_(now),elapsed_(0),progress_at_(0),generation_(0),submitted_(0),played_(0),tail_(0),
     phase_(Phase::Playing),accepted_(false),tracking_(false),tail_pending_(false) {
    validate(config_);nonnegative(now);
}
void Sequence::validate_now(runtime::Tick now) const {
    nonnegative(now);if(before(now,now_))throw std::invalid_argument("startup clock moved backward");
}
void Sequence::restart(runtime::Tick now,bool accepted,const AudioState& audio) {
    validate_now(now);
    if(generation_==(std::numeric_limits<std::uint32_t>::max)())throw std::overflow_error("startup generations exhausted");
    ++generation_;now_=now;elapsed_=progress_at_=0;phase_=Phase::Intro;
    accepted_=accepted;tracking_=accepted&&audio.running;tail_pending_=false;
    submitted_=audio.submitted;played_=audio.played;tail_=0;
}
void Sequence::bypass(runtime::Tick now) {
    validate_now(now);now_=now;elapsed_=progress_at_=0;phase_=Phase::Playing;
    accepted_=tracking_=tail_pending_=false;tail_=0;
}
Events Sequence::advance(runtime::Tick now,bool paused,const AudioState& audio) {
    validate_now(now);const runtime::Tick delta=now-now_;now_=now;Events e;
    if(paused||phase_==Phase::Playing)return e;
    if(phase_==Phase::FeedDelay) {
        elapsed_=capped_add(elapsed_,delta,config_.feed_delay);
        if(!before(elapsed_,config_.feed_delay)){phase_=Phase::Playing;e.became_playing=true;}
        return e;
    }
    elapsed_=capped_add(elapsed_,delta,config_.intro_duration+config_.stall_timeout);
    bool drained=false;
    if(tracking_) {
        if(!audio.running)tracking_=false;
        else {
            if(audio.submitted!=submitted_||audio.played!=played_){progress_at_=elapsed_;submitted_=audio.submitted;played_=audio.played;}
            if(audio.active_voices==0&&!tail_pending_){tail_pending_=true;tail_=audio.submitted;}
            drained=tail_pending_&&passed(audio.played,tail_);
            if(!drained&&(!before(elapsed_-progress_at_,config_.stall_timeout)||
                !before(elapsed_,config_.intro_duration+config_.stall_timeout)))tracking_=false;
        }
    }
    if(drained||(!tracking_&&!before(elapsed_,config_.intro_duration))) {
        e.play_feed=true;e.flush_audio=accepted_&&!drained;
        phase_=Phase::FeedDelay;elapsed_=progress_at_=0;tracking_=tail_pending_=false;
    }
    return e;
}
bool Sequence::blocks_gameplay() const {return phase_!=Phase::Playing;}
bool Sequence::ball_visible() const {return phase_==Phase::Playing;}
Snapshot Sequence::snapshot() const {
    const runtime::Tick duration=phase_==Phase::Intro?config_.intro_duration:phase_==Phase::FeedDelay?config_.feed_delay:0;
    const runtime::Tick remaining=before(elapsed_,duration)?duration-elapsed_:0;
    const Snapshot result={phase_,elapsed_,remaining,generation_,tracking_,tail_pending_};return result;
}
} }
