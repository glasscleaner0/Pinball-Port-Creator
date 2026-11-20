#ifndef PINBALL_REWRITE_RUNTIME_HPP
#define PINBALL_REWRITE_RUNTIME_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace pinball {
namespace runtime {

// Independent rewrite baseline, not a claim about the XP reference's timing.
// Times are nonnegative nanoseconds from an arbitrary monotonic epoch. No API
// accepts a wall/calendar clock. Invalid/negative time or backward clock advancement throws;
// arithmetic overflow throws rather than wrapping.
typedef std::int64_t Tick;
const Tick ticks_per_second = 1000000000;
typedef std::uint64_t TimerId;
const TimerId invalid_timer_id = 0;

typedef std::function<void(TimerId, Tick)> TimerCallback;
struct TimerDispatchResult {
    std::size_t callbacks;
    bool due_remaining;
};

class TimerQueue {
public:
    TimerQueue();
    TimerId schedule_at(Tick deadline, const TimerCallback& callback);
    TimerId schedule_after(Tick now, Tick delay, const TimerCallback& callback);
    bool cancel(TimerId id);
    void clear();
    // Start a fresh clock epoch and remove pending timers, preserving the ID
    // sequence so stale handles never become valid. Forbidden in a callback.
    void reset(Tick now = 0);
    std::size_t size() const;

    // Timers use the clock passed by the host; use simulation_time to freeze
    // them with simulation pause. Past-due insertion is legal.
    // One-shot timers run by deadline, then creation sequence. A timer is
    // removed BEFORE its callback. Callbacks can cancel/create/clear timers;
    // new already-due timers are eligible in this dispatch. The budget bounds
    // self-rescheduling callbacks. Recursive dispatch throws logic_error.
    // Callback exceptions propagate; the fired timer stays removed and all
    // unmodified pending timers survive. Repeating dispatch at the same time is OK.
    TimerDispatchResult dispatch_until(Tick now, std::size_t max_callbacks = 1024);
private:
    typedef std::pair<Tick, TimerId> Key;
    struct Entry { TimerCallback callback; };
    std::map<Key, Entry> ordered_;
    std::map<TimerId, Key> active_;
    TimerId last_id_;
    Tick last_dispatch_;
    bool dispatching_;
};

struct FixedStepConfig {
    Tick simulation_step;
    Tick render_period;
    std::size_t max_catch_up_steps;
    FixedStepConfig(Tick step, Tick render, std::size_t catch_up);
};

struct FrameSchedule {
    Tick elapsed;
    Tick paused_time;
    std::size_t simulation_steps;
    Tick simulation_step;
    Tick simulation_time;
    Tick dropped_simulation_time;
    bool render_due;
    std::uint64_t skipped_render_frames;
    double interpolation;
    bool paused;
};

class FixedStepScheduler {
public:
    explicit FixedStepScheduler(const FixedStepConfig& config, Tick now = 0);
    FrameSchedule advance(Tick now);

    // Pause takes effect at now. Any active interval not yet advanced, and any
    // fractional step at this boundary, is reported as dropped time in the
    // next schedule. Resume rebases the clock; paused time never becomes
    // catch-up work. Calling this with the existing state has no effect.
    // Rendering continues on its independent cadence during pause.
    void set_paused(bool paused, Tick now);
    bool paused() const;
    Tick simulation_time() const;
    const FixedStepConfig& config() const;
    void reset(Tick now = 0);
private:
    FixedStepConfig config_;
    Tick last_wall_;
    Tick simulation_accumulator_;
    Tick render_accumulator_;
    Tick simulation_time_;
    Tick pending_elapsed_;
    Tick pending_paused_;
    Tick pending_dropped_;
    bool paused_;
};

// Each backend assigns a stable, distinct source token to its keyboard or
// controller. The all-ones value is reserved for global release events.
typedef std::uint64_t InputSource;
const InputSource no_input_source = static_cast<InputSource>(-1);
enum class Action {
    LeftFlipper, RightFlipper, Plunger, NudgeLeft, NudgeRight, NudgeUp,
    NewGame, Pause, Menu, HiddenTest, Count
};
const std::size_t action_count = static_cast<std::size_t>(Action::Count);
const char* action_name(Action action);

enum class TransitionCause {
    Physical, FocusLost, Disconnected, Paused, ExplicitRelease
};
struct InputTransition {
    std::uint64_t sequence;
    InputSource source;
    Action action;
    bool pressed;
    TransitionCause cause;
};
struct InputPolicy {
    bool release_on_focus_loss;
    bool release_on_disconnect;
    bool release_on_pause;
    bool block_unfocused_presses;
    bool block_paused_gameplay_presses;
    InputPolicy();
};

class InputState {
public:
    explicit InputState(const InputPolicy& policy = InputPolicy());
    // Multiple sources OR together. Repeat presses and intermediate releases
    // produce no logical edge. Rejected presses are not remembered/replayed;
    // a backend can resubmit its physical state after focus/resume.
    void set_source(InputSource source, Action action, bool pressed);
    bool down(Action action) const;
    void release_source(InputSource source,
                        TransitionCause cause = TransitionCause::ExplicitRelease);
    // Global releases emit at most one up edge per action, in enum order.
    void release_all(TransitionCause cause = TransitionCause::ExplicitRelease);
    void disconnect_source(InputSource source);
    void set_focused(bool focused);
    // The default policy releases ALL actions on pause. While paused, new
    // Pause/Menu/NewGame/HiddenTest presses remain available; gameplay presses are blocked.
    void set_paused(bool paused);
    bool focused() const;
    bool paused() const;
    const InputPolicy& policy() const;
    std::vector<InputTransition> drain_transitions();
private:
    typedef std::array<bool, action_count> SourceState;
    InputPolicy policy_;
    std::map<InputSource, SourceState> sources_;
    std::array<std::size_t, action_count> holds_;
    std::vector<InputTransition> transitions_;
    std::uint64_t last_sequence_;
    bool focused_;
    bool paused_;
    void ensure_sequence_space(std::size_t count) const;
    void emit(InputSource source, Action action, bool pressed, TransitionCause cause);
};

} // namespace runtime
} // namespace pinball

#endif
