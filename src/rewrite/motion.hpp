#ifndef PINBALL_REWRITE_MOTION_HPP
#define PINBALL_REWRITE_MOTION_HPP

#include "collision2d.hpp"
#include "material_response.hpp"
#include "flipper.hpp"
#include "runtime.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pinball {
namespace motion {

using physics::Vec2;
using runtime::Tick;

struct SurfacePlane {
    bool active;
    Vec2 gradient;
    float offset;
    Vec2 field_force;
    float drag;
    std::size_t id; // Default is max(size_t), meaning no selected plane.
    SurfacePlane();
};
struct SurfaceTransition {
    std::uint32_t target_mask;
    SurfacePlane plane;
    bool set_height;
    float height_offset;
    SurfaceTransition();
};

// Caller-supplied development parameters; no XP dynamics or table geometry
// are implied. Distance units are arbitrary, velocity is units/second.
struct BallState {
    Vec2 position;
    Vec2 velocity;
    float radius;
    std::uint32_t collision_mask;
    float height;
    SurfacePlane plane;
    BallState(Vec2 position_value, Vec2 velocity_value, float radius_value);
};

enum class ColliderKind { Segment, Disk };
struct StaticCollider {
    ColliderKind kind;
    Vec2 first;
    Vec2 second;
    float radius;
    float restitution;
    bool two_sided;
    bool reference_response;
    // Terminal event. One-sided segment gates accept only incoming motion,
    // including initially overlapping starts; outgoing/tangent motion escapes.
    bool captures_ball; // Ordinary factories default false.
    bool center_trigger; // Disk sensor tests ball center, without ball-radius inflation.
    bool enabled; // Factories default true; disabled geometry is still validated.
    std::uint32_t collision_mask; // Factories default to all mask bits.
    bool center_crossing; // Directed point/line event, without radius or endcaps.
    SurfaceTransition transition;
    response::Material material;
    // On strict pre-response threshold activation, use threshold1e9 for
    // subsequent contacts with this collider in this step only. Caller owns
    // persistent cooldown/rearming; factories leave this policy disabled.
    bool latch_response;
    bool applies_transition; // Center crossing updates mask/plane only when true.
    std::size_t response_owner; // max(size_t) means only this collider.
    bool deactivate_on_activation; // Local to this solve; all matching owner shapes drop.
    bool directional_approach_only; // Explicit one-way solid: no outgoing/tangent overlap response.
    static StaticCollider segment(Vec2 start, Vec2 end, float restitution,
                                  bool two_sided = true);
    static StaticCollider disk(Vec2 center, float radius, float restitution);
    // Positive finite trigger radius. Terminal sensor: no force or separation
    // offset, and initial containment captures without moving the center.
    static StaticCollider center_capture(Vec2 center, float trigger_radius);
    // Directed point event preserving velocity, mask, selected plane and height.
    // Front is CCW (-dy,dx); no ball-radius/endcap inflation.
    static StaticCollider event_segment(Vec2 start, Vec2 end, std::uint32_t listen_mask);
    // Front is CCW (-dy,dx); only movement from front toward back crosses.
    static StaticCollider transition_segment(Vec2 start, Vec2 end,
        std::uint32_t listen_mask, const SurfaceTransition& target);
};

struct MotionConfig {
    Vec2 acceleration;
    Tick max_step;
    std::size_t max_contacts;
    float separation_slop;
    float maximum_query_speed; // Zero disables; clamp before each movement query.
    std::uint32_t moving_collision_mask; // Default ground bit 1; zero disables movers.
    MotionConfig(Vec2 acceleration_value, Tick max_step_value,
                 std::size_t max_contacts_value, float separation_slop_value,
                 float maximum_query_speed_value = 0,
                 std::uint32_t moving_collision_mask_value = 1);
};
struct MotionContact {
    std::size_t collider_index;
    double seconds_from_start;
    physics::SweepHit hit;
    bool moving; // collider_index addresses moving list when true.
    Vec2 surface_velocity;
    bool boost_applied;
    bool transitioned; // Surface event, no bounce/boost; counts toward contact budget.
    float incident_normal_speed; // Pre-response normal speed; zero for sensors/transitions.
    bool activated; // Static reference response with incident STRICTLY>current threshold.
    bool triggered; // Nonphysical center crossing, with or without a transition.
    float incoming_speed; // Total ball speed immediately before this event/response.
};
struct FlipperYield { std::size_t collider_index; double seconds_from_start; };
struct MotionStep {
    double simulated_seconds;
    double discarded_seconds;
    bool contact_budget_exhausted;
    std::vector<MotionContact> contacts;
    bool moving_work_exhausted;
    std::size_t moving_iterations;
    std::vector<FlipperYield> flipper_yields;
    bool captured;
    std::size_t capture_collider_index;
};

class BallStepper {
public:
    explicit BallStepper(const MotionConfig& config);
    // The host supplies fixed steps (normally from FixedStepScheduler). A
    // duration in [0,max_step] is accepted; zero is a validated no-op.
    // If enabled, the query speed limit is applied before each remaining-time
    // search, including the search after a powered response. An impact at the
    // step endpoint can retain over-limit velocity until the next query.
    // Free flight integrates constant acceleration exactly. Each contact
    // search sweeps the straight chord of the remaining ballistic interval,
    // then resolves its impact velocity and repeats for the remainder.
    // Curved collision paths/times are therefore a step-size approximation,
    // not exact accelerated CCD. Smaller caller steps reduce this error.
    // A terminal static capture zeros velocity and returns inactive remainder;
    // it never rebounds or applies a boost. It may record one terminal event
    // beyond the response contact budget. Capture is not budget exhaustion.
    // Equal float hit times use collider input order. On contact exhaustion,
    // remaining time is explicitly discarded at the last resolved position;
    // it is never advanced without another sweep. All changes are committed
    // only after success. Invalid/nonfinite values and float overflow throw.
    MotionStep step(BallState& ball, Tick duration,
                    const std::vector<StaticCollider>& colliders) const;
    // Snapshots begin at this step's time zero. Searches split at kinematic
    // phase ends; static colliders win equal-time ties, then moving-list order.
    // Relative surface velocity transfers rotation/length-change momentum.
    // If work/contact budget exhausts, simulated_seconds is the last validated
    // world time. Do NOT advance external flippers over discarded_seconds;
    // freeze/cancel them at the validated pose before resuming the world.
    MotionStep step(BallState& ball, Tick duration,
                    const std::vector<StaticCollider>& colliders,
                    const std::vector<flipper::FlipperMotion>& moving,
                    const flipper::MovingSweepConfig& sweep_config) const;
    const MotionConfig& config() const;
private:
    MotionConfig config_;
};

struct PlungerConfig {
    Tick pull_duration;   // Time from zero to full pull.
    Tick return_duration; // Time for a full-stroke visual return.
    Vec2 launch_direction;
    float minimum_speed; // Velocity change, not force; no mass is assumed.
    float maximum_speed;
    float minimum_pull;  // Gameplay releases below this fraction do not fire.
    PlungerConfig(Tick pull, Tick returning, Vec2 direction,
                  float minimum, float maximum, float threshold);
};
enum class PlungerPhase { Idle, Pulling, Returning };
struct LaunchEvent {
    bool fired;
    float pull;
    Vec2 delta_velocity;
    LaunchEvent();
};

class TimedPlunger {
public:
    explicit TimedPlunger(const PlungerConfig& config, Tick now = 0);
    void update(Tick now);
    // Only a Physical down->up edge can return a launch, and only if seated
    // at release, above threshold and with positive configured speed. Repeats
    // cannot fire. The caller applies this returned event once; no event is
    // queued for a later step. Repress during return continues from its pose.
    // Nonphysical transitions cancel; use cancel for pause/focus/disconnect
    // even if the input backend emits no logical edge. A fresh press is then
    // required. Do not feed physical releases while gameplay is suspended.
    LaunchEvent set_pressed(bool pressed, Tick now, bool ball_seated,
        runtime::TransitionCause cause = runtime::TransitionCause::Physical);
    void cancel(Tick now); // Clears held state, charge and visual return.
    void reset(Tick now = 0); // Explicit fresh epoch, including backward reset.
    float pull() const;
    bool held() const;
    PlungerPhase phase() const;
    Tick time() const;
private:
    PlungerConfig config_;
    Tick last_time_;
    Tick phase_start_;
    double phase_pull_;
    double pull_;
    bool held_;
    PlungerPhase phase_;
};

} // namespace motion
} // namespace pinball
#endif
