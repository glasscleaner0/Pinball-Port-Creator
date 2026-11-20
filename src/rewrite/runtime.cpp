#include "runtime.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pinball {
namespace runtime {
namespace {

void require_time(Tick value) {
    if (value < 0) throw std::invalid_argument("runtime time must be nonnegative");
}

Tick add_time(Tick left, Tick right) {
    require_time(left);
    require_time(right);
    if (right > (std::numeric_limits<Tick>::max)() - left)
        throw std::overflow_error("runtime time overflow");
    return left + right;
}

std::size_t action_index(Action action) {
    const std::size_t index = static_cast<std::size_t>(action);
    if (index >= action_count) throw std::invalid_argument("invalid input action");
    return index;
}

void require_source(InputSource source) {
    if (source == no_input_source)
        throw std::invalid_argument("reserved input source token");
}

bool gameplay_action(Action action) {
    return action_index(action) <= static_cast<std::size_t>(Action::NudgeUp);
}

struct DispatchGuard {
    explicit DispatchGuard(bool& flag) : flag_(flag) { flag_ = true; }
    ~DispatchGuard() { flag_ = false; }
    bool& flag_;
};

} // namespace

TimerQueue::TimerQueue() : last_id_(0), last_dispatch_(0), dispatching_(false) {}

TimerId TimerQueue::schedule_at(Tick deadline, const TimerCallback& callback) {
    require_time(deadline);
    if (!callback) throw std::invalid_argument("timer callback must not be empty");
    if (last_id_ == (std::numeric_limits<TimerId>::max)())
        throw std::overflow_error("timer creation sequence exhausted");
    const TimerId id = last_id_ + 1;
    const Key key(deadline, id);
    Entry entry;
    entry.callback = callback;
    ordered_.insert(std::make_pair(key, entry));
    try {
        active_.insert(std::make_pair(id, key));
    } catch (...) {
        ordered_.erase(key);
        throw;
    }
    last_id_ = id;
    return id;
}

TimerId TimerQueue::schedule_after(Tick now, Tick delay, const TimerCallback& callback) {
    return schedule_at(add_time(now, delay), callback);
}

bool TimerQueue::cancel(TimerId id) {
    std::map<TimerId, Key>::iterator found = active_.find(id);
    if (found == active_.end()) return false;
    ordered_.erase(found->second);
    active_.erase(found);
    return true;
}

void TimerQueue::clear() {
    ordered_.clear();
    active_.clear();
}

void TimerQueue::reset(Tick now) {
    require_time(now);
    if (dispatching_) throw std::logic_error("cannot reset timer clock during dispatch");
    clear();
    last_dispatch_ = now;
}

std::size_t TimerQueue::size() const { return ordered_.size(); }

TimerDispatchResult TimerQueue::dispatch_until(Tick now, std::size_t max_callbacks) {
    require_time(now);
    if (now < last_dispatch_) throw std::invalid_argument("timer clock moved backward");
    if (dispatching_) throw std::logic_error("recursive timer dispatch is not supported");
    last_dispatch_ = now;
    DispatchGuard guard(dispatching_);
    TimerDispatchResult result = {0, false};
    while (result.callbacks < max_callbacks && !ordered_.empty()) {
        std::map<Key, Entry>::iterator next = ordered_.begin();
        if (next->first.first > now) break;
        const TimerId id = next->first.second;
        const Tick deadline = next->first.first;
        const TimerCallback callback = next->second.callback;
        ordered_.erase(next);
        active_.erase(id);
        ++result.callbacks;
        callback(id, deadline);
    }
    result.due_remaining = !ordered_.empty() && ordered_.begin()->first.first <= now;
    return result;
}

FixedStepConfig::FixedStepConfig(Tick step, Tick render, std::size_t catch_up)
    : simulation_step(step), render_period(render), max_catch_up_steps(catch_up) {}

FixedStepScheduler::FixedStepScheduler(const FixedStepConfig& config, Tick now)
    : config_(config), last_wall_(0), simulation_accumulator_(0),
      render_accumulator_(0), simulation_time_(0), pending_elapsed_(0),
      pending_paused_(0), pending_dropped_(0), paused_(false) {
    if (config_.simulation_step <= 0 || config_.render_period <= 0 ||
        config_.max_catch_up_steps == 0)
        throw std::invalid_argument("runtime intervals and catch-up limit must be positive");
    reset(now);
}

FrameSchedule FixedStepScheduler::advance(Tick now) {
    require_time(now);
    if (now < last_wall_) throw std::invalid_argument("frame clock moved backward");
    const Tick elapsed = now - last_wall_;
    const Tick render_total = add_time(render_accumulator_, elapsed);
    FrameSchedule result = {};
    result.elapsed = add_time(pending_elapsed_, elapsed);
    result.paused_time = add_time(pending_paused_, paused_ ? elapsed : 0);
    result.simulation_step = config_.simulation_step;
    result.dropped_simulation_time = pending_dropped_;
    result.paused = paused_;

    Tick accumulator = simulation_accumulator_;
    Tick simulated = simulation_time_;
    if (!paused_) {
        const Tick total = add_time(accumulator, elapsed);
        const std::uint64_t available = static_cast<std::uint64_t>(total / config_.simulation_step);
        const std::uint64_t limit = static_cast<std::uint64_t>(config_.max_catch_up_steps);
        result.simulation_steps = static_cast<std::size_t>((std::min)(available, limit));
        // Both products are bounded by total; neither can overflow Tick.
        const Tick performed = static_cast<Tick>(result.simulation_steps) * config_.simulation_step;
        const Tick discarded = static_cast<Tick>(available - result.simulation_steps) * config_.simulation_step;
        simulated = add_time(simulated, performed);
        result.dropped_simulation_time = add_time(result.dropped_simulation_time, discarded);
        accumulator = total % config_.simulation_step;
    }
    const Tick render_count = render_total / config_.render_period;
    result.render_due = render_count != 0;
    result.skipped_render_frames = render_count == 0 ? 0 : static_cast<std::uint64_t>(render_count - 1);
    result.interpolation = static_cast<double>(accumulator) / static_cast<double>(config_.simulation_step);
    result.simulation_time = simulated;

    last_wall_ = now;
    simulation_accumulator_ = accumulator;
    simulation_time_ = simulated;
    render_accumulator_ = render_total % config_.render_period;
    pending_elapsed_ = pending_paused_ = pending_dropped_ = 0;
    return result;
}

void FixedStepScheduler::set_paused(bool paused, Tick now) {
    require_time(now);
    if (now < last_wall_) throw std::invalid_argument("frame clock moved backward");
    if (paused == paused_) return;
    const Tick elapsed = now - last_wall_;
    const Tick render_total = add_time(render_accumulator_, elapsed);
    const Tick pending_elapsed = add_time(pending_elapsed_, elapsed);
    const Tick pending_paused = add_time(pending_paused_, paused_ ? elapsed : 0);
    Tick dropped = pending_dropped_;
    if (!paused_) dropped = add_time(dropped, add_time(simulation_accumulator_, elapsed));

    render_accumulator_ = render_total;
    pending_elapsed_ = pending_elapsed;
    pending_paused_ = pending_paused;
    pending_dropped_ = dropped;
    simulation_accumulator_ = 0;
    last_wall_ = now;
    paused_ = paused;
}

bool FixedStepScheduler::paused() const { return paused_; }
Tick FixedStepScheduler::simulation_time() const { return simulation_time_; }
const FixedStepConfig& FixedStepScheduler::config() const { return config_; }

void FixedStepScheduler::reset(Tick now) {
    require_time(now);
    last_wall_ = now;
    simulation_accumulator_ = render_accumulator_ = simulation_time_ = 0;
    pending_elapsed_ = pending_paused_ = pending_dropped_ = 0;
    paused_ = false;
}

const char* action_name(Action action) {
    switch (action) {
    case Action::LeftFlipper: return "left flipper";
    case Action::RightFlipper: return "right flipper";
    case Action::Plunger: return "plunger";
    case Action::NudgeLeft: return "nudge left";
    case Action::NudgeRight: return "nudge right";
    case Action::NudgeUp: return "nudge up";
    case Action::NewGame: return "new game";
    case Action::Pause: return "pause";
    case Action::Menu: return "menu";
    case Action::HiddenTest: return "hidden test";
    default: return "unknown";
    }
}

InputPolicy::InputPolicy()
    : release_on_focus_loss(true), release_on_disconnect(true),
      release_on_pause(true), block_unfocused_presses(true),
      block_paused_gameplay_presses(true) {}

InputState::InputState(const InputPolicy& policy)
    : policy_(policy), last_sequence_(0), focused_(true), paused_(false) {
    holds_.fill(0);
}

void InputState::ensure_sequence_space(std::size_t count) const {
    if (static_cast<std::uint64_t>(count) > (std::numeric_limits<std::uint64_t>::max)() - last_sequence_)
        throw std::overflow_error("input transition sequence exhausted");
}

void InputState::emit(InputSource source, Action action, bool pressed, TransitionCause cause) {
    ensure_sequence_space(1);
    InputTransition transition = {last_sequence_ + 1, source, action, pressed, cause};
    transitions_.push_back(transition);
    ++last_sequence_;
}

void InputState::set_source(InputSource source, Action action, bool pressed) {
    require_source(source);
    const std::size_t index = action_index(action);
    if (pressed && ((!focused_ && policy_.block_unfocused_presses) ||
        (paused_ && policy_.block_paused_gameplay_presses && gameplay_action(action)))) return;

    std::map<InputSource, SourceState>::iterator found = sources_.find(source);
    if (found == sources_.end()) {
        if (!pressed) return;
        SourceState empty = {};
        found = sources_.insert(std::make_pair(source, empty)).first;
    }
    if (found->second[index] == pressed) return;
    const bool edge = pressed ? holds_[index] == 0 : holds_[index] == 1;
    if (edge) emit(source, action, pressed, TransitionCause::Physical);
    found->second[index] = pressed;
    if (pressed) ++holds_[index];
    else --holds_[index];
    bool any = false;
    for (std::size_t i = 0; i < action_count; ++i) any = any || found->second[i];
    if (!any) sources_.erase(found);
}

bool InputState::down(Action action) const { return holds_[action_index(action)] != 0; }

void InputState::release_source(InputSource source, TransitionCause cause) {
    require_source(source);
    std::map<InputSource, SourceState>::iterator found = sources_.find(source);
    if (found == sources_.end()) return;
    std::size_t edges = 0;
    for (std::size_t i = 0; i < action_count; ++i)
        if (found->second[i] && holds_[i] == 1) ++edges;
    ensure_sequence_space(edges);
    for (std::size_t i = 0; i < action_count; ++i) {
        if (!found->second[i]) continue;
        if (holds_[i] == 1) emit(source, static_cast<Action>(i), false, cause);
        --holds_[i];
    }
    sources_.erase(found);
}

void InputState::release_all(TransitionCause cause) {
    std::size_t edges = 0;
    for (std::size_t i = 0; i < action_count; ++i) if (holds_[i] != 0) ++edges;
    ensure_sequence_space(edges);
    for (std::size_t i = 0; i < action_count; ++i) {
        if (holds_[i] != 0) emit(no_input_source, static_cast<Action>(i), false, cause);
    }
    sources_.clear();
    holds_.fill(0);
}

void InputState::disconnect_source(InputSource source) {
    require_source(source);
    if (policy_.release_on_disconnect) release_source(source, TransitionCause::Disconnected);
}

void InputState::set_focused(bool focused) {
    if (focused == focused_) return;
    if (!focused && policy_.release_on_focus_loss) release_all(TransitionCause::FocusLost);
    focused_ = focused;
}

void InputState::set_paused(bool paused) {
    if (paused == paused_) return;
    if (paused && policy_.release_on_pause) release_all(TransitionCause::Paused);
    paused_ = paused;
}

bool InputState::focused() const { return focused_; }
bool InputState::paused() const { return paused_; }
const InputPolicy& InputState::policy() const { return policy_; }

std::vector<InputTransition> InputState::drain_transitions() {
    std::vector<InputTransition> result;
    result.swap(transitions_);
    return result;
}

} // namespace runtime
} // namespace pinball
