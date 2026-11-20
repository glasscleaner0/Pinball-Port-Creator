#include "collision2d.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace pinball {
namespace physics {
namespace {

bool finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

// Float is the public storage format. Geometry and intermediate corrections
// stay in double so two finite float inputs never overflow during subtraction.
struct WideVec {
    double x;
    double y;
    WideVec(double px = 0, double py = 0) : x(px), y(py) {}
    explicit WideVec(Vec2 value) : x(value.x), y(value.y) {}
};

WideVec plus(WideVec a, WideVec b) { return WideVec(a.x + b.x, a.y + b.y); }
WideVec minus(WideVec a, WideVec b) { return WideVec(a.x - b.x, a.y - b.y); }
WideVec scale(WideVec value, double amount) { return WideVec(value.x * amount, value.y * amount); }
double inner(WideVec a, WideVec b) { return a.x * b.x + a.y * b.y; }
double cross(WideVec a, WideVec b) { return a.x * b.y - a.y * b.x; }
double norm(WideVec value) { return std::sqrt(inner(value, value)); }
WideVec at(WideVec position, WideVec velocity, double time) {
    return plus(position, scale(velocity, time));
}

float narrow(double value) {
    const double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(value) || value > limit || value < -limit) {
        throw std::overflow_error("collision result exceeds finite float range");
    }
    return static_cast<float>(value);
}

Vec2 narrow(WideVec value) { return Vec2(narrow(value.x), narrow(value.y)); }

void require_finite(Vec2 value) {
    if (!finite(value)) throw std::invalid_argument("vector must be finite");
}

void arguments(Vec2 p, Vec2 v, float radius, float max_time) {
    if (!finite(p) || !finite(v) || !std::isfinite(radius) || radius <= 0 ||
        !std::isfinite(max_time) || max_time < 0)
        throw std::invalid_argument("invalid swept-circle arguments");
}

WideVec contact_normal(WideVec separation, WideVec velocity) {
    const double distance = norm(separation);
    if (distance > 0) return scale(separation, 1.0 / distance);
    const double speed = norm(velocity);
    if (speed > 0) return scale(velocity, -1.0 / speed);
    return WideVec(1, 0);
}

struct WideHit {
    bool hit;
    bool initial_overlap;
    double time;
    double penetration;
    WideVec point;
    WideVec normal;
    WideHit() : hit(false), initial_overlap(false), time(0), penetration(0), point(), normal() {}
};

WideHit make_hit(double time, WideVec point, WideVec normal, bool overlap, double penetration) {
    WideHit result;
    result.hit = true;
    result.initial_overlap = overlap;
    result.time = time;
    result.point = point;
    result.normal = normal;
    result.penetration = penetration;
    return result;
}

SweepHit public_hit(const WideHit& source) {
    SweepHit result;
    if (!source.hit) return result;
    result.time = narrow(source.time);
    result.point = narrow(source.point);
    result.normal = narrow(source.normal);
    result.penetration = narrow(source.penetration);
    result.initial_overlap = source.initial_overlap;
    result.hit = true;
    return result;
}

void consider(WideHit& best, const WideHit& candidate) {
    if (candidate.hit && (!best.hit || candidate.time < best.time)) best = candidate;
}

WideHit circle_sweep(WideVec p, WideVec velocity, double radius, WideVec obstacle,
                     double obstacle_radius, double max_time) {
    const WideVec relative = minus(p, obstacle);
    const double expanded = radius + obstacle_radius;
    const double distance = norm(relative);
    const double separation = distance - expanded;
    const WideVec normal = contact_normal(relative, velocity);
    if (separation < 0)
        return make_hit(0, plus(obstacle, scale(normal, obstacle_radius)), normal, true, -separation);
    if (separation == 0) {
        if (inner(velocity, normal) < 0)
            return make_hit(0, plus(obstacle, scale(normal, obstacle_radius)), normal, false, 0);
        return WideHit();
    }
    const double speed = norm(velocity);
    if (speed == 0) return WideHit();
    const double approach = inner(relative, velocity);
    if (approach >= 0) return WideHit();

    // Cross-product line distance avoids cancellation of b*b-a*c for a
    // distant almost-tangent trajectory. Its sign also reconstructs a stable
    // impact normal without subtracting two enormous world coordinates.
    const double signed_perpendicular = (cross(velocity, p) - cross(velocity, obstacle)) / speed;
    double perpendicular = std::fabs(signed_perpendicular);
    const double tolerance = 8 * std::numeric_limits<double>::epsilon() *
        (perpendicular + expanded);
    if (perpendicular - expanded > tolerance) return WideHit();
    if (perpendicular > expanded) perpendicular = expanded;
    const double half_chord = std::sqrt((expanded - perpendicular) * (expanded + perpendicular));
    const double distance_along = -approach / speed;
    // Conjugate form preserves a small entry distance near initial contact.
    const double entry_distance = (distance - expanded) * (distance + expanded) /
        (distance_along + half_chord);
    const double time = entry_distance / speed;
    if (time < 0 || time > max_time) return WideHit();
    const WideVec direction = scale(velocity, 1.0 / speed);
    const WideVec left(-direction.y, direction.x);
    const double signed_distance = signed_perpendicular < 0 ? -perpendicular : perpendicular;
    const WideVec contact_offset = plus(scale(direction, -half_chord), scale(left, signed_distance));
    const WideVec impact_normal = contact_normal(contact_offset, velocity);
    return make_hit(time, plus(obstacle, scale(impact_normal, obstacle_radius)), impact_normal, false, 0);
}

} // namespace

Vec2::Vec2(float px, float py) : x(px), y(py) {}
Vec2 add(Vec2 a, Vec2 b) {
    require_finite(a); require_finite(b);
    return narrow(plus(WideVec(a), WideVec(b)));
}
Vec2 subtract(Vec2 a, Vec2 b) {
    require_finite(a); require_finite(b);
    return narrow(minus(WideVec(a), WideVec(b)));
}
Vec2 multiply(Vec2 value, float amount) {
    require_finite(value);
    if (!std::isfinite(amount)) throw std::invalid_argument("scale must be finite");
    return narrow(scale(WideVec(value), amount));
}
float dot(Vec2 a, Vec2 b) {
    require_finite(a); require_finite(b);
    return narrow(inner(WideVec(a), WideVec(b)));
}
float length(Vec2 value) {
    require_finite(value);
    return narrow(norm(WideVec(value)));
}
Vec2 unit(Vec2 value) {
    require_finite(value);
    const double distance = norm(WideVec(value));
    if (distance == 0) throw std::invalid_argument("normal must be finite and nonzero");
    return narrow(scale(WideVec(value), 1.0 / distance));
}
SweepHit::SweepHit() : hit(false), initial_overlap(false), time(0), penetration(0), point(), normal() {}

SweepHit sweep_circle_circle(Vec2 p, Vec2 velocity, float radius, Vec2 obstacle,
                             float obstacle_radius, float max_time) {
    arguments(p, velocity, radius, max_time);
    if (!finite(obstacle) || !std::isfinite(obstacle_radius) || obstacle_radius < 0)
        throw std::invalid_argument("invalid obstacle circle");
    return public_hit(circle_sweep(WideVec(p), WideVec(velocity), radius,
        WideVec(obstacle), obstacle_radius, max_time));
}

SweepHit sweep_circle_segment(Vec2 p, Vec2 velocity, float radius, Vec2 start, Vec2 end,
                              float max_time, bool two_sided) {
    arguments(p, velocity, radius, max_time);
    if (!finite(start) || !finite(end)) throw std::invalid_argument("invalid segment endpoints");
    const WideVec position(p), motion(velocity), first(start), last(end);
    const WideVec axis = minus(last, first);
    const double segment_length = norm(axis);
    if (segment_length == 0)
        return public_hit(circle_sweep(position, motion, radius, first, 0, max_time));
    const WideVec tangent = scale(axis, 1.0 / segment_length);
    const WideVec front(-tangent.y, tangent.x);
    const WideVec from_start = minus(position, first);
    const double along = inner(from_start, tangent);
    // Evaluate the line equation before subtracting a huge endpoint from a
    // small position. This retains, for example, (0,1)'s distance from y=x
    // even when the segment endpoints are near the float range limits.
    const double signed_distance = (cross(axis, position) - cross(last, first)) / segment_length;
    WideVec closest;
    WideVec offset;
    if (along < 0) {
        closest = first;
        offset = minus(position, first);
    } else if (along > segment_length) {
        closest = last;
        offset = minus(position, last);
    } else {
        offset = scale(front, signed_distance);
        closest = minus(position, offset);
    }
    const double distance = norm(offset);
    WideVec overlap_normal = contact_normal(offset, motion);
    if (distance == 0) {
        overlap_normal = front;
        if (two_sided && inner(motion, front) > 0) overlap_normal = scale(front, -1);
    }
    if (distance < radius && (two_sided || inner(overlap_normal, front) >= 0))
        return public_hit(make_hit(0, closest, overlap_normal, true, radius - distance));
    if (distance == radius && inner(motion, overlap_normal) < 0 &&
        (two_sided || inner(overlap_normal, front) >= 0))
        return public_hit(make_hit(0, closest, overlap_normal, false, 0));
    WideHit best;
    const int faces = two_sided ? 2 : 1;
    for (int side = 0; side < faces; ++side) {
        const WideVec normal = side == 0 ? front : scale(front, -1);
        const double speed = inner(motion, normal);
        if (speed >= 0) continue;
        const double initial_distance = side == 0 ? signed_distance : -signed_distance;
        const double time = (radius - initial_distance) / speed;
        if (time < 0 || time > max_time) continue;
        const WideVec centre = at(position, motion, time);
        const double projection = inner(minus(centre, first), tangent);
        if (projection < 0 || projection > segment_length) continue;
        consider(best, make_hit(time, minus(centre, scale(normal, radius)), normal, false, 0));
    }
    const WideVec endpoints[] = { first, last };
    for (int i = 0; i < 2; ++i) {
        const WideHit hit = circle_sweep(position, motion, radius, endpoints[i], 0, max_time);
        if (two_sided || (hit.hit && inner(hit.normal, front) >= 0)) consider(best, hit);
    }
    return public_hit(best);
}

Vec2 contact_velocity(Vec2 incoming, Vec2 normal, float restitution) {
    if (!finite(incoming) || !finite(normal) || !std::isfinite(restitution) ||
        restitution < 0 || restitution > 1)
        throw std::invalid_argument("invalid velocity, normal or restitution");
    const double normal_length = norm(WideVec(normal));
    if (normal_length == 0) throw std::invalid_argument("normal must be finite and nonzero");
    const WideVec direction = scale(WideVec(normal), 1.0 / normal_length);
    const double incoming_normal = inner(WideVec(incoming), direction);
    if (incoming_normal >= 0) return incoming;
    return narrow(plus(WideVec(incoming), scale(direction, -(1.0 + restitution) * incoming_normal)));
}

} // namespace physics
} // namespace pinball
