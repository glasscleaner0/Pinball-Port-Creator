#ifndef PINBALL_REWRITE_REFERENCE_PLUNGER_HPP
#define PINBALL_REWRITE_REFERENCE_PLUNGER_HPP

#include "runtime.hpp"
#include <cstddef>
#include <cstdint>

namespace pinball {
namespace reference {

// Exact-target XP static audit: the maximum and charge increment are signed
// integers. For eight DAT images, trunc(100/(8*8)) is 1, rather than the
// Release_2.1.0 reconstruction's float 1.5625. Evidence is in work/physics-v4-plunger/xp-binary-plunger-facts.json.
// This independent clock catches up against the press epoch; the reference's
// callback scheduling can add frame-dependent delay. No callback code is reused.
struct PlungerConfig {
    std::size_t image_count;
    float maximum_charge; // Positive signed32 integer; collision boost, not velocity.
    runtime::Tick cadence;
    runtime::Tick release_window;
    std::uint32_t increments_per_image;
    explicit PlungerConfig(std::size_t images, float maximum = 100.0f,
        runtime::Tick interval = 25000000, runtime::Tick window = 25000000,
        std::uint32_t subdivisions = 8);
};

class Plunger {
public:
    explicit Plunger(const PlungerConfig& config, runtime::Tick now = 0);
    // Nonnegative, monotonic nanoseconds. Equal times are permitted. No loops
    // or repeated callbacks are needed to catch up, including very large gaps.
    void advance(runtime::Tick now);
    // A fresh physical press resets charge to one immediate increment.
    // Returns true only when a held physical release opens the collision
    // window. Repeats do not restart charging or extend the window.
    // Nonphysical transitions cancel, deliberately retaining the fresh port's
    // focus/pause/disconnect safety policy instead of releasing a launch.
    bool set_pressed(bool pressed, runtime::Tick now,
        runtime::TransitionCause cause = runtime::TransitionCause::Physical);
    void cancel(runtime::Tick now);
    void reset(runtime::Tick now = 0); // Explicit fresh epoch; can go backward.

    float charge() const; // Retained during the release window, then zero.
    float pull() const;   // Charge fraction, also retained during the window.
    std::size_t image_frame() const; // Rest frame0 immediately on release.
    bool held() const;
    runtime::Tick time() const;
    const PlungerConfig& config() const;
    runtime::Tick full_pull_time() const; // Immediate first increment counts; XP 2.475 s.

    // Both queries advance time. Window is [release, release+duration), with
    // exclusive end. boost requires finite unit_random in[0,1] even outside
    // the window. It returns charge*(1+.1*unit_random) inside, zero outside.
    // It does not consume a collision: the caller controls contacts and RNG.
    runtime::Tick window_remaining(runtime::Tick now);
    float boost(runtime::Tick now, float unit_random);
    // Invalid input and unrepresentable results throw before changing state.
private:
    PlungerConfig config_;
    std::uint32_t maximum_charge_, quantum_, full_steps_;
    runtime::Tick full_pull_time_;
    std::uint32_t charge_steps_;
    runtime::Tick now_;
    runtime::Tick press_time_;
    runtime::Tick window_end_;
    bool held_;
    bool window_open_;
    std::uint32_t charge_units() const;
    double charge_value() const;
};

} // namespace reference
} // namespace pinball
#endif
