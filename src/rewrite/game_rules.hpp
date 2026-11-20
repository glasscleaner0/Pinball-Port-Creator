#ifndef PINBALL_REWRITE_GAME_RULES_HPP
#define PINBALL_REWRITE_GAME_RULES_HPP

#include "runtime.hpp"
#include <cstdint>

namespace pinball {
namespace rules {

enum class RoundPhase { Playing, DrainWait, ServeWait, GameOver };
enum class SavePhase { Armed, Steady, Flashing, Off };

struct GameRulesConfig {
    std::uint32_t balls_per_game;
    runtime::Tick drain_delay,feed_delay,shoot_again_on,shoot_again_flash;
    std::uint32_t initial_bonus,subsequent_bonus,score_wrap;
    runtime::Tick reflex_duration;
    std::uint32_t reflex_award;
    GameRulesConfig(std::uint32_t balls,runtime::Tick drain,runtime::Tick feed,
        runtime::Tick save_on,runtime::Tick save_flash,std::uint32_t first_bonus,
        std::uint32_t later_bonus,std::uint32_t wrap=1000000000U,
        runtime::Tick reflex_time=5000000000LL,std::uint32_t reflex_points=25000U);
};
struct GameSnapshot {
    std::uint64_t score; // The source display counter, including exact1e9.
    std::uint64_t total_score; // Display counter plus carried billions.
    std::uint32_t player,ball_number,balls_remaining;
    bool game_over,ball_active,serve_pending,shoot_again;
    RoundPhase phase;
    SavePhase save_phase;
    std::uint32_t drains,saved_drains,serves;
    bool reflex_active,skill_shot_armed;
    std::uint32_t score_multiplier;
    bool tilted;
    std::uint32_t extra_balls_pending,extra_balls_awarded,extra_balls_used,replays_awarded,replays_used;
    bool replay_armed;
};
struct RulesEvents {
    bool serve_ball,game_over;
    RulesEvents():serve_ball(false),game_over(false) {}
};
struct DrainResult {
    bool accepted,saved,extra_ball_used,replay_used;
    DrainResult():accepted(false),saved(false),extra_ball_used(false),replay_used(false) {}
};

// Fresh single-player/default-scope rules. No mission,
// tilt, second-chance award or startup show/audio state is invented here.
// Reset immediately serves the first ball as an explicit development policy.
// The host owns the simulation clock; pause freezes that clock.
class GameRules {
public:
    explicit GameRules(const GameRulesConfig& config,runtime::Tick now=0);
    void reset(runtime::Tick now=0);
    bool award(std::uint32_t points); // Ordinary points use the current multiplier.
    bool award_special(std::uint32_t points); // Bypasses multiplier; active ball only.
    void tilt(runtime::Tick now);
    void clear_tilt(runtime::Tick now);
    bool set_bonus(std::uint32_t points); // Source pool value, special/unmultiplied on loss.
    void set_score_multiplier(std::uint32_t multiplier); // Numeric 1,2,3,5,10.
    bool award_extra_ball(bool cheat=false); // Credit, consumed by the next ordinary loss.
    bool award_replay(); // One pending protection, not a stack.
    void skill_gate(runtime::Tick now);
    void skill_clear(runtime::Tick now);
    void hole_release(runtime::Tick now);
    std::uint32_t award_ramp(std::uint32_t base,runtime::Tick now);
    DrainResult drain(runtime::Tick now);
    RulesEvents advance(runtime::Tick now);
    GameSnapshot snapshot() const;
    runtime::Tick time() const;
    // Positive duration to next lifecycle/save boundary; maxTick if none.
    runtime::Tick until_boundary() const;
private:
    GameRulesConfig config_;
    runtime::Tick now_,phase_started_,save_started_,reflex_started_;
    bool reflex_active_,skill_shot_armed_,last_drain_saved_,replay_armed_,tilted_;
    RoundPhase phase_;
    SavePhase save_phase_;
    std::uint32_t balls_,score_low_,score_carry_,bonus_,drains_,saved_drains_,serves_,score_multiplier_;
    std::uint32_t extra_balls_,extra_awarded_,extra_used_,replays_awarded_,replays_used_;
    void validate_now(runtime::Tick now) const;
    void update_save(runtime::Tick now,bool inclusive);
    void update_reflex(runtime::Tick now,bool inclusive);
};

} // namespace rules
} // namespace pinball
#endif

