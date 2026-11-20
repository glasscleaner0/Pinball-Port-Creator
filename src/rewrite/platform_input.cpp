#include "platform_input.hpp"

#include <cmath>
#include <stdexcept>

namespace pinball {
namespace platform {
namespace {

std::size_t index(runtime::Action action) { return static_cast<std::size_t>(action); }

bool trigger_state(std::uint8_t value, bool was_down, const ControllerBindings& bindings) {
    return was_down ? value > bindings.trigger_release : value >= bindings.trigger_press;
}

} // namespace

ControllerSnapshot::ControllerSnapshot()
    : connected(false), buttons(0), left_trigger(0), right_trigger(0),
      left_stick_x(0), left_stick_y(0) {}

StickState left_stick(const ControllerSnapshot& snapshot) {
    StickState result = {0, 0};
    if (!snapshot.connected) return result;
    const double x = static_cast<double>(snapshot.left_stick_x) /
        (snapshot.left_stick_x < 0 ? 32768.0 : 32767.0);
    const double y = static_cast<double>(snapshot.left_stick_y) /
        (snapshot.left_stick_y < 0 ? 32768.0 : 32767.0);
    const double magnitude = std::sqrt(x * x + y * y);
    const double deadzone = .18;
    if (magnitude <= deadzone) return result;
    const double bounded = magnitude < 1.0 ? magnitude : 1.0;
    const double scale = (bounded - deadzone) / ((1.0 - deadzone) * magnitude);
    result.x = static_cast<float>(x * scale);
    result.y = static_cast<float>(y * scale);
    return result;
}

ControllerBindings::ControllerBindings()
    : buttons(), left_trigger(), right_trigger(), trigger_press(160), trigger_release(96) {}

ControllerBindings ControllerBindings::viewer_defaults() {
    ControllerBindings result;


    result.buttons[index(runtime::Action::Plunger)] = PadA | PadDpadDown;
    result.buttons[index(runtime::Action::NudgeLeft)] = PadLeftShoulder;
    result.buttons[index(runtime::Action::NudgeRight)] = PadRightShoulder;

    result.buttons[index(runtime::Action::NewGame)] = PadY;
    result.buttons[index(runtime::Action::Pause)] = PadStart;
    result.buttons[index(runtime::Action::HiddenTest)] = PadBack;
    result.left_trigger[index(runtime::Action::LeftFlipper)] = true;
    result.right_trigger[index(runtime::Action::RightFlipper)] = true;
    return result;
}

ControllerAdapter::ControllerAdapter(runtime::InputState& input, runtime::InputSource source,
                                     const ControllerBindings& bindings)
    : input_(input), source_(source), bindings_(bindings), physical_actions_(),
      connected_(false), ever_connected_(false), hidden_test_armed_(true),
      left_trigger_down_(false), right_trigger_down_(false) {
    if (source == runtime::no_input_source) throw std::invalid_argument("reserved input source");
    if (bindings.trigger_release >= bindings.trigger_press) {
        throw std::invalid_argument("trigger release must be below trigger press");
    }
}

void ControllerAdapter::update(const ControllerSnapshot& snapshot) {
    if (!snapshot.connected) { disconnect(); return; }
    if (!connected_) {
        hidden_test_armed_ = !ever_connected_;
        ever_connected_ = true;
    }
    connected_ = true;
    left_trigger_down_ = trigger_state(snapshot.left_trigger, left_trigger_down_, bindings_);
    right_trigger_down_ = trigger_state(snapshot.right_trigger, right_trigger_down_, bindings_);
    for (std::size_t a = 0; a < runtime::action_count; ++a) {
        bool pressed = (snapshot.buttons & bindings_.buttons[a]) != 0 ||
            (bindings_.left_trigger[a] && left_trigger_down_) ||
            (bindings_.right_trigger[a] && right_trigger_down_);
        if (a == index(runtime::Action::HiddenTest) && !hidden_test_armed_) {
            if (!pressed) hidden_test_armed_ = true;
            pressed = false;
        }
        if (pressed != physical_actions_[a]) {
            input_.set_source(source_, static_cast<runtime::Action>(a), pressed);
            physical_actions_[a] = pressed;
        }
    }
}

void ControllerAdapter::disconnect() {
    input_.disconnect_source(source_);
    if (input_.policy().release_on_disconnect) physical_actions_.fill(false);
    connected_ = false;
    left_trigger_down_ = false;
    right_trigger_down_ = false;
}

bool ControllerAdapter::connected() const { return connected_; }
const ControllerBindings& ControllerAdapter::bindings() const { return bindings_; }

} // namespace platform
} // namespace pinball

