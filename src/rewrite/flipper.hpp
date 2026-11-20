#ifndef PINBALL_REWRITE_FLIPPER_HPP
#define PINBALL_REWRITE_FLIPPER_HPP
#include "collision2d.hpp"
#include "material_response.hpp"
#include "runtime.hpp"
#include <cstddef>

namespace pinball {
namespace flipper {
using physics::Vec2;
using runtime::Tick;

enum class ResponseMode { PhysicalSurface, ReferenceMaterial };
// Explicit parameters. Tip vectors
// are relative to the pivot. The shape is the convex hull of the two end
// disks. Shortest-angle rotation and linear tip-length interpolation reach
// both supplied endpoints, even when their lengths differ slightly.
struct FlipperConfig {
    Vec2 pivot, rest_tip_vector, raised_tip_vector;
    float pivot_radius, tip_radius;
    Tick raise_duration, return_duration;
    float restitution;
    float smoothness, impulse_gain;
    ResponseMode response_mode;
    FlipperConfig(Vec2 pivot_value, Vec2 rest_tip, Vec2 raised_tip,
                  float base_radius, float end_radius, Tick raising,
                  Tick returning, float bounce, float smoothing = 1,
                  float gain = 0, ResponseMode policy = ResponseMode::PhysicalSurface);
};
struct FlipperPose {
    Vec2 pivot, tip;
    float pivot_radius, tip_radius;
    double normalized, angle, angular_velocity, length_velocity;
};
struct SurfaceSample {
    double separation; // Signed separation of the point from the hull.
    Vec2 point, normal, velocity;
};

class FlipperMotion {
public:
    FlipperMotion(const FlipperConfig& config, double normalized_pose,
                  double normalized_velocity);
    FlipperPose pose(double seconds = 0) const;
    SurfaceSample surface(Vec2 point, double seconds) const;
    SurfaceSample surface(double point_x, double point_y, double seconds) const;
    double duration() const; // Seconds until the current phase clamps; 0 if still.
    double speed_bound() const;
    float restitution() const;
    bool reference_response() const;
    bool reference_response_enabled() const;
    FlipperMotion frozen_at(double seconds) const; // Constant-pose snapshot.
    // point is the physical hull surface, not the ball center. Reference mode
    // reconstructs the ball center with normal*ball_radius, applies passive
    // E/S attenuation or front/back/base-limited angular boost, and ignores
    // infinite-mass surface reflection. PhysicalSurface remains the default.
    response::CollisionResult respond(Vec2 incoming,Vec2 normal,Vec2 point,
                                     float ball_radius,double motion_time) const;
private:
    FlipperConfig config_;
    double start_, rate_, angle_, angle_delta_, length_, length_delta_, duration_, speed_;
};

class TimedFlipper {
public:
    explicit TimedFlipper(const FlipperConfig& config, Tick now = 0);
    void update(Tick now);
    void set_pressed(bool pressed, Tick now,
        runtime::TransitionCause cause = runtime::TransitionCause::Physical);
    // Pause/focus/disconnect cancellation freezes the current pose, clears
    // held state and removes future motion. It never teleports the shape or
    // generates an impulse. A fresh physical press starts another cycle.
    void cancel(Tick now);
    // Explicit host resume policy: return from the frozen pose without a
    // jump or synthetic press. Return contacts occur only in a later step.
    void resume_released(Tick now);
    // Independent yield approximation: commit only the contact pose, then
    // resume the same held/direction/rate from end_time on the next step.
    // This omits the reference driver's small collision-angle retreat.
    void commit_yield(Tick contact_time,Tick end_time);
    void reset(Tick now = 0);
    double pose() const;
    bool held() const;
    Tick time() const;
    FlipperMotion motion() const; // Snapshot BEFORE advancing the ball/world.
private:
    FlipperConfig config_;
    Tick last_time_, phase_start_;
    double phase_pose_, pose_;
    int direction_;
    bool held_;
};

struct MovingSweepConfig {
    std::size_t max_iterations, max_total_iterations;
    double distance_tolerance, time_tolerance;
    MovingSweepConfig(std::size_t per_query, std::size_t per_step,
                      double distance_error, double time_error);
};
struct MovingSweep {
    physics::SweepHit hit;
    Vec2 surface_velocity;
    double time; // Seconds from the start of this query (not global epoch).
    std::size_t iterations;
    bool work_budget_exhausted;
    MovingSweep();
};
// Conservative advancement against the actual rotating tapered hull, not a
// static-pose sample. Ball trajectory is linear within this query. Contacts
// may be early by the explicit spatial/time tolerance. A global speed bound
// prevents jumping over fast tip motion or a stationary ball. Work exhaustion
// is NOT a miss: the caller must discard/uncommit the unvalidated interval.
MovingSweep sweep_circle(Vec2 position, Vec2 velocity, float ball_radius,
                         const FlipperMotion& motion, double motion_start,
                         double max_seconds, const MovingSweepConfig& config,
                         std::size_t available_iterations);

} // namespace flipper
} // namespace pinball
#endif
