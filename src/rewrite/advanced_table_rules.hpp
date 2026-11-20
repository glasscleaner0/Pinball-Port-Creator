#ifndef PINBALL_REWRITE_ADVANCED_TABLE_RULES_HPP
#define PINBALL_REWRITE_ADVANCED_TABLE_RULES_HPP
#include "runtime.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace pinball { namespace rules {

struct AdvancedRulesConfig {
    std::array<runtime::Tick,12> fuel_durations;
    std::array<runtime::Tick,5> hyperspace_delays; // Actual resolved WAV durations.
    runtime::Tick hyperspace_reflex_delay,hyperspace_combined_delay,hyperspace_special_delay;
    std::array<std::uint32_t,5> hyperspace_awards;
    std::array<std::uint32_t,6> skill_awards;
    runtime::Tick target_duration,booster_rearm,spot_flash,destination_flash,feature_duration;
    runtime::Tick extra_on,extra_flash,hyper_flash,hyper_decay,skill_flash,reflex_window,fuel_pulse,flash_off,flash_on;
    std::uint32_t reflex_points,bonus_cap,jackpot_cap;
    // Source-observed constants; unresolved sound durations start zero.
    // Hyperspace rejects a selected unresolved duration before changing state.
    AdvancedRulesConfig();
};
enum class AdvancedFeature {
    Flags, Jackpot, Bonus, BonusHold, ExtraBall, BonusLane, ReturnLanes,
    Blocker, HyperspaceReflex, HyperspaceCombined, HyperspaceSuper
};
struct AdvancedLightState {bool visible;std::size_t frame;};
struct AdvancedActions {
    bool accepted,ordinary_recorded; // Skip caller pool recording when already accounted in event order.
    std::uint32_t ordinary_points,special_points;
    bool extra_ball,replay,rearm_booster,enable_blocker,enable_gravity_well;
    bool set_multiplier_max,set_medals_max,fuel_empty,bonus_collect;
    std::uint32_t flag_hits; // bit0/bit1 are actual spinner impacts.
    int wormhole_destination; // -1 none, otherwise zero-based sink index.
    runtime::Tick hyperspace_delay;
    unsigned sound_control; // Named soundwave number;49 selects soundwave49D,0 is silent.
    AdvancedActions();
};
struct AdvancedSnapshot {
    std::array<bool,23> target_enabled,target_visible,target_fired;
    std::array<runtime::Tick,23> target_remaining; // Source target number index.
    std::array<bool,6> fuel_lamps,skill_lamps;
    std::array<bool,3> fuel_spots,mission_lamps;
    std::array<std::array<bool,3>,2> hazard_lamps;
    std::array<bool,4> hyperspace_lamps;
    std::array<bool,3> wormhole_lamps,wormhole_arrows;
    std::array<bool,2> gates_open,return_lamps;
    std::uint32_t mission_mask;
    std::array<std::uint32_t,2> hazard_masks;
    int fuel_index;
    bool fuel_countdown,fuel_empty,skill_armed;
    bool flags_active,jackpot_active,bonus_active,bonus_hold,extra_ball_lit,blocker_on,booster_lit;
    std::uint32_t bonus_pool,jackpot_pool,wormhole_destination,hyperspace_level,wormhole_frame;
};

// Independent pure rules for the supported default source controls.
// No geometry, files, audio playback, score ledger or mission execution.
// Call record_ordinary(BASE points), once, for every ordinary score path.
// Actions with ordinary_recorded already accounted their ordered pool effects.
// Specials bypass pool accumulation. Player multiplication is caller-owned.
// Contacts process timers strictly earlier; advance() includes equal-time
// expiry. Pause freezes this clock. Protected drains preserve this state.
class AdvancedTableRules {
public:
    explicit AdvancedTableRules(const AdvancedRulesConfig& config,runtime::Tick now=0);
    void reset(runtime::Tick now=0);
    AdvancedActions serve(bool saved,runtime::Tick now);
    AdvancedActions ordinary_loss(runtime::Tick now);
    AdvancedActions advance(runtime::Tick now);
    AdvancedActions target(std::size_t number,runtime::Tick now);
    AdvancedActions rollover(std::size_t number,runtime::Tick now);
    AdvancedActions skill_gate(runtime::Tick now);
    AdvancedActions skill_tripwire(std::size_t number,runtime::Tick now);
    AdvancedActions skill_award(runtime::Tick now);
    AdvancedActions skill_clear(runtime::Tick now);
    AdvancedActions flag_hit(std::size_t index,bool completed_turn,runtime::Tick now);
    AdvancedActions wormhole(std::size_t index,runtime::Tick now);
    AdvancedActions hyperspace(runtime::Tick now,bool reflex_active=false);
    AdvancedActions enable(AdvancedFeature feature,runtime::Tick now);
    AdvancedActions set_gate(std::size_t index,bool open,runtime::Tick now);
    AdvancedActions close_gate(std::size_t index,runtime::Tick now);
    void mission_hyperspace(bool combined,bool super);
    void consume_mission_selection(); // Reset only selection bits, preserving lamp animations.
    void mission_flags(bool enabled); // Only lite19/20; independent of booster lamp61.
    void record_ordinary(std::uint32_t base);
    void set_mission_state(std::uint32_t state);
    void clear_spots(std::uint32_t families=15); // Fuel1, mission2, left4, right8.
    void request_booster_rearm(runtime::Tick now);
    AdvancedSnapshot snapshot() const;
    // Original named light number (lite19 etc), not DAT group number.
    AdvancedLightState light(std::uint32_t number) const;
    runtime::Tick until_boundary() const;
    runtime::Tick time() const;
private:
    struct Lamp {
        bool on,flashing,timed,after_on,starts_on;
        runtime::Tick started,duration,flash_started;
        Lamp():on(false),flashing(false),timed(false),after_on(false),starts_on(false),started(0),duration(0),flash_started(0) {}
    };
    AdvancedRulesConfig config_;
    runtime::Tick now_,fuel_started_,hyper_started_,booster_started_;
    int fuel_index_;
    bool fuel_countdown_,fuel_full_,fuel_empty_,booster_pending_,skill_armed_,skill_flashing_;
    runtime::Tick skill_started_;
    std::array<bool,6> skill_on_,skill_flash_mask_;
    std::array<Lamp,23> targets_;
    std::array<bool,3> booster_raised_;
    std::array<Lamp,3> fuel_spots_,mission_lamps_;
    std::array<std::array<Lamp,3>,2> hazards_;
    std::array<Lamp,6> fuel_pulses_;
    std::array<bool,6> fuel_flash_cancelled_;
    std::array<Lamp,4> hyper_lamps_,features_;
    std::array<Lamp,2> return_lamps_,gate_lamps_,gate_secondary_,flag_lamps_;
    Lamp extra_,blocker_,bonus_lane_,destination_lamp_,hyper_reflex_,hyper_combined_,hyper_super_;
    std::array<Lamp,3> worm_lamps_,worm_arrows_;
    std::uint32_t mission_mask_,mission_state_,hyper_level_,destination_,worm_frame_,bonus_pool_,jackpot_pool_;
    std::array<std::uint32_t,2> hazard_masks_;
    std::array<bool,2> gates_open_;
    bool hyper_decay_,bonus_accum_,jackpot_accum_,mission_flags_;
    void validate_now(runtime::Tick now) const;
    bool visible(const Lamp& lamp) const;
    bool active(const Lamp& lamp) const;
    void on(Lamp& lamp,runtime::Tick duration=0);
    void flash(Lamp& lamp,runtime::Tick duration,bool then_on);
    void off(Lamp& lamp);
    runtime::Tick next_boundary(bool& pending) const;
    AdvancedActions update(runtime::Tick now,bool inclusive);
    void expire_at_now(AdvancedActions& actions);
    void refill(int index);
    void destination_advance(bool force);
    void feature_enable(AdvancedFeature feature);
    void gate_set(std::size_t index,bool open);
    void validate_config() const;
};
} }
#endif





