#include "reference_nudge.hpp"

#include <stdexcept>

namespace pinball {
namespace reference {
namespace {

void require_time(runtime::Tick time)
{
    if ((static_cast<std::uint64_t>(time) >> 63U) != 0)
        throw std::invalid_argument("reference nudge time must be nonnegative");
}

std::size_t side_index(NudgeSide side)
{
    switch (side) {
    case NudgeSide::LeftButton: return 0;
    case NudgeSide::RightButton: return 1;
    case NudgeSide::Up: return 2;
    }
    throw std::invalid_argument("unknown reference nudge side");
}

void require_cause(runtime::TransitionCause cause)
{
    switch (cause) {
    case runtime::TransitionCause::Physical:
    case runtime::TransitionCause::FocusLost:
    case runtime::TransitionCause::Disconnected:
    case runtime::TransitionCause::Paused:
    case runtime::TransitionCause::ExplicitRelease:
        return;
    }
    throw std::invalid_argument("unknown reference nudge transition cause");
}

} // namespace

ReferenceNudge::ReferenceNudge(runtime::Tick now)
{
    reset(now);
}

void ReferenceNudge::validate_now(runtime::Tick now) const
{
    require_time(now);
    if (static_cast<std::uint64_t>(now) < static_cast<std::uint64_t>(now_))
        throw std::invalid_argument("reference nudge time must not move backward");
}

void ReferenceNudge::change_contribution(std::size_t side, bool enabled)
{
    if (active_[side] == enabled) return;
    // Fixed bounded state contributions, rather than simulated forces. Every
    // inverse is emitted only when that contribution is actually active.
    const float horizontal[] = {1.0f, -1.0f, 0.0f};
    const float sign = enabled ? 1.0f : -1.0f;
    pending_.world_dx += sign * horizontal[side];
    pending_.world_dy += sign * 0.5f;
    pending_.changed = true;
    active_[side] = enabled;
}

void ReferenceNudge::set_pressed(NudgeSide side, bool pressed, runtime::Tick now,
    runtime::TransitionCause cause)
{
    const std::size_t index = side_index(side);
    validate_now(now);
    require_cause(cause);
    now_ = now;
    // Nonphysical edges are safety releases, not synthetic nudge presses.
    if (!pressed || cause != runtime::TransitionCause::Physical) {
        down_[index] = false;
        change_contribution(index, false);
        return; // A release never changes the shared timer or its owner.
    }
    if (down_[index]) return;
    down_[index] = true;
    change_contribution(index, true);
    timer_owner_ = index;
    timer_start_ = now;
    timer_pending_ = true;
}

void ReferenceNudge::press(NudgeSide side, runtime::Tick now, runtime::TransitionCause cause)
{
    set_pressed(side, true, now, cause);
}

void ReferenceNudge::release(NudgeSide side, runtime::Tick now, runtime::TransitionCause cause)
{
    set_pressed(side, false, now, cause);
}

NudgeDelta ReferenceNudge::drain()
{
    const NudgeDelta result = pending_;
    pending_.world_dx = 0;
    pending_.world_dy = 0;
    pending_.changed = false;
    return result;
}

NudgeDelta ReferenceNudge::advance(runtime::Tick now)
{
    validate_now(now);
    now_ = now;
    if (timer_pending_ &&
        static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(timer_start_) >=
            static_cast<std::uint64_t>(nudge_hold_duration)) {
        timer_pending_ = false;
        change_contribution(timer_owner_, false);
    }
    return drain();
}

NudgeOffset ReferenceNudge::offset() const
{
    const NudgeOffset result = {
        (active_[0] ? 2 : 0) - (active_[1] ? 2 : 0),
        -(int(active_[0]) + int(active_[1]) + int(active_[2]))};
    return result;
}

bool ReferenceNudge::active() const
{
    return active_[0] || active_[1] || active_[2];
}

bool ReferenceNudge::active(NudgeSide side) const
{
    return active_[side_index(side)];
}

std::array<bool,3> ReferenceNudge::active_flags() const
{
    return active_;
}

bool ReferenceNudge::down(NudgeSide side) const
{
    return down_[side_index(side)];
}

bool ReferenceNudge::timer_pending() const
{
    return timer_pending_;
}

runtime::Tick ReferenceNudge::time() const
{
    return now_;
}

NudgeDelta ReferenceNudge::cancel(runtime::Tick now)
{
    validate_now(now);
    now_ = now;
    for (std::size_t i = 0; i < active_.size(); ++i) change_contribution(i, false);
    down_.fill(false);
    timer_pending_ = false;
    return drain();
}

void ReferenceNudge::reset(runtime::Tick now)
{
    require_time(now);
    down_.fill(false);
    active_.fill(false);
    timer_pending_ = false;
    timer_owner_ = 0;
    timer_start_ = now;
    now_ = now;
    pending_.world_dx = 0;
    pending_.world_dy = 0;
    pending_.changed = false;
}

} // namespace reference
} // namespace pinball
