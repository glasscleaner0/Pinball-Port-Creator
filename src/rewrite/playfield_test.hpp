#ifndef PINBALL_REWRITE_PLAYFIELD_TEST_HPP
#define PINBALL_REWRITE_PLAYFIELD_TEST_HPP
#include "ball_visual.hpp"
#include "reference_table.hpp"
#include "reference_plunger.hpp"
#include "reference_nudge.hpp"
#include "reference_hole.hpp"
#include "game_rules.hpp"
#include "target_bank.hpp"
#include "table_progression.hpp"
#include "mission_engine.hpp"
#include "advanced_table_rules.hpp"
#include "flag_spinner.hpp"
#include "kickback.hpp"
#include "tilt_counter.hpp"
#include "sound_cue.hpp"
#include "cheat_action.hpp"
#include "score_display.hpp"
#include "projection.hpp"
#include "table_shake.hpp"
#include <cstdint>
#include <vector>
namespace pinball {
namespace development {
enum class SessionMode { FiniteRound, PhysicsTest };
struct ComponentVisualState {
    bool active,visible;
    std::size_t frame;
    runtime::Tick remaining; // Until the earliest pending expiry, even if hidden.
};
struct MotionSnapshot {
    runtime::Tick time;
    motion::BallState ball;
    float pull;
    bool seated, suspended;
    std::uint32_t launches, returns, contacts, exhausted_steps;
    double left_flipper, right_flipper;
    std::uint32_t flipper_contacts, nudges, moving_exhausted;
    int shake_x,shake_y;
    float charge;
    bool launch_window, captured;
    std::uint32_t sink_captures,sink_ejections;
    bool captured_kickout;
    std::uint32_t kickout_captures,kickout_ejections;
    runtime::Tick kickout_cooldown;
    bool captured_hole;
    std::uint32_t hole_captures,hole_landings,ramp_transitions;
    bool hidden_test;
    std::uint32_t bumper_hits,rebound_hits,active_flashes;
    std::uint64_t score,total_score;
    std::uint32_t player,ball_number,balls_remaining;
    bool game_over,ball_active,serve_pending,shoot_again;
    std::uint32_t drains,saved_drains,serves;
    std::uint32_t target_hits,bank_completions,score_multiplier,multiplier_tier;
    std::uint32_t lane_hits,lane_completions,ground_bumper_level,raised_bumper_level;
    std::uint32_t medal_hits,medal_completions,medal_tier,extra_balls_pending,extra_balls_used;
    bool replay_armed;
    std::uint32_t mission_id,mission_selected,mission_stage,mission_remaining,rank,rank_progress,missions_completed;
    bool tilted;double tilt_pressure;
    bool unlimited_balls,cheats_used,gravity_well_active;
};
class PlayfieldTest {
public:
    explicit PlayfieldTest(const table::TableModel& model);
    PlayfieldTest(const table::TableModel& model,SessionMode mode);
    // Deterministic replay start; reset still returns to the normal feed.
    PlayfieldTest(const table::TableModel& model,physics::Vec2 position,physics::Vec2 velocity);
    PlayfieldTest(const table::TableModel& model,const motion::BallState& state);
    PlayfieldTest(const table::TableModel& model,const motion::BallState& state,SessionMode mode);
    // Harness-only replay positioning; no controller binding and no rules reset.
    void set_replay_ball(const motion::BallState& state);
    void reset();
    void set_suspended(bool suspended);
    void input(const runtime::InputTransition& transition);
    // Normalized screen directions: +X right, +Y up. Ignored while disabled/paused.
    void set_test_stick(float x,float y);
    void advance(runtime::Tick duration);
    void render(scene::ResourceScene& scene,const runtime::InputState& input,bool ball_visible=true) const;
    MotionSnapshot snapshot() const;
    ComponentVisualState component_visual(std::size_t group) const;
    rules::TargetBankSnapshot target_bank_snapshot() const;
    rules::TargetBankSnapshot medal_bank_snapshot() const;
    rules::ProgressionSnapshot progression_snapshot() const;
    rules::MissionSnapshot mission_snapshot() const;
    std::string mission_text() const;
    rules::AdvancedSnapshot advanced_snapshot() const;
    ComponentVisualState advanced_target_visual(std::size_t group) const;
    rules::KickbackSnapshot kickback_snapshot(std::size_t group) const;
    rules::TiltSnapshot tilt_snapshot() const;
    void drain_sounds(std::vector<audio::SoundCue>& destination);
    std::uint32_t sound_cue_drops() const;
    bool activate_cheat(rules::CheatAction action);
    ComponentVisualState rollover_visual(std::size_t group) const;
    std::size_t collider_count() const;
    runtime::Tick full_pull_time() const;
    runtime::Tick flipper_raise_time() const;
    runtime::Tick flipper_return_time() const;
private:
    reference::TablePhysics profile_;
    visual::Camera camera_;
    visual::BallSpriteCache art_;
    hud::ScoreDisplay score_display_;
    std::vector<double> art_depth_;
    std::vector<motion::StaticCollider> colliders_;
    std::vector<flipper::TimedFlipper> flippers_;
    std::vector<runtime::Tick> kickout_cooldowns_;
    struct ComponentState {
        bool active;
        std::vector<runtime::Tick> fired_at;
        ComponentState():active(false),fired_at() {}
    };
    std::vector<ComponentState> component_states_;
    std::vector<std::size_t> component_for_collider_;
    std::vector<std::size_t> target_for_collider_;
    std::vector<std::size_t> advanced_target_for_collider_;
    std::map<std::size_t,std::uint32_t> advanced_light_numbers_;
    struct RolloverState {
        bool inside,cooldown,delayed_hide;
        runtime::Tick exited_at;
        RolloverState():inside(false),cooldown(false),delayed_hide(false),exited_at(0) {}
    };
    std::vector<RolloverState> rollover_states_;
    std::vector<std::size_t> rollover_for_collider_;
    std::vector<bool> rollover_exit_;
    motion::BallState ball_;
    motion::BallStepper stepper_;
    reference::Plunger plunger_;
    reference::ReferenceNudge nudge_;
    reference::HoleDrop hole_drop_;
    rules::GameRules game_rules_;
    rules::TargetBank target_bank_,medal_bank_;
    rules::TableProgression progression_;
    rules::MissionEngine mission_;
    rules::AdvancedTableRules advanced_;
    std::vector<rules::FlagSpinner> spinners_;
    std::vector<rules::Kickback> kickbacks_;
    rules::TiltCounter tilt_counter_;
    bool tilt_nudge_active_;
    bool next_serve_saved_,unlimited_balls_,cheats_used_;
    runtime::Tick rank_flash_started_,rank_flash_duration_;
    unsigned rank_flash_index_;
    std::size_t hyperspace_kickout_,gravity_kickout_;
    bool gravity_active_;
    std::map<std::size_t,rules::MissionEvent> mission_events_;
    runtime::Tick mission_started_;
    bool finite_round_;
    runtime::Tick time_,sink_wait_;
    std::size_t captured_index_;
    bool seated_,suspended_,captured_,captured_kickout_,captured_hole_;
    bool hidden_test_;
    runtime::Tick test_eject_remaining_;
    float test_stick_x_,test_stick_y_;
    std::uint32_t random_state_;
    std::uint32_t launches_,returns_,contacts_,exhausted_,flipper_contacts_,nudges_,moving_exhausted_,sink_captures_,sink_ejections_,kickout_captures_,kickout_ejections_,hole_captures_,hole_landings_,ramp_transitions_;
    std::uint32_t bumper_hits_,rebound_hits_,target_hits_,bank_completions_;
    std::uint32_t lane_hits_,lane_completions_,medal_hits_,medal_completions_;
    float random_unit();
    float captured_height_;
    std::uint32_t captured_mask_;
    float drain_y_; int table_width_,table_height_;
    std::vector<audio::SoundCue> sound_cues_;
    std::uint32_t sound_drops_;
    std::size_t rank_sound_,mission_sound_,drain_sound_;
    std::map<unsigned,std::size_t> event_sounds_;
    void feed_ball();
    void eject_capture();
    physics::Vec2 throw_velocity(physics::Vec2 direction,float angle,float speed,float variation);
    void apply_nudge(const reference::NudgeDelta& delta);
    void step(runtime::Tick duration);
    physics::Vec2 test_velocity(runtime::Tick duration) const;
    void expire_components(runtime::Tick now);
    runtime::Tick component_boundary(runtime::Tick now) const;
    void component_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration);
    void update_rules(runtime::Tick now);
    void sync_target_colliders();
    void target_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration);
    void award_group(std::size_t group);
    void rollover_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration);
    void expire_rollovers(runtime::Tick now);
    runtime::Tick rollover_boundary(runtime::Tick now) const;
    std::size_t bumper_bank(std::size_t group) const;
    void fire_bumper_groups(std::uint32_t mask,runtime::Tick now);
    void reset_progression(runtime::Tick now);
    rules::MissionContext mission_context() const;
    void mission_event(const rules::MissionEvent& event,runtime::Tick now);
    void mission_group(std::size_t group,runtime::Tick now);
    void ordinary_points(std::uint32_t base);
    void advanced_actions(const rules::AdvancedActions& action,runtime::Tick now);
    void sync_advanced_colliders();
    void advanced_target_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration);
    void advanced_sensor_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration);
    void spinner_events(std::size_t spinner,const rules::SpinnerEvents& events,runtime::Tick now);
    std::uint32_t source_mission_state() const;
    void kickback_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration);
    void kickback_events(std::size_t owner,const rules::KickbackEvents& events,runtime::Tick now);
    void update_tilt(runtime::Tick now);
    void settle_drain(runtime::Tick now,runtime::Tick inactive=0);
    void cue(std::size_t group,audio::SoundRole role,runtime::Tick now);
    void control_cue(unsigned number,runtime::Tick now);
};
}
}
#endif


