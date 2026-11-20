#ifndef PINBALL_REWRITE_REFERENCE_TABLE_HPP
#define PINBALL_REWRITE_REFERENCE_TABLE_HPP

#include "table_model.hpp"
#include "motion.hpp"
#include "flipper.hpp"
#include "material_response.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pinball {
namespace reference {

// Source-behavior specification: loader material301/302 and kicker401/402.
// Missing links retain S=.95/E=.6/threshold~9e10/boost0. Sound and throw
// attributes are validated but ignored here; TableModel retains all metadata.
response::Material material_for_group(const table::TableModel& model,
                                      std::size_t group_index);

// Visual attribute 602 is a BIT POSITION; repeated attributes OR together.
// With no 602 attributes the source defaults to mask 1 (the ground layer).
std::uint32_t collision_mask_for_group(const table::TableModel& model,
                                       std::size_t group_index);

// Default unlit wormhole metadata for v_sink1/2/3 only. Direction is the raw
// DAT vector, NOT normalized: scalar speed35 with direction(1,+/-1) gives
// nominal Cartesian speed35*sqrt(2). Capture routing for lights/missions,
// scoring, multiball locks and v_sink7 is outside this bounded profile.
struct SinkProfile {
    std::size_t group, collider_index;
    physics::Vec2 position, direction;
    float angle_multiplier, speed, speed_variation;
    runtime::Tick delay;
};

// Default active black-hole kickout only. The radial field adds to the table
// field; capture_radius tests the ball CENTER without adding ball radius.
// Session owns field application, hold/eject/cooldown and held-Z presentation.
struct KickoutProfile {
    std::size_t group, collider_index;
    physics::Vec2 center, direction;
    float field_radius, capture_radius, field_strength, angle_multiplier;
    float speed, speed_variation, held_z;
    runtime::Tick delay, cooldown;
};

enum class HitComponentKind { Bumper, Rebound };
// Default level only: bumper idle0/fired1; rebound idle-1 (hidden)/fired0.
// Material is the immutable original response. Activation is strictly greater
// than its threshold; response boost itself uses the separate inclusive rule.
// Session owns source-style image expiry and bumper threshold suppression.
struct ComponentProfile {
    std::size_t group;
    std::vector<std::size_t> collider_indices;
    HitComponentKind kind;
    int idle_frame;
    std::size_t fired_frame;
    runtime::Tick duration;
    response::Material material;
};

struct RampPlaneProfile {
    std::size_t group, local_index, id;
    physics::Vec2 vertices[3];
    motion::SurfacePlane surface;
};
struct HoleProfile {
    std::size_t group, collider_index;
    physics::Vec2 center;
    float field_radius, capture_radius, field_strength;
    std::uint32_t collision_mask, landing_mask;
    float landing_z, fall_acceleration;
    runtime::Tick delay;
};

// Bounded source-default awards and single-player flow metadata. Award keys
// identify owners, not arbitrary contacts: Session awards only an activated
// component, capture, or the named ramp score-line event. A plane transition
// and hole landing have no direct default award.
struct RuleProfile {
    std::map<std::size_t,std::uint32_t> event_group_awards;
    std::size_t drain_group, drain_collider_index;
    std::size_t skill_gate_group, skill_gate_collider_index;
    std::size_t ramp_score_group, ramp_score_collider_index;
    std::size_t skill_clear_group, skill_clear_collider_index;
    runtime::Tick drain_delay, feed_delay, shoot_again_on, shoot_again_flash, reflex_duration;
    std::uint32_t balls_per_game, initial_bonus, subsequent_bonus, score_wrap, reflex_award;
    RuleProfile(); // Safe initialized metadata; TablePhysics fills DAT owners.
};

// One source multiplier drop bank only. A down popup target is hidden and all
// its collision edges are inactive until a controller-requested rearm expires.
struct TargetProfile {
    std::size_t group;
    std::vector<std::size_t> collider_indices;
    response::Material material;
    int idle_frame, down_frame;
    runtime::Tick rearm_delay;
};
struct BankLampProfile {
    std::size_t group, on_frame;
    runtime::Tick off_delay, on_delay;
};
struct TargetBankProfile {
    std::array<TargetProfile,3> targets;
    std::array<BankLampProfile,4> lamps; // Declared light-group order.
    std::size_t light_group;
    std::uint32_t hit_award, complete_award;
    std::array<std::uint32_t,5> multipliers;
    runtime::Tick flash_duration, decay_period;
    TargetBankProfile();
};

// Medal stages share drop-target rearm mechanics but do not change the score
// coefficient. The third and later completions grant one extra-ball credit;
// completion_awards[2] is zero, not an additional point award.
struct MedalBankProfile {
    std::array<TargetProfile,3> targets;
    std::array<BankLampProfile,3> lamps;
    std::size_t light_group;
    std::uint32_t hit_award;
    std::array<std::uint32_t,3> completion_awards;
    std::uint32_t extra_ball_level;
    runtime::Tick flash_duration, decay_period;
    MedalBankProfile();
};

// Rollover contact regions use directed, unoffset ball-center crossings.
// Entry/exit lists refer to opposite polygon windings; Session owns the
// inside latch, source 100 ms rearm and frame visibility, never wall response.
struct RolloverProfile {
    std::size_t group;
    std::vector<std::size_t> entry_indices, exit_indices;
    int idle_frame, active_frame;
    runtime::Tick rearm_delay;
    runtime::Tick exit_hide_delay; // Only the light rollover retains its hit image after exit.
};
struct LaneGroupProfile {
    std::size_t light_group, bumper_group;
    std::array<std::size_t,3> rollover_groups; // Same order as lamps.
    std::array<BankLampProfile,3> lamps;
    std::vector<std::size_t> bumper_groups;
    std::array<std::uint32_t,4> bumper_scores;
    std::uint32_t rollover_award;
    runtime::Tick completion_flash, decay_period;
    LaneGroupProfile();
};

enum class AdvancedTargetKind { Booster, Fuel, Mission, LeftHazard, RightHazard, WormholeDestination };
struct AdvancedTargetProfile {
    TargetProfile body;
    AdvancedTargetKind kind;
    std::size_t number, index, lamp_group; // Original a_targ number; family-local index.
    std::uint32_t score;
};
enum class AdvancedRolloverKind { Outlane, Bonus, Return, Fuel, SpaceWarp };
struct AdvancedRolloverProfile {
    std::size_t region_index, number; // Index into TablePhysics.rollovers; source a_roll number.
    AdvancedRolloverKind kind;
    std::size_t lamp_group; // max size_t if the control has no single associated lamp.
    std::uint32_t score, lit_score, fuel_level;
};
struct SkillTripwireProfile {
    std::size_t group, collider_index, lamp_group, number;
};
struct DeploymentGateProfile {
    std::size_t group, number, collider_index;
};
struct FuelProfile {
    bool present;
    std::size_t group;
    std::array<BankLampProfile,6> lamps;
    std::array<runtime::Tick,12> level_durations;
    FuelProfile();
};
struct SoundReference {
    std::size_t group, resource_group;
    std::string filename; // DAT reference only; this loader does not open WAV files.
    SoundReference();
};
struct FlagSpinnerProfile {
    std::size_t group, number, forward_index, reverse_index, image_count;
    float minimum_speed, maximum_speed, speed_scale, speed_decay;
    std::uint32_t score, powered_score;
    int idle_frame;
    bool default_enabled;
};
enum class DynamicWallKind { Gate, Blocker };
struct DynamicWallProfile {
    std::size_t group;
    std::vector<std::size_t> collider_indices;
    DynamicWallKind kind;
    int closed_frame, open_frame;
    bool default_enabled;
    response::Material material;
    std::array<std::size_t,2> lamp_groups; // max size_t marks an unused slot.
    runtime::Tick initial_duration, extended_duration;
};
struct GravityWellProfile {
    KickoutProfile capture;
    SoundReference release_sound;
    std::size_t lamp_group;
    bool default_enabled, reactivate_after_cooldown;
    std::uint32_t score;
    GravityWellProfile();
};

struct KickbackProfile {
    std::size_t group, number, collider_index, gate_group;
    response::Material material; // Initial threshold1e9; retain source boost55.
    float armed_threshold;
    runtime::Tick arm_delay, pulse_period;
    int idle_frame, fired_frame, reset_frame;
    bool default_enabled;
};
struct OneWayProfile {
    std::size_t group, number, solid_index, pass_event_index;
    response::Material material;
    bool default_enabled;
};

// Additive feature facts. New bodies/events start disabled until their Session
// rules are connected. Sound-dependent capture delay is deliberately zero;
// the caller must resolve the referenced WAV duration before enabling it.
struct AdvancedTableProfile {
    std::vector<AdvancedTargetProfile> targets;
    std::vector<AdvancedRolloverProfile> rollovers;
    std::vector<SkillTripwireProfile> skill_tripwires;
    std::vector<DeploymentGateProfile> deployment_gates;
    FuelProfile fuel;
    bool has_hyperspace;
    KickoutProfile hyperspace;
    std::size_t hyperspace_light_group;
    std::array<BankLampProfile,4> hyperspace_lamps;
    std::array<std::uint32_t,5> hyperspace_awards;
    std::array<SoundReference,5> hyperspace_sound_refs;
    SoundReference hyperspace_reflex_sound, hyperspace_combined_sound, hyperspace_special_sound;
    std::vector<BankLampProfile> lights; // Only named dependencies of supported controls.
    std::vector<FlagSpinnerProfile> spinners;
    std::vector<DynamicWallProfile> dynamic_walls;
    bool has_gravity_well;
    GravityWellProfile gravity_well;
    std::vector<KickbackProfile> kickbacks;
    std::vector<OneWayProfile> one_ways;
    bool has_escape_chute;
    SinkProfile escape_chute; // No award, no ball loss; return via its own two-second sink timer.
    AdvancedTableProfile();
};

// An independent DAT-only profile loader. Runtime opens only DAT resources.
// All object 1000/1010 geometry retains source 602 masks; per-ball queries select
// the current layer. Source clockwise fronts are represented by reversing
// endpoints for our CCW-front primitive. Ramp edges are nonphysical center
// crossings. Full rules, other component roles and exact corner construction
// remain outside this bounded profile. Evidence: work/physics-v6-ramp/.
struct TablePhysics {
    std::size_t table_group, ball_group, plunger_group;
    float ball_radius;
    physics::Vec2 feed_position;
    // A horizontal-striker settling target, not the source's feed position.
    physics::Vec2 seat_position;
    std::size_t plunger_image_count;
    response::FieldConfig field;
    std::vector<motion::StaticCollider> static_colliders;
    std::vector<std::size_t> collider_groups; // Same indices as static_colliders.
    std::vector<std::size_t> ground_excluded_groups;
    std::size_t ground_excluded_collider_count;
    std::size_t plunger_collider_index;
    std::array<std::size_t,2> flipper_groups;
    std::array<flipper::FlipperConfig,2> flippers; // a_flip1 then a_flip2.

    std::vector<SinkProfile> sinks; // v_sink1, v_sink2, v_sink3, in that order.
    std::vector<KickoutProfile> kickouts; // Only default-active a_kout3.
    std::vector<RampPlaneProfile> ramp_planes;
    std::vector<std::size_t> ramp_transition_indices;
    std::vector<HoleProfile> holes; // Only ramp_hole 104.
    std::vector<ComponentProfile> components; // Default bumpers and four rebound walls.
    RuleProfile rules;
    TargetBankProfile multiplier_bank;
    std::vector<RolloverProfile> rollovers; // Six lanes first; additive regions follow.
    std::array<LaneGroupProfile,2> lane_groups;
    MedalBankProfile medal_bank;
    AdvancedTableProfile advanced;

    explicit TablePhysics(const table::TableModel& model);
};

} // namespace reference
} // namespace pinball
#endif
