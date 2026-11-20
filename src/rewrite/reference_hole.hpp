#ifndef PINBALL_REWRITE_REFERENCE_HOLE_HPP
#define PINBALL_REWRITE_REFERENCE_HOLE_HPP

#include "runtime.hpp"

namespace pinball {
namespace reference {

struct HoleDropConfig {
    runtime::Tick hold;
    float gravity_acceleration; // Finite, nonnegative downward magnitude.
    runtime::Tick cadence;
    HoleDropConfig(runtime::Tick hold_time, float acceleration,
        runtime::Tick fall_cadence = runtime::ticks_per_second / 120);
};

// Independent fixed-cadence model of the documented legacy hole drop:
// wait at captured Z, then each fall tick subtract acceleration*dt from the
// vertical accumulator and add that accumulator directly to Z (no second dt).
// An analytic sum from the fall epoch makes large gaps bounded and independent
// of advance chunking. Float outputs do not claim XP floating-point bit parity
// or measured original cadence. This class owns no XY, masks, render or clock.
class HoleDrop {
public:
    explicit HoleDrop(const HoleDropConfig& config, runtime::Tick now = 0);
    // Nonnegative monotonic simulation times, equal times allowed. A second
    // capture while active is a logic error. Heights must be finite. Capturing
    // at/below the target still waits, then lands on the first fall tick.
    void capture(float z, float landing_z, runtime::Tick now);
    // True ONLY for a new landing; subsequent calls return false. The caller
    // applies the landing mask/XY velocity/ownership changes at this event.
    bool advance(runtime::Tick now);
    void reset(runtime::Tick now = 0); // Explicit fresh epoch may move backward.
    bool captured() const;
    bool falling() const; // Wait is over, even before the first fall tick.
    bool landed() const;  // Sticky until capture/reset; captured() is then false.
    float height() const;
    float vertical_velocity() const;
    runtime::Tick time() const;
    // DURATION from time() to wait end or next fall tick; zero if inactive or
    // landed. No absolute deadline is formed, including near INT64_MAX. The
    // caller must check its own clock addition before advancing this duration.
    runtime::Tick until_boundary() const;
private:
    HoleDropConfig config_;
    runtime::Tick now_;
    runtime::Tick captured_at_;
    float captured_z_;
    float landing_z_;
    float height_;
    float vertical_velocity_;
    bool captured_;
    bool falling_;
    bool landed_;
    void validate_now(runtime::Tick now) const;
};

} // namespace reference
} // namespace pinball
#endif
