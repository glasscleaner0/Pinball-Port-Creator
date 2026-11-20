#include "runtime.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace pinball::runtime;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Error, class Function>
void throws(Function function, const std::string& message) {
    bool caught = false;
    try { function(); }
    catch (const Error&) { caught = true; }
    check(caught, message);
}

void near(double value, double expected, const std::string& message) {
    check(std::fabs(value - expected) < 0.00000001, message);
}

void timer_order_and_stale_ids() {
    TimerQueue queue;
    std::vector<int> events;
    const TimerId first = queue.schedule_at(10, [&](TimerId id, Tick time) {
        check(id != invalid_timer_id && time == 10, "timer carries id and deadline");
        events.push_back(1);
    });
    queue.schedule_at(5, [&](TimerId, Tick) { events.push_back(2); });
    queue.schedule_at(10, [&](TimerId, Tick) { events.push_back(3); });
    const TimerId canceled = queue.schedule_at(10, [&](TimerId, Tick) { events.push_back(4); });
    check(queue.cancel(canceled) && !queue.cancel(canceled), "cancel succeeds once");
    check(queue.dispatch_until(4).callbacks == 0, "early timer does not fire");
    check(queue.dispatch_until(5).callbacks == 1, "first deadline fires once");
    TimerDispatchResult result = queue.dispatch_until(10);
    check(result.callbacks == 2 && !result.due_remaining && queue.size() == 0, "due timer count");
    check(events == std::vector<int>({2, 1, 3}), "deadline then creation order");
    check(!queue.cancel(first), "fired id is stale");
    const TimerId cleared = queue.schedule_at(20, [](TimerId, Tick) {});
    queue.clear();
    const TimerId later = queue.schedule_at(20, [](TimerId, Tick) {});
    check(later > cleared && !queue.cancel(cleared), "clear never reuses ids");
    check(!queue.cancel(invalid_timer_id) && queue.size() == 1, "invalid id cannot cancel another timer");
    queue.reset(0);
    const TimerId reset_id = queue.schedule_at(0, [](TimerId, Tick) {});
    check(reset_id > later && !queue.cancel(later), "clock reset preserves stale-handle safety");
    check(queue.dispatch_until(0).callbacks == 1, "explicit reset permits a fresh clock epoch");
}

void timer_mutation_and_budget() {
    TimerQueue queue;
    std::vector<int> events;
    TimerId second = 0;
    queue.schedule_at(10, [&](TimerId own, Tick) {
        check(!queue.cancel(own), "executing timer removed before callback");
        events.push_back(1);
        check(queue.cancel(second), "callback cancels later timer");
        queue.schedule_at(10, [&](TimerId, Tick) { events.push_back(4); });
    });
    second = queue.schedule_at(10, [&](TimerId, Tick) { events.push_back(2); });
    queue.schedule_at(10, [&](TimerId, Tick) { events.push_back(3); });
    check(queue.dispatch_until(10).callbacks == 3, "new due timer fires in current dispatch");
    check(events == std::vector<int>({1, 3, 4}), "equal-deadline mutation preserves creation order");

    queue.schedule_at(11, [&](TimerId, Tick) {
        queue.clear();
        queue.schedule_at(10, [&](TimerId, Tick) { events.push_back(5); });
    });
    queue.schedule_at(11, [&](TimerId, Tick) { events.push_back(6); });
    check(queue.dispatch_until(11).callbacks == 2 && events.back() == 5, "clear and past-due insertion during dispatch");

    std::size_t called = 0;
    TimerCallback repeat;
    repeat = [&](TimerId, Tick deadline) {
        ++called;
        queue.schedule_at(deadline, repeat);
    };
    queue.schedule_at(12, repeat);
    TimerDispatchResult result = queue.dispatch_until(12, 3);
    check(called == 3 && result.callbacks == 3 && result.due_remaining, "self-reschedule is bounded");
    check(queue.dispatch_until(12, 0).due_remaining && called == 3, "zero dispatch budget is read-only");
    queue.clear();
}

void timer_reentrancy_and_exceptions() {
    TimerQueue queue;
    bool recursive_rejected = false;
    bool reset_rejected = false;
    queue.schedule_at(0, [&](TimerId, Tick) {
        try { queue.dispatch_until(0); }
        catch (const std::logic_error&) { recursive_rejected = true; }
        try { queue.reset(0); }
        catch (const std::logic_error&) { reset_rejected = true; }
    });
    queue.dispatch_until(0);
    check(recursive_rejected && reset_rejected, "recursive dispatch and clock reset rejected deterministically");
    const TimerId bad = queue.schedule_at(1, [](TimerId, Tick) { throw std::runtime_error("callback failure"); });
    bool survivor_ran = false;
    queue.schedule_at(1, [&](TimerId, Tick) { survivor_ran = true; });
    throws<std::runtime_error>([&]() { queue.dispatch_until(1); }, "callback exception propagates");
    check(queue.size() == 1 && !queue.cancel(bad), "throwing timer consumed, pending timer retained");
    check(queue.dispatch_until(1).callbacks == 1 && survivor_ran, "dispatch guard restored after exception");
}

void timer_invalid_times() {
    TimerQueue queue;
    const TimerCallback noop = [](TimerId, Tick) {};
    throws<std::invalid_argument>([&]() { queue.schedule_at(-1, noop); }, "negative deadline rejected");
    throws<std::invalid_argument>([&]() { queue.schedule_at(0, TimerCallback()); }, "empty callback rejected");
    throws<std::invalid_argument>([&]() { queue.schedule_after(0, -1, noop); }, "negative delay rejected");
    throws<std::invalid_argument>([&]() { queue.schedule_after(-1, 0, noop); }, "negative scheduling time rejected");
    throws<std::overflow_error>([&]() {
        queue.schedule_after((std::numeric_limits<Tick>::max)(), 1, noop);
    }, "deadline addition cannot wrap");
    throws<std::invalid_argument>([&]() { queue.reset(-1); }, "negative timer epoch rejected");
    check(queue.size() == 0, "invalid scheduling does not create timers");
    queue.dispatch_until(10);
    throws<std::invalid_argument>([&]() { queue.dispatch_until(9); }, "backward dispatch rejected");
    throws<std::invalid_argument>([&]() { queue.dispatch_until(-1); }, "negative dispatch rejected");
    check(queue.dispatch_until(10).callbacks == 0, "invalid dispatch preserves clock");
    queue.schedule_at((std::numeric_limits<Tick>::max)(), noop);
    check(queue.dispatch_until((std::numeric_limits<Tick>::max)()).callbacks == 1, "maximum tick deadline remains valid");
}

void fixed_step_cadence_and_catch_up() {
    FixedStepScheduler clock(FixedStepConfig(10, 16, 3));
    FrameSchedule frame = clock.advance(0);
    check(frame.simulation_steps == 0 && !frame.render_due, "initial frame is not artificially advanced");
    frame = clock.advance(9);
    check(frame.simulation_steps == 0 && frame.elapsed == 9, "fractional simulation step retained");
    near(frame.interpolation, 0.9, "fractional interpolation");
    frame = clock.advance(10);
    check(frame.simulation_steps == 1 && frame.simulation_time == 10 && !frame.render_due, "simulation has independent cadence");
    frame = clock.advance(17);
    check(frame.simulation_steps == 0 && frame.render_due && frame.skipped_render_frames == 0, "render cadence is independent");
    near(frame.interpolation, 0.7, "simulation remainder survives render");
    frame = clock.advance(105);
    check(frame.simulation_steps == 3 && frame.simulation_time == 40, "catch-up steps capped");
    check(frame.dropped_simulation_time == 60, "excess complete steps reported as dropped");
    check(frame.render_due && frame.skipped_render_frames == 4, "stale render opportunities counted");
    near(frame.interpolation, 0.5, "catch-up preserves fractional remainder");
    frame = clock.advance(110);
    check(frame.simulation_steps == 1 && frame.simulation_time == 50 && frame.dropped_simulation_time == 0,
          "dropped time is not hidden deferred catch-up");
    check(!frame.render_due && frame.skipped_render_frames == 0, "render count consumed once");
}

void fixed_step_pause_and_resume() {
    FixedStepScheduler clock(FixedStepConfig(10, 16, 3));
    clock.advance(15);
    clock.set_paused(true, 20);
    FrameSchedule frame = clock.advance(100);
    check(frame.paused && frame.simulation_steps == 0 && frame.simulation_time == 10, "paused clock freezes simulation");
    check(frame.dropped_simulation_time == 10, "pause boundary accounts for residual and unadvanced active time");
    check(frame.elapsed == 85 && frame.paused_time == 80, "pause boundary elapsed accounting");
    check(frame.render_due && frame.skipped_render_frames == 5, "rendering remains scheduled while paused");
    near(frame.interpolation, 0.0, "pause clears simulation remainder");
    clock.set_paused(false, 150);
    frame = clock.advance(160);
    check(!frame.paused && frame.simulation_steps == 1 && frame.simulation_time == 20, "resume excludes paused interval from simulation");
    check(frame.paused_time == 50 && frame.elapsed == 60 && frame.dropped_simulation_time == 0, "resume reports elapsed paused time");
    clock.set_paused(false, 165);
    frame = clock.advance(170);
    check(frame.simulation_steps == 1, "same pause state does not rebase clock");

    FixedStepScheduler boundary(FixedStepConfig(10, 16, 1));
    boundary.set_paused(true, 5);
    boundary.set_paused(false, 20);
    boundary.set_paused(true, 25);
    frame = boundary.advance(30);
    check(frame.elapsed == 30 && frame.paused_time == 20 && frame.dropped_simulation_time == 10,
          "multiple pause transitions before a frame preserve accounting");
}

void fixed_step_invalid_and_extreme_times() {
    throws<std::invalid_argument>([]() { FixedStepScheduler clock(FixedStepConfig(0, 1, 1)); }, "zero simulation interval rejected");
    throws<std::invalid_argument>([]() { FixedStepScheduler clock(FixedStepConfig(1, -1, 1)); }, "negative render interval rejected");
    throws<std::invalid_argument>([]() { FixedStepScheduler clock(FixedStepConfig(1, 1, 0)); }, "zero catch-up cap rejected");
    throws<std::invalid_argument>([]() { FixedStepScheduler clock(FixedStepConfig(1, 1, 1), -1); }, "negative clock origin rejected");
    FixedStepScheduler clock(FixedStepConfig(10, 16, 2), 100);
    throws<std::invalid_argument>([&]() { clock.advance(99); }, "backward frame rejected");
    throws<std::invalid_argument>([&]() { clock.set_paused(true, 99); }, "backward pause rejected");
    throws<std::invalid_argument>([&]() { clock.reset(-1); }, "negative reset rejected");
    check(!clock.paused() && clock.advance(110).simulation_steps == 1, "invalid calls preserve scheduler state");
    clock.reset(200);
    check(clock.simulation_time() == 0 && clock.advance(200).elapsed == 0, "reset establishes a fresh epoch");

    const Tick maximum = (std::numeric_limits<Tick>::max)();
    FixedStepScheduler extreme(FixedStepConfig(1, 1, 1));
    FrameSchedule frame = extreme.advance(maximum);
    check(frame.simulation_steps == 1 && frame.simulation_time == 1 && frame.dropped_simulation_time == maximum - 1,
          "extreme gap does not overflow or loop over missed steps");
    check(frame.skipped_render_frames == static_cast<std::uint64_t>(maximum - 1), "extreme render skip count remains exact");
    check(extreme.advance(maximum).simulation_steps == 0, "maximum clock supports equal-time advance");
}

void input_edges_and_multiple_sources() {
    InputState input;
    input.set_source(0, Action::LeftFlipper, true);
    input.set_source(0, Action::LeftFlipper, true);
    input.set_source(1, Action::LeftFlipper, true);
    input.set_source(0, Action::LeftFlipper, false);
    check(input.down(Action::LeftFlipper), "second input source keeps action held");
    std::vector<InputTransition> edges = input.drain_transitions();
    check(edges.size() == 1 && edges[0].pressed && edges[0].source == 0 && edges[0].sequence == 1,
          "repeat and overlapping source press produce one logical down edge");
    input.set_source(1, Action::LeftFlipper, false);
    input.set_source(1, Action::LeftFlipper, false);
    input.set_source(2, Action::LeftFlipper, false);
    edges = input.drain_transitions();
    check(edges.size() == 1 && !edges[0].pressed && edges[0].source == 1 && edges[0].sequence == 2,
          "last source release produces one logical up edge");
    check(!input.down(Action::LeftFlipper) && input.drain_transitions().empty(), "drain consumes only transitions");
    for (std::size_t i = 0; i < action_count; ++i) input.set_source(3, static_cast<Action>(i), true);
    edges = input.drain_transitions();
    check(edges.size() == action_count, "all requested controller actions supported");
    input.release_source(3);
    edges = input.drain_transitions();
    check(edges.size() == action_count, "source release clears all its actions");
    for (std::size_t i = 0; i < action_count; ++i) {
        check(!input.down(static_cast<Action>(i)) && edges[i].action == static_cast<Action>(i) && !edges[i].pressed,
              "source release order is deterministic");
    }
}

void input_focus_and_disconnect() {
    InputState input;
    input.set_source(5, Action::RightFlipper, true);
    input.set_source(1, Action::LeftFlipper, true);
    input.set_source(1, Action::RightFlipper, true);
    input.drain_transitions();
    input.disconnect_source(5);
    check(input.down(Action::RightFlipper) && input.drain_transitions().empty(), "disconnect only removes that controller's holds");
    input.set_focused(false);
    std::vector<InputTransition> edges = input.drain_transitions();
    check(!input.focused() && edges.size() == 2, "focus loss releases all held logical actions");
    check(edges[0].action == Action::LeftFlipper && edges[1].action == Action::RightFlipper,
          "global releases use action order rather than source order");
    check(edges[0].cause == TransitionCause::FocusLost && edges[0].source == no_input_source, "focus loss has explicit provenance");
    input.set_source(1, Action::Plunger, true);
    input.set_source(1, Action::HiddenTest, true);
    input.set_focused(false);
    check(input.drain_transitions().empty() && !input.down(Action::Plunger) &&
          !input.down(Action::HiddenTest), "unfocused gameplay and control presses are blocked");
    input.set_focused(true);
    check(!input.down(Action::Plunger), "focus restoration does not synthesize a press");
    input.set_source(1, Action::Plunger, true);
    input.drain_transitions();
    input.disconnect_source(1);
    edges = input.drain_transitions();
    check(edges.size() == 1 && edges[0].cause == TransitionCause::Disconnected && edges[0].source == 1,
          "disconnect emits explicit release edge");
}

void input_pause_and_policy() {
    InputState input;
    input.set_source(1, Action::LeftFlipper, true);
    input.set_source(1, Action::Pause, true);
    input.drain_transitions();
    input.set_paused(true);
    std::vector<InputTransition> edges = input.drain_transitions();
    check(edges.size() == 2 && edges[0].cause == TransitionCause::Paused && !input.down(Action::Pause),
          "default pause releases all actions including pause button");
    input.set_source(1, Action::Plunger, true);
    input.set_source(1, Action::NudgeUp, true);
    input.set_source(1, Action::Pause, true);
    input.set_source(1, Action::Menu, true);
    input.set_source(1, Action::NewGame, true);
    input.set_source(1, Action::HiddenTest, true);
    input.set_source(1, Action::HiddenTest, true);
    edges = input.drain_transitions();
    check(edges.size() == 4 && !input.down(Action::Plunger) && !input.down(Action::NudgeUp) &&
          input.down(Action::HiddenTest) && edges.back().action == Action::HiddenTest &&
          edges.back().pressed && edges.back().cause == TransitionCause::Physical,
          "pause blocks gameplay but allows one fresh hidden-test/control edge");
    input.set_paused(false);
    check(!input.down(Action::Plunger), "resume does not replay blocked gameplay presses");
    input.set_source(1, Action::Plunger, true);
    check(input.down(Action::Plunger), "physical state can be explicitly resubmitted on resume");

    InputPolicy preserve;
    preserve.release_on_focus_loss = false;
    preserve.release_on_disconnect = false;
    preserve.release_on_pause = false;
    preserve.block_unfocused_presses = false;
    preserve.block_paused_gameplay_presses = false;
    InputState custom(preserve);
    custom.set_source(2, Action::NudgeLeft, true);
    custom.drain_transitions();
    custom.set_focused(false);
    custom.disconnect_source(2);
    custom.set_paused(true);
    check(custom.down(Action::NudgeLeft) && custom.drain_transitions().empty(), "explicit preserve policy leaves held states unchanged");
    custom.set_source(3, Action::NudgeRight, true);
    check(custom.down(Action::NudgeRight), "explicit policy can accept unfocused and paused gameplay");
    custom.release_all();
    edges = custom.drain_transitions();
    check(edges.size() == 3 && edges.back().cause == TransitionCause::ExplicitRelease,
          "explicit release is available even when automatic releases disabled");
}

void input_invalid_tokens() {
    InputState input;
    throws<std::invalid_argument>([&]() { input.set_source(no_input_source, Action::Menu, true); }, "reserved source rejected");
    throws<std::invalid_argument>([&]() { input.set_source(0, Action::Count, true); }, "sentinel action rejected");
    throws<std::invalid_argument>([&]() { input.down(static_cast<Action>(-1)); }, "negative action rejected");
    throws<std::invalid_argument>([&]() { input.release_source(no_input_source); }, "reserved release source rejected");
    throws<std::invalid_argument>([&]() { input.disconnect_source(no_input_source); }, "reserved disconnect source rejected");
    check(input.drain_transitions().empty(), "invalid input calls do not emit edges");
    check(std::string(action_name(Action::Menu)) == "menu", "stable human-readable action label");
    check(std::string(action_name(Action::HiddenTest)) == "hidden test", "hidden-test diagnostic label");
    check(static_cast<std::size_t>(Action::Menu) == 8 &&
          static_cast<std::size_t>(Action::HiddenTest) == 9 && action_count == 10,
          "new action is appended without renumbering existing actions");
}

} // namespace

int main() {
    try {
        timer_order_and_stale_ids();
        timer_mutation_and_budget();
        timer_reentrancy_and_exceptions();
        timer_invalid_times();
        fixed_step_cadence_and_catch_up();
        fixed_step_pause_and_resume();
        fixed_step_invalid_and_extreme_times();
        input_edges_and_multiple_sources();
        input_focus_and_disconnect();
        input_pause_and_policy();
        input_invalid_tokens();
        std::cout << "runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "runtime test failure: " << error.what() << '\n';
        return 1;
    }
}
