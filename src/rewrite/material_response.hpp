#ifndef PINBALL_REWRITE_MATERIAL_RESPONSE_HPP
#define PINBALL_REWRITE_MATERIAL_RESPONSE_HPP
#include "collision2d.hpp"
namespace pinball {
namespace response {
using physics::Vec2;

// Caller-supplied behavioral parameters. The closed-form velocity policy was
// specified from authorized reference-source study; no source implementation
// or original types are incorporated. This is distinct from the generic
// tangential-preserving physical contact_velocity helper.
struct Material {
    float elasticity, smoothness, threshold, boost;
    Material(); // Neutral default keeps generic collider value-initialization valid.
    Material(float elasticity_value,float smoothness_value,
             float threshold_value,float boost_value);
};
struct CollisionResult {
    Vec2 velocity;
    float incident_normal_speed; // Absolute pre-response normal component.
    bool threshold_met;         // Inclusive >=; a negative threshold is valid.
    bool boost_applied;         // Threshold met and nonzero boost.
};
// Outward normal may have any finite nonzero length; it is normalized here.
// Incoming velocity changes direction using E/S-weighted normal/tangent;
// total speed is attenuated by incidence, then eligible boost is ADDED along
// the normal. An outgoing input is attenuated without reflecting. A zero
// weighted direction absorbs the velocity. There is no collision speed cap:
// the caller applies ordinary field refresh/capping at the proper boundary.
CollisionResult collide(Vec2 velocity,Vec2 normal,const Material& material);

struct FieldConfig {
    Vec2 gravity;
    float drag_per_second, lateral_noise_per_speed, maximum_speed;
    FieldConfig(Vec2 gravity_value,float drag,float noise,float limit);
};
// Deterministic Euler field refresh: gravity, velocity-proportional drag and
// X-only speed-scaled noise, followed by speed limiting. random_unit is an
// explicit [-1,1] sample; this module owns no RNG or clock. Zero seconds still
// applies the speed limit. Coarse caller steps retain Euler's behavior.
Vec2 advance_field(Vec2 velocity,double seconds,const FieldConfig& config,
                   float random_unit);
} // namespace response
} // namespace pinball
#endif
