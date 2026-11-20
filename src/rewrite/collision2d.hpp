#ifndef PINBALL_REWRITE_COLLISION2D_HPP
#define PINBALL_REWRITE_COLLISION2D_HPP

namespace pinball {
namespace physics {

struct Vec2 {
    float x;
    float y;
    Vec2(float x_value = 0.0f, float y_value = 0.0f);
};
Vec2 add(Vec2 a, Vec2 b);
Vec2 subtract(Vec2 a, Vec2 b);
Vec2 multiply(Vec2 value, float scale);
float dot(Vec2 a, Vec2 b);
float length(Vec2 value);
Vec2 unit(Vec2 value);

struct SweepHit {
    bool hit;
    bool initial_overlap;
    float time;
    float penetration;
    Vec2 point;
    Vec2 normal;
    SweepHit();
};

// Constant-velocity swept disk against a stationary disk. Time uses the same
// units as velocity and max_time. Zero obstacle radius represents an endpoint.
// Initial penetration is reported even without movement. A merely touching
// disk is reported only when its velocity points into the contact.
SweepHit sweep_circle_circle(Vec2 position, Vec2 velocity, float radius,
                             Vec2 obstacle, float obstacle_radius,
                             float max_time);

// Sweep against a finite capsule: the segment expanded by the moving radius.
// The segment's front normal is (-dy,dx)/length. With two_sided=false, only
// front-facing contacts are eligible. Zero-length segments become endpoints.
SweepHit sweep_circle_segment(Vec2 position, Vec2 velocity, float radius,
                              Vec2 start, Vec2 end, float max_time,
                              bool two_sided = true);

// Resolve only the incoming normal velocity; the tangent is preserved.
// Restitution is in [0,1]; moving obstacles can be represented by subtracting
// their contact velocity before this call and adding it back afterwards.
Vec2 contact_velocity(Vec2 incoming, Vec2 normal, float restitution);

} // namespace physics
} // namespace pinball
#endif
