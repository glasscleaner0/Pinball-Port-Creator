#ifndef PINBALL_REWRITE_TABLE_SHAKE_HPP
#define PINBALL_REWRITE_TABLE_SHAKE_HPP

#include "runtime.hpp"
#include "scene.hpp"

#include <array>

namespace pinball {
namespace visual {

enum class ShakeDirection { Left, Right, Up };
struct ShakeOffset {
    int dx;
    int dy;
};

struct ShakeConfig {
    double horizontal_pixels;
    double vertical_pixels;
    runtime::Tick duration;
    // Baseline defaults are visible Release-reference offsets and a maximum
    // hold of 400 ms; this is not verification against the target XP executable.
    // Pixel amplitudes are floored. Both amplitudes must be finite/nonnegative;
    // duration must be positive. Three vertical contributions must fit int.
    ShakeConfig(double horizontal = 2, double vertical = 1,
                runtime::Tick duration = 400000000);
};

// Independently implemented held visual translations; no ball impulse,
// cooldown, tilt, input mapping, render placement or wall-clock reads.
// Left contributes (-horizontal,-vertical), Right (+horizontal,-vertical),
// Up (0,-vertical). Concurrent distinct directions add; retriggering the same
// direction refreshes its bounded hold instead of adding another contribution.
// Each expires independently after duration; release can end one earlier.
// This deliberately exposes a clear per-direction hold policy; it does not
// reproduce the Release implementation's shared latest-timer overlap quirk.
class TableShake {
public:
    explicit TableShake(const ShakeConfig& config = ShakeConfig(), runtime::Tick now = 0);
    void trigger(ShakeDirection direction, runtime::Tick now);
    void release(ShakeDirection direction, runtime::Tick now);
    void cancel(runtime::Tick now);
    void reset(runtime::Tick now = 0); // Explicit fresh epoch; may move backward.
    ShakeOffset offset(runtime::Tick now) const;
    // A live hold, even if zero amplitude or opposing offsets cancel visually.
    bool active(runtime::Tick now) const;
    const ShakeConfig& config() const;
private:
    ShakeConfig config_;
    int horizontal_;
    int vertical_;
    std::array<runtime::Tick,3> started_;
    std::array<bool,3> held_;
    runtime::Tick last_event_;
    void validate_now(runtime::Tick now) const;
};

// Shift the intersection of region and surface; source samples are restricted
// to that same intersection. Color and depth move together, uncovered pixels
// become opaque black with depth 65535, and every outside pixel is untouched.
// Positive dx/dy moves content right/down. Zero offsets are exact no-ops.
// No source is read after destination writes begin: overlapping moves are safe.
// Apply this only to the final table region AFTER rendering table sprites/ball.
// Render again from the immutable scene background before the next application;
// this helper changes only the Surface passed to it, not any separate background.
void shift_region(scene::Surface& surface, scene::Rect region, int dx, int dy);

} // namespace visual
} // namespace pinball
#endif
