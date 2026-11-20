#include "reference_nudge.hpp"

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace pinball::reference;
using pinball::runtime::Tick;
using pinball::runtime::TransitionCause;

void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& operation, const std::string& fragment)
{
    try { operation(); }
    catch (const std::invalid_argument& error) {
        require(std::string(error.what()).find(fragment) != std::string::npos,
            "wrong reference nudge rejection: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("expected reference nudge rejection containing " + fragment);
}

void delta(const NudgeDelta& value, float dx, float dy, bool changed = true)
{
    require(value.world_dx == dx && value.world_dy == dy && value.changed == changed,
        "incorrect accumulated world velocity delta or change marker");
}

void offset(const ReferenceNudge& value, int dx, int dy)
{
    const NudgeOffset actual = value.offset();
    require(actual.dx == dx && actual.dy == dy, "incorrect visible reference nudge offset");
}

void logical_directions_early_release_and_no_cooldown()
{
    const NudgeSide sides[] = {NudgeSide::LeftButton, NudgeSide::RightButton, NudgeSide::Up};
    const float world_x[] = {1, -1, 0};
    const int visible_x[] = {2, -2, 0};
    for (std::size_t i = 0; i < 3; ++i) {
        ReferenceNudge controller;
        controller.press(sides[i], 0);
        require(controller.active(sides[i]) && controller.down(sides[i]),
            "physical press must set active contribution and input latch");
        offset(controller, visible_x[i], -1);
        delta(controller.advance(0), world_x[i], 0.5f);
        controller.release(sides[i], 1);
        offset(controller, 0, 0);
        delta(controller.advance(1), -world_x[i], -0.5f);
        require(!controller.active() && !controller.down(sides[i]) && controller.timer_pending(),
            "early key release clears its side but must leave the shared timeout pending");
        controller.press(sides[i], 2); // No cooldown even one nanosecond after release.
        delta(controller.advance(2), world_x[i], 0.5f);
        controller.release(sides[i], 3);
        delta(controller.advance(3), -world_x[i], -0.5f);
        delta(controller.advance(nudge_hold_duration + 2), 0, 0, false);
        require(!controller.timer_pending(), "released owner's timeout must eventually dispatch as a no-op");
    }
}

void expiry_repeat_latch_and_timer_replacement()
{
    ReferenceNudge controller;
    controller.press(NudgeSide::LeftButton, 0);
    delta(controller.advance(0), 1, 0.5f);
    delta(controller.advance(nudge_hold_duration - 1), 0, 0, false);
    offset(controller, 2, -1);
    delta(controller.advance(nudge_hold_duration), -1, -0.5f);
    offset(controller, 0, 0);
    require(controller.down(NudgeSide::LeftButton) && !controller.active(),
        "timeout clears contribution without pretending the physical button was released");
    controller.press(NudgeSide::Up, nudge_hold_duration + 1);
    delta(controller.advance(nudge_hold_duration + 1), 0, 0.5f);
    controller.press(NudgeSide::LeftButton, nudge_hold_duration + 2);
    delta(controller.advance(nudge_hold_duration + 2), 0, 0, false);
    offset(controller, 0, -1);
    // Ignored repeat must not postpone or replace Up's shared timer.
    delta(controller.advance(nudge_hold_duration * 2 + 1), 0, -0.5f);
    controller.release(NudgeSide::LeftButton, nudge_hold_duration * 2 + 2);
    delta(controller.advance(nudge_hold_duration * 2 + 2), 0, 0, false);
    controller.press(NudgeSide::LeftButton, nudge_hold_duration * 2 + 3);
    delta(controller.advance(nudge_hold_duration * 2 + 3), 1, 0.5f);
    offset(controller, 2, -1);
}

void shared_timer_overlap_and_released_owner_noop()
{
    ReferenceNudge controller;
    controller.press(NudgeSide::LeftButton, 0);
    delta(controller.advance(0), 1, 0.5f);
    controller.press(NudgeSide::RightButton, 100);
    delta(controller.advance(100), -1, 0.5f);
    offset(controller, 0, -2);
    controller.release(NudgeSide::RightButton, 150);
    delta(controller.advance(150), 1, -0.5f);
    offset(controller, 2, -1);
    delta(controller.advance(nudge_hold_duration), 0, 0, false);
    require(controller.active(NudgeSide::LeftButton), "new press must have replaced the old Left timeout");
    delta(controller.advance(nudge_hold_duration + 100), 0, 0, false);
    require(controller.active(NudgeSide::LeftButton) && !controller.timer_pending(),
        "released latest owner must no-op at expiry without clearing older overlapping flags");
    controller.release(NudgeSide::LeftButton, nudge_hold_duration + 101);
    delta(controller.advance(nudge_hold_duration + 101), -1, -0.5f);
    offset(controller, 0, 0);

    controller.reset();
    controller.press(NudgeSide::LeftButton, 0); delta(controller.advance(0), 1, 0.5f);
    controller.press(NudgeSide::RightButton, 100); delta(controller.advance(100), -1, 0.5f);
    controller.release(NudgeSide::LeftButton, 150); delta(controller.advance(150), -1, -0.5f);
    // Releasing a non-owner also must not kill the latest owner's timeout.
    delta(controller.advance(nudge_hold_duration + 100), 1, -0.5f);
    offset(controller, 0, 0);
}

void all_sides_concurrent_and_explicit_dispatch_order()
{
    ReferenceNudge controller;
    controller.press(NudgeSide::LeftButton, 0);
    controller.press(NudgeSide::RightButton, 100);
    controller.press(NudgeSide::Up, 200);
    delta(controller.advance(200), 0, 1.5f);
    offset(controller, 0, -3);
    const std::array<bool,3> flags = controller.active_flags();
    require(flags[0] && flags[1] && flags[2], "all three distinct contributions must coexist");
    delta(controller.advance(nudge_hold_duration + 100), 0, 0, false);
    delta(controller.advance(nudge_hold_duration + 200), 0, -0.5f);
    offset(controller, 0, -2);
    require(controller.active(NudgeSide::LeftButton) && controller.active(NudgeSide::RightButton) &&
        !controller.active(NudgeSide::Up), "only latest timer owner may expire");
    delta(controller.cancel(nudge_hold_duration + 201), 0, -1);
    offset(controller, 0, 0);

    // A due timer is not secretly dispatched by key commands. At this exact
    // boundary, replacing it before advance retains the older contribution.
    controller.reset();
    controller.press(NudgeSide::LeftButton, 0); delta(controller.advance(0), 1, 0.5f);
    controller.press(NudgeSide::RightButton, nudge_hold_duration);
    delta(controller.advance(nudge_hold_duration), -1, 0.5f);
    offset(controller, 0, -2);
    controller.reset();
    controller.press(NudgeSide::LeftButton, 0); delta(controller.advance(0), 1, 0.5f);
    delta(controller.advance(nudge_hold_duration), -1, -0.5f);
    controller.press(NudgeSide::RightButton, nudge_hold_duration);
    delta(controller.advance(nudge_hold_duration), -1, 0.5f);
    offset(controller, -2, -1);
}

void pending_deltas_cancel_and_reset()
{
    ReferenceNudge controller;
    controller.press(NudgeSide::LeftButton, 0);
    controller.release(NudgeSide::LeftButton, 0);
    delta(controller.advance(0), 0, 0); // Events happened even though net delta is zero.
    delta(controller.advance(0), 0, 0, false);
    for (int i = 0; i < 10000; ++i) {
        controller.press(NudgeSide::Up, 0);
        controller.release(NudgeSide::Up, 0);
    }
    delta(controller.advance(0), 0, 0); // No accumulation drift or integer event counter.
    controller.press(NudgeSide::LeftButton, 1);
    delta(controller.cancel(1), 0, 0); // Pending press and inverse returned together.
    delta(controller.advance(1), 0, 0, false);
    require(!controller.active() && !controller.down(NudgeSide::LeftButton) && !controller.timer_pending(),
        "cancel must clear all visual/input/timer state");
    controller.press(NudgeSide::LeftButton, 2);
    controller.press(NudgeSide::Up, 2);
    delta(controller.advance(2), 1, 1);
    delta(controller.cancel(3), -1, -1);
    delta(controller.cancel(3), 0, 0, false);
    controller.press(NudgeSide::RightButton, 4);
    controller.reset(0); // Intentionally discard pending and establish a fresh epoch.
    offset(controller, 0, 0);
    delta(controller.advance(0), 0, 0, false);
    require(controller.time() == 0, "reset must establish the requested new epoch");
}

void nonphysical_safety_releases()
{
    const TransitionCause causes[] = {TransitionCause::FocusLost, TransitionCause::Disconnected,
        TransitionCause::Paused, TransitionCause::ExplicitRelease};
    for (std::size_t i = 0; i < sizeof(causes) / sizeof(causes[0]); ++i) {
        ReferenceNudge controller;
        controller.press(NudgeSide::LeftButton, 0);
        controller.press(NudgeSide::RightButton, 0);
        delta(controller.advance(0), 0, 1);
        controller.set_pressed(NudgeSide::LeftButton, false, 1, causes[i]);
        delta(controller.advance(1), -1, -0.5f);
        require(controller.active(NudgeSide::RightButton) && !controller.down(NudgeSide::LeftButton),
            "safety release is per-side and must clear its physical latch");
        controller.set_pressed(NudgeSide::RightButton, true, 2, causes[i]);
        delta(controller.advance(2), 1, -0.5f);
        offset(controller, 0, 0);
        require(controller.timer_pending(), "safety release must preserve shared timer semantics");
        delta(controller.advance(nudge_hold_duration), 0, 0, false);
        controller.press(NudgeSide::LeftButton, nudge_hold_duration + 1, causes[i]);
        delta(controller.advance(nudge_hold_duration + 1), 0, 0, false);
        require(!controller.active(), "nonphysical pressed edge must never inject a nudge");
    }
}

void clock_boundaries_and_transactional_errors()
{
    const Tick boundaries[] = {2147483648LL, 4294967296LL};
    for (std::size_t i = 0; i < 2; ++i) {
        const Tick start = boundaries[i] - 10;
        ReferenceNudge controller(start);
        controller.press(NudgeSide::LeftButton, start);
        delta(controller.advance(start), 1, 0.5f);
        delta(controller.advance(start + nudge_hold_duration - 1), 0, 0, false);
        offset(controller, 2, -1);
        delta(controller.advance(start + nudge_hold_duration), -1, -0.5f);
        offset(controller, 0, 0);
    }
    // Large elapsed ages, not only large absolute epochs, must dispatch once.
    for (std::size_t i = 0; i < 2; ++i) {
        ReferenceNudge gap;
        gap.press(NudgeSide::LeftButton, 0); delta(gap.advance(0), 1, 0.5f);
        delta(gap.advance(boundaries[i]), -1, -0.5f);
        delta(gap.advance(boundaries[i]), 0, 0, false);
    }
    const Tick maximum = (std::numeric_limits<Tick>::max)();
    ReferenceNudge gap;
    gap.press(NudgeSide::Up, 0); delta(gap.advance(0), 0, 0.5f);
    delta(gap.advance(maximum), 0, -0.5f);
    ReferenceNudge end(maximum - nudge_hold_duration);
    end.press(NudgeSide::Up, maximum - nudge_hold_duration);
    delta(end.advance(maximum - 1), 0, 0.5f);
    delta(end.advance(maximum), 0, -0.5f);
    end.reset(maximum - 10);
    end.press(NudgeSide::RightButton, maximum - 10);
    delta(end.advance(maximum), -1, 0.5f); // No unrepresentable deadline is formed.
    delta(end.cancel(maximum), 1, -0.5f);
    rejects([] { ReferenceNudge invalid(-1); }, "nonnegative");
    ReferenceNudge controller(10);
    controller.press(NudgeSide::LeftButton, 11);
    rejects([&] { controller.advance(-1); }, "nonnegative");
    rejects([&] { controller.advance(10); }, "backward");
    rejects([&] { controller.press(NudgeSide::RightButton, 10); }, "backward");
    rejects([&] { controller.release(NudgeSide::LeftButton, 10); }, "backward");
    rejects([&] { controller.cancel(10); }, "backward");
    rejects([&] { controller.reset(-1); }, "nonnegative");
    rejects([&] { controller.press(static_cast<NudgeSide>(99), 12); }, "side");
    rejects([&] { controller.release(static_cast<NudgeSide>(99), 12); }, "side");
    rejects([&] { controller.active(static_cast<NudgeSide>(99)); }, "side");
    rejects([&] { controller.down(static_cast<NudgeSide>(99)); }, "side");
    rejects([&] { controller.press(NudgeSide::Up, 12, static_cast<TransitionCause>(99)); }, "cause");
    require(controller.time() == 11 && controller.active(NudgeSide::LeftButton),
        "invalid operations must preserve clock and active state");
    offset(controller, 2, -1);
    delta(controller.advance(11), 1, 0.5f); // Pending output also survives rejection.
    // Frozen simulation time means a pause does not consume any timeout.
    for (int i = 0; i < 10; ++i) delta(controller.advance(11), 0, 0, false);
    offset(controller, 2, -1);
}
} // namespace

int main()
{
    try {
        logical_directions_early_release_and_no_cooldown();
        expiry_repeat_latch_and_timer_replacement();
        shared_timer_overlap_and_released_owner_noop();
        all_sides_concurrent_and_explicit_dispatch_order();
        pending_deltas_cancel_and_reset();
        nonphysical_safety_releases();
        clock_boundaries_and_transactional_errors();
        std::cout << "reference nudge tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "reference nudge test failure: " << error.what() << '\n';
        return 1;
    }
}
