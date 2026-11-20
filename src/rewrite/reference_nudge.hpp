#ifndef PINBALL_REWRITE_REFERENCE_NUDGE_HPP
#define PINBALL_REWRITE_REFERENCE_NUDGE_HPP

#include "runtime.hpp"
#include <array>

namespace pinball {
namespace reference {

// Logical input sides, not visible displacement directions. LeftButton moves
// the table right/up and contributes world velocity (+1,+.5); RightButton is
// (-1,+.5); Up is (0,+.5). The visual contributions are (+2,-1),(-2,-1),(0,-1).
enum class NudgeSide { LeftButton, RightButton, Up };
struct NudgeOffset { int dx; int dy; };
struct NudgeDelta {
    float world_dx;
    float world_dy;
    // At least one contribution/inverse since the preceding drain, even if
    // those effects sum to zero. Physical-latch/timer-only changes are false.
    bool changed;
};
const runtime::Tick nudge_hold_duration = 400000000;

// Independently authored state model from Release_2.1.0 nudge.cpp18-82,
// pb.cpp550-557/602-612 and timer.cpp. Source behavior is reference evidence,
// not verification against the supplied XP executable. No source is reused.
//
// One shared timeout belongs ONLY to the most recently accepted physical
// press. A new press replaces it without reversing an older side. A release
// reverses its side if still active and leaves the timeout untouched. Thus
// an older overlapping side can stay active until its physical release.
// Physical repeats are ignored until release, including after auto-expiry.
//
// Commands do NOT implicitly dispatch timers: call advance to do so. Input
// before advance at an exact boundary can replace an already-due timer;
// advance before input dispatches it first. This preserves caller event order.
// Every accepted operation uses nonnegative monotonic simulation nanoseconds;
// external simulation-clock freezing supplies pause behavior. No cooldown,
// tilt, ball selection/eligibility, ball mutation or wall-clock reads occur.
class ReferenceNudge {
public:
    explicit ReferenceNudge(runtime::Tick now = 0);
    // Nonphysical causes act as per-side safety releases, even if pressed=true.
    // This focus/pause/disconnect policy is a port extension.
    void set_pressed(NudgeSide side, bool pressed, runtime::Tick now,
        runtime::TransitionCause cause = runtime::TransitionCause::Physical);
    void press(NudgeSide side, runtime::Tick now,
        runtime::TransitionCause cause = runtime::TransitionCause::Physical);
    void release(NudgeSide side, runtime::Tick now,
        runtime::TransitionCause cause = runtime::TransitionCause::Physical);
    // Dispatch the current timeout if due and drain all accumulated effects.
    // Apply each drain only to balls eligible at that instant. Drain after
    // each command if eligibility can change between events.
    NudgeDelta advance(runtime::Tick now);
    NudgeOffset offset() const;
    bool active() const;
    bool active(NudgeSide side) const;
    std::array<bool,3> active_flags() const; // LeftButton,RightButton,Up order.
    bool down(NudgeSide side) const;
    bool timer_pending() const;
    runtime::Tick time() const;
    // Port safety extension: clear all latches/flags/timer, returning pending
    // effects plus the inverse of every active contribution, already drained.
    NudgeDelta cancel(runtime::Tick now);
    // Fresh epoch; may move backward. Clears without inverse and discards any
    // pending effects, suitable when the caller also resets/replaces balls.
    void reset(runtime::Tick now = 0);
private:
    std::array<bool,3> down_;
    std::array<bool,3> active_;
    bool timer_pending_;
    std::size_t timer_owner_;
    runtime::Tick timer_start_;
    runtime::Tick now_;
    NudgeDelta pending_;
    void validate_now(runtime::Tick now) const;
    void change_contribution(std::size_t side, bool enabled);
    NudgeDelta drain();
};

} // namespace reference
} // namespace pinball
#endif
