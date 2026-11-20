#include "game_rules.hpp"

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
    if((static_cast<std::uint64_t>(value)>>63)!=0)
        throw std::invalid_argument("rules time must be nonnegative");
}
void validate(const GameRulesConfig& c) {
    require_time(c.drain_delay);require_time(c.feed_delay);
    require_time(c.shoot_again_on);require_time(c.shoot_again_flash);require_time(c.reflex_duration);
    if(c.balls_per_game==0||zero(c.drain_delay)||zero(c.feed_delay)||
       zero(c.shoot_again_on)||zero(c.shoot_again_flash)||c.score_wrap==0||
       c.score_wrap>1000000000U||c.initial_bonus>c.score_wrap||c.subsequent_bonus>c.score_wrap||
       zero(c.reflex_duration)||c.reflex_award>c.score_wrap)
        throw std::invalid_argument("invalid default game rules configuration");
}
void increment(std::uint32_t& value) {
    if(value!=(std::numeric_limits<std::uint32_t>::max)())++value;
}
runtime::Tick remaining(runtime::Tick elapsed,runtime::Tick duration) {
    return before(elapsed,duration)?duration-elapsed:0;
}
}
GameRulesConfig::GameRulesConfig(std::uint32_t balls,runtime::Tick drain,runtime::Tick feed,
    runtime::Tick save_on,runtime::Tick save_flash,std::uint32_t first_bonus,
    std::uint32_t later_bonus,std::uint32_t wrap,runtime::Tick reflex_time,std::uint32_t reflex_points)
    :balls_per_game(balls),drain_delay(drain),feed_delay(feed),shoot_again_on(save_on),
     shoot_again_flash(save_flash),initial_bonus(first_bonus),subsequent_bonus(later_bonus),score_wrap(wrap),
     reflex_duration(reflex_time),reflex_award(reflex_points) {
    validate(*this);
}
GameRules::GameRules(const GameRulesConfig& config,runtime::Tick now):config_(config) {
    validate(config_);reset(now);
}
void GameRules::reset(runtime::Tick now) {
    require_time(now);
    now_=phase_started_=save_started_=reflex_started_=now;phase_=RoundPhase::Playing;save_phase_=SavePhase::Armed;
    reflex_active_=false;skill_shot_armed_=true;last_drain_saved_=false;tilted_=false;
    balls_=config_.balls_per_game;score_low_=score_carry_=0;bonus_=config_.initial_bonus;
    drains_=saved_drains_=0;serves_=1;score_multiplier_=1;
    extra_balls_=extra_awarded_=extra_used_=replays_awarded_=replays_used_=0;replay_armed_=false;
}
void GameRules::validate_now(runtime::Tick now) const {
    require_time(now);if(before(now,now_))throw std::invalid_argument("rules clock moved backward");
}
bool GameRules::award_extra_ball(bool cheat) {
    if((tilted_&&!cheat)||phase_!=RoundPhase::Playing)return false;
    if(extra_balls_==(std::numeric_limits<std::uint32_t>::max)())
        throw std::overflow_error("extra-ball credits exhausted");
    ++extra_balls_;increment(extra_awarded_);return true;
}
bool GameRules::award_replay() {
    if(tilted_||phase_!=RoundPhase::Playing||replay_armed_)return false;
    replay_armed_=true;increment(replays_awarded_);return true;
}
void GameRules::tilt(runtime::Tick now) {
    validate_now(now);if(phase_!=RoundPhase::Playing||tilted_)return;
    update_save(now,false);update_reflex(now,false);now_=now;
    tilted_=true;save_phase_=SavePhase::Off;replay_armed_=false;reflex_active_=false;
}
void GameRules::clear_tilt(runtime::Tick now) {validate_now(now);now_=now;tilted_=false;}
bool GameRules::set_bonus(std::uint32_t points) {
    if(points>5000000U||points>config_.score_wrap)throw std::invalid_argument("bonus pool exceeds supported range");
    if(phase_!=RoundPhase::Playing)return false;
    bonus_=points;return true;
}
void GameRules::set_score_multiplier(std::uint32_t value) {
    if(value!=1&&value!=2&&value!=3&&value!=5&&value!=10)
        throw std::invalid_argument("unsupported numeric score multiplier");
    score_multiplier_=value;
}
bool GameRules::award(std::uint32_t points) {
    const std::uint64_t scaled=std::uint64_t(points)*score_multiplier_;
    if(scaled>config_.score_wrap)throw std::invalid_argument("multiplied award exceeds supported range");
    return award_special(static_cast<std::uint32_t>(scaled));
}
bool GameRules::award_special(std::uint32_t points) {
    if(points>config_.score_wrap)throw std::invalid_argument("award exceeds supported single-add range");
    if(tilted_||phase_!=RoundPhase::Playing)return false;
    const std::uint64_t sum=std::uint64_t(score_low_)+points;
    const bool carry=sum>config_.score_wrap;
    if(carry&&score_carry_==(std::numeric_limits<std::uint32_t>::max)())
        throw std::overflow_error("score carry exhausted");
    score_low_=static_cast<std::uint32_t>(carry?sum-config_.score_wrap:sum);
    if(carry)++score_carry_;
    return true;
}
void GameRules::update_save(runtime::Tick now,bool inclusive) {
    if(save_phase_==SavePhase::Armed||save_phase_==SavePhase::Off)return;
    const runtime::Tick elapsed=now-save_started_;
    const bool on_expired=inclusive?!before(elapsed,config_.shoot_again_on):
        before(config_.shoot_again_on,elapsed);
    if(!on_expired){save_phase_=SavePhase::Steady;return;}
    const runtime::Tick flash_elapsed=elapsed-config_.shoot_again_on;
    const bool flash_expired=inclusive?!before(flash_elapsed,config_.shoot_again_flash):
        before(config_.shoot_again_flash,flash_elapsed);
    save_phase_=flash_expired?SavePhase::Off:SavePhase::Flashing;
}
void GameRules::update_reflex(runtime::Tick now,bool inclusive) {
    if(!reflex_active_)return;
    const runtime::Tick elapsed=now-reflex_started_;
    if(inclusive?!before(elapsed,config_.reflex_duration):before(config_.reflex_duration,elapsed))
        reflex_active_=false;
}
void GameRules::hole_release(runtime::Tick now) {
    validate_now(now);
    if(tilted_||phase_!=RoundPhase::Playing)return;
    update_save(now,false);now_=now;reflex_started_=now;reflex_active_=true;
}
std::uint32_t GameRules::award_ramp(std::uint32_t base,runtime::Tick now) {
    validate_now(now);
    if(base>config_.score_wrap)throw std::invalid_argument("ramp award exceeds supported range");
    if(tilted_||phase_!=RoundPhase::Playing)return 0;
    update_save(now,false);update_reflex(now,false);now_=now;
    const std::uint32_t points=reflex_active_?config_.reflex_award:base;
    if(reflex_active_)award_special(points);else award(points);
    return reflex_active_?points:points*score_multiplier_;
}
void GameRules::skill_clear(runtime::Tick now) {
    validate_now(now);
    if(tilted_||phase_!=RoundPhase::Playing)return;
    update_save(now,false);update_reflex(now,false);now_=now;skill_shot_armed_=false;
}
void GameRules::skill_gate(runtime::Tick now) {
    validate_now(now);
    if(tilted_||phase_!=RoundPhase::Playing)return;
    update_reflex(now,false);now_=now;save_started_=now;save_phase_=SavePhase::Steady;
    if(skill_shot_armed_){reflex_started_=now;reflex_active_=true;}
}
DrainResult GameRules::drain(runtime::Tick now) {
    validate_now(now);DrainResult result;
    if(phase_!=RoundPhase::Playing)return result;
    // Physical contact precedes an equal-time timer expiry, as in the source
    // frame's collision-before-timer ordering.
    update_save(now,false);update_reflex(now,false);
    result.accepted=true;result.replay_used=save_phase_==SavePhase::Off&&replay_armed_;
    result.saved=save_phase_!=SavePhase::Off||result.replay_used;last_drain_saved_=result.saved;
    if(result.saved) {
        if(result.replay_used){replay_armed_=false;increment(replays_used_);}
        save_phase_=SavePhase::Armed;save_started_=now;increment(saved_drains_);
    } else {
        if(!tilted_)award_special(bonus_);bonus_=config_.subsequent_bonus;
        if(extra_balls_!=0){--extra_balls_;increment(extra_used_);result.extra_ball_used=true;}else --balls_;
        reflex_active_=false;skill_shot_armed_=false;
        score_multiplier_=1;
    }
    increment(drains_);phase_=RoundPhase::DrainWait;phase_started_=now;now_=now;
    return result;
}
RulesEvents GameRules::advance(runtime::Tick now) {
    validate_now(now);RulesEvents events;update_save(now,true);update_reflex(now,true);
    if(phase_==RoundPhase::DrainWait&&!before(now-phase_started_,config_.drain_delay)) {
        // This sum is <=now by the proven elapsed comparison, so it cannot
        // overflow even when the clock is near maxTick.
        phase_started_+=config_.drain_delay;
        if(balls_==0){phase_=RoundPhase::GameOver;events.game_over=true;}
        else{phase_=RoundPhase::ServeWait;save_phase_=SavePhase::Armed;save_started_=phase_started_;
            if(!last_drain_saved_)skill_shot_armed_=true;}
    }
    if(phase_==RoundPhase::ServeWait&&!before(now-phase_started_,config_.feed_delay)) {
        phase_started_+=config_.feed_delay;phase_=RoundPhase::Playing;save_phase_=SavePhase::Armed;
        save_started_=phase_started_;increment(serves_);events.serve_ball=true;
    }
    now_=now;return events;
}
GameSnapshot GameRules::snapshot() const {
    const bool active=phase_==RoundPhase::Playing;
    const GameSnapshot state={score_low_,std::uint64_t(score_carry_)*config_.score_wrap+score_low_,
        1,balls_==0?0:config_.balls_per_game-balls_+1,balls_,phase_==RoundPhase::GameOver,active,
        balls_!=0&&(phase_==RoundPhase::DrainWait||phase_==RoundPhase::ServeWait),
        save_phase_!=SavePhase::Off,phase_,save_phase_,drains_,saved_drains_,serves_,reflex_active_,skill_shot_armed_,score_multiplier_,tilted_,
        extra_balls_,extra_awarded_,extra_used_,replays_awarded_,replays_used_,replay_armed_};
    return state;
}
runtime::Tick GameRules::time() const{return now_;}
runtime::Tick GameRules::until_boundary() const {
    runtime::Tick result=(std::numeric_limits<runtime::Tick>::max)();
    if(phase_==RoundPhase::DrainWait)result=remaining(now_-phase_started_,config_.drain_delay);
    else if(phase_==RoundPhase::ServeWait)result=remaining(now_-phase_started_,config_.feed_delay);
    if(save_phase_==SavePhase::Steady) {
        const runtime::Tick left=remaining(now_-save_started_,config_.shoot_again_on);
        if(before(left,result))result=left;
    } else if(save_phase_==SavePhase::Flashing) {
        const runtime::Tick left=remaining(now_-save_started_-config_.shoot_again_on,config_.shoot_again_flash);
        if(before(left,result))result=left;
    }
    if(reflex_active_) {
        const runtime::Tick left=remaining(now_-reflex_started_,config_.reflex_duration);
        if(before(left,result))result=left;
    }
    return result;
}
} // namespace rules
} // namespace pinball


