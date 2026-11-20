#include "platform_input.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using pinball::runtime::Action;
using pinball::runtime::InputState;
using pinball::platform::ControllerAdapter;
using pinball::platform::ControllerBindings;
using pinball::platform::ControllerSnapshot;

void check(bool condition, const char* label) {
    if (!condition) throw std::runtime_error(label);
}

void buttons_and_sources() {
    InputState input;
    ControllerAdapter controller(input, 2);
    ControllerSnapshot state;
    state.connected = true;
    state.buttons = pinball::platform::PadLeftShoulder | pinball::platform::PadA;
    controller.update(state);
    controller.update(state);
    check(input.down(Action::NudgeLeft) && input.down(Action::Plunger), "mapped button presses");
    check(input.drain_transitions().size() == 2, "repeat snapshot emits no repeat transitions");
    input.set_source(1, Action::NudgeLeft, true);
    state.buttons = 0;
    controller.update(state);
    const std::vector<pinball::runtime::InputTransition> edges = input.drain_transitions();
    check(edges.size() == 1 && edges[0].action == Action::Plunger && !edges[0].pressed,
        "keyboard and controller holds OR together");
    controller.disconnect();
    check(input.down(Action::NudgeLeft), "disconnect preserves separate keyboard source");
    input.set_source(1, Action::NudgeLeft, false);
    check(!input.down(Action::NudgeLeft), "final source release emits up");
    state.buttons = pinball::platform::PadRightShoulder;
    controller.update(state);
    input.drain_transitions();
    state.connected = false;
    controller.update(state);
    const std::vector<pinball::runtime::InputTransition> disconnected = input.drain_transitions();
    check(disconnected.size() == 1 && !disconnected[0].pressed &&
        disconnected[0].cause == pinball::runtime::TransitionCause::Disconnected,
        "disconnect releases source with diagnostic cause");
    check(!controller.connected() && !input.down(Action::NudgeRight), "disconnected neutral state");
    state.connected = true;
    controller.update(state);
    check(input.down(Action::NudgeRight), "reconnection accepts currently held physical input");
}

void trigger_hysteresis_and_or() {
    InputState input;
    ControllerBindings bindings=ControllerBindings::viewer_defaults();
    bindings.buttons[static_cast<std::size_t>(Action::LeftFlipper)]=pinball::platform::PadX;
    ControllerAdapter controller(input, 2, bindings);
    ControllerSnapshot state;
    state.connected = true;
    state.left_trigger = 159;
    controller.update(state);
    check(!input.down(Action::LeftFlipper), "below trigger press threshold");
    state.left_trigger = 160;
    controller.update(state);
    state.left_trigger = 97;
    controller.update(state);
    check(input.down(Action::LeftFlipper) && input.drain_transitions().size() == 1,
        "trigger remains held inside hysteresis band");
    state.buttons = pinball::platform::PadX;
    state.left_trigger = 96;
    controller.update(state);
    check(input.down(Action::LeftFlipper) && input.drain_transitions().empty(),
        "button retains action when trigger releases");
    state.buttons = 0;
    controller.update(state);
    check(!input.down(Action::LeftFlipper) && input.drain_transitions().size() == 1,
        "aggregate action releases after last physical binding");
    state.left_trigger = 150;
    controller.update(state);
    check(!input.down(Action::LeftFlipper), "released trigger requires press threshold again");
}

void blocked_inputs_and_toggle_edges() {
    InputState input;
    ControllerAdapter controller(input, 2);
    ControllerSnapshot state;
    state.connected = true;
    state.buttons = pinball::platform::PadStart;
    controller.update(state);
    check(input.drain_transitions().size() == 1, "pause initial edge");
    input.set_paused(true);
    input.drain_transitions();
    controller.update(state);
    check(input.drain_transitions().empty(), "held pause does not retrigger after global release");
    state.buttons = 0; controller.update(state);
    state.buttons = pinball::platform::PadStart; controller.update(state);
    check(input.down(Action::Pause) && input.drain_transitions().size() == 1,
        "fresh pause press is available while paused");
    input.set_paused(false);
    state.buttons = pinball::platform::PadA;
    controller.update(state);
    check(input.down(Action::Plunger), "gameplay source restored on fresh press");
    input.set_focused(false); input.drain_transitions();
    controller.update(state); input.set_focused(true); controller.update(state);
    check(!input.down(Action::Plunger) && input.drain_transitions().empty(),
        "focus regain requires physical release and fresh press");
    state.buttons = 0; controller.update(state);
    state.buttons = pinball::platform::PadA; controller.update(state);
    check(input.down(Action::Plunger), "fresh press after focus regain");
}

void preserve_disconnect_policy() {
    pinball::runtime::InputPolicy policy;
    policy.release_on_disconnect = false;
    InputState input(policy);
    ControllerAdapter controller(input, 7);
    ControllerSnapshot state;
    state.connected = true;
    state.buttons = pinball::platform::PadA;
    state.left_trigger = 255;
    controller.update(state);
    input.drain_transitions();
    controller.disconnect();
    check(input.down(Action::Plunger) && input.down(Action::LeftFlipper),
        "custom disconnect policy retains requested source holds");
    check(input.drain_transitions().empty(), "preserved disconnect emits no release edge");
    state.buttons = 0;
    state.left_trigger = 0;
    controller.update(state);
    check(!input.down(Action::Plunger) && !input.down(Action::LeftFlipper),
        "neutral reconnect releases previously retained physical holds");
    check(input.drain_transitions().size() == 2, "retained reconnect releases each action once");
}
void user_control_layout() {
    InputState input;ControllerAdapter controller(input,9);
    ControllerSnapshot state;state.connected=true;
    state.buttons=pinball::platform::PadA | pinball::platform::PadDpadDown;
    controller.update(state);input.drain_transitions();
    state.buttons=pinball::platform::PadDpadDown;controller.update(state);
    check(input.down(Action::Plunger) && input.drain_transitions().empty(),"DpadDown retains plunger after A release");
    state.buttons=0;controller.update(state);
    check(!input.down(Action::Plunger) && input.drain_transitions().size()==1,"plunger releases only after both alternatives");
    state.buttons=pinball::platform::PadLeftShoulder|pinball::platform::PadRightShoulder;
    controller.update(state);
    check(input.down(Action::NudgeLeft) && input.down(Action::NudgeRight),"bumpers map to table shake");
    check(!input.down(Action::LeftFlipper) && !input.down(Action::RightFlipper),"bumpers do not flip");
    state.buttons=0;state.left_trigger=255;state.right_trigger=255;controller.update(state);
    check(input.down(Action::LeftFlipper) && input.down(Action::RightFlipper),"triggers flip");
    check(!input.down(Action::NudgeLeft) && !input.down(Action::NudgeRight),"triggers do not shake");
}
void stick_normalization() {
    using pinball::platform::left_stick;
    using pinball::platform::StickState;
    ControllerSnapshot state;
    check(state.left_stick_x == 0 && state.left_stick_y == 0,
        "new snapshot stick axes are neutral");
    state.left_stick_x = (std::numeric_limits<std::int16_t>::min)();
    state.left_stick_y = (std::numeric_limits<std::int16_t>::max)();
    StickState stick = left_stick(state);
    check(stick.x == 0 && stick.y == 0, "disconnected sticks ignore stale extreme axes");
    state.connected = true;
    stick = left_stick(state);
    check(std::fabs(stick.x + .7071067811865475) < .000001 &&
          std::fabs(stick.y - .7071067811865475) < .000001 &&
          double(stick.x)*stick.x + double(stick.y)*stick.y <= 1.000001,
        "signed extreme diagonal preserves direction and caps radial magnitude");

    state.left_stick_y = 0;
    stick = left_stick(state);
    check(stick.x == -1 && stick.y == 0, "negative32768 reaches the negative unit axis");
    state.left_stick_x = (std::numeric_limits<std::int16_t>::max)();
    stick = left_stick(state);
    check(stick.x == 1 && stick.y == 0, "positive32767 reaches the positive unit axis");
    state.left_stick_x = 0;
    state.left_stick_y = (std::numeric_limits<std::int16_t>::min)();
    stick = left_stick(state);
    check(stick.x == 0 && stick.y == -1, "vertical orientation is not inverted");

    state.left_stick_x = 0; state.left_stick_y = 0;
    stick = left_stick(state);
    check(stick.x == 0 && stick.y == 0, "center remains exactly zero");
    state.left_stick_x = 5898;
    stick = left_stick(state);
    check(stick.x == 0 && stick.y == 0, "integer sample just inside .18 deadzone is zero");
    state.left_stick_x = 5899;
    stick = left_stick(state);
    check(stick.x > 0 && stick.x < .0001f && stick.y == 0,
        "first sample outside .18 deadzone starts continuously near zero");
    state.left_stick_x = -16384;
    stick = left_stick(state);
    check(std::fabs(stick.x + .3902439024390244) < .000001 && stick.y == 0,
        "half negative axis rescales the remaining radial range");
    state.left_stick_x = 16384;
    stick = left_stick(state);
    check(std::fabs(stick.x - .3902625105798577) < .000001 && stick.y == 0,
        "half positive axis uses the full asymmetric signed range");
    state.left_stick_x = 4500; state.left_stick_y = 4500;
    stick = left_stick(state);
    check(stick.x > 0 && stick.x == stick.y,
        "diagonal just outside radial deadzone is not removed by axis deadzones");
    state.left_stick_x = 4000; state.left_stick_y = 4000;
    stick = left_stick(state);
    check(stick.x == 0 && stick.y == 0, "diagonal inside radial deadzone is neutral");
}

void hidden_test_toggle_guard() {
    using pinball::runtime::InputTransition;
    using pinball::runtime::TransitionCause;
    InputState input;
    ControllerAdapter controller(input, 12);
    ControllerSnapshot state;
    controller.update(state); // Disconnected polling is not an earlier connection.
    state.connected = true; state.buttons = pinball::platform::PadBack;
    controller.update(state);
    std::vector<InputTransition> edges = input.drain_transitions();
    check(edges.size() == 1 && edges[0].action == Action::HiddenTest &&
          edges[0].pressed && edges[0].cause == TransitionCause::Physical &&
          !input.down(Action::Menu),
        "Back on the first connection is one hidden-test press, never exit");
    controller.update(state);
    check(input.drain_transitions().empty(), "held Back cannot repeat a toggle");
    input.set_paused(true);
    edges = input.drain_transitions();
    check(edges.size() == 1 && !edges[0].pressed && edges[0].cause == TransitionCause::Paused,
        "pause releases held hidden-test with a nonphysical up edge");
    controller.update(state);
    check(input.drain_transitions().empty(), "held Back does not retoggle after pause release");
    state.buttons = 0; controller.update(state);
    state.buttons = pinball::platform::PadBack; controller.update(state);
    edges = input.drain_transitions();
    check(edges.size() == 1 && edges[0].pressed && edges[0].action == Action::HiddenTest,
        "a fresh Back press is available while paused");
    input.set_paused(false);
    controller.disconnect();
    edges = input.drain_transitions();
    check(edges.size() == 1 && !edges[0].pressed && edges[0].cause == TransitionCause::Disconnected,
        "disconnect releases hidden-test without a new toggle press");
    controller.disconnect();
    state.buttons = pinball::platform::PadBack | pinball::platform::PadA;
    state.left_trigger = 255;
    controller.update(state);
    edges = input.drain_transitions();
    check(!input.down(Action::HiddenTest) && !input.down(Action::Menu) &&
          input.down(Action::Plunger) && input.down(Action::LeftFlipper) && edges.size() == 2,
        "held Back must rearm after reconnect while other held controls resume");
    controller.update(state);
    check(input.drain_transitions().empty(), "repeated reconnect-held Back remains suppressed");
    state.buttons = pinball::platform::PadA; controller.update(state);
    check(input.drain_transitions().empty(), "releasing suppressed Back only arms the next press");
    state.buttons |= pinball::platform::PadBack; controller.update(state);
    edges = input.drain_transitions();
    check(edges.size() == 1 && edges[0].action == Action::HiddenTest && edges[0].pressed,
        "release then press after reconnect toggles exactly once");

    state.connected = false; controller.update(state); input.drain_transitions();
    state.connected = true; state.buttons = 0; state.left_trigger = 0;
    controller.update(state);
    check(input.drain_transitions().empty(), "neutral reconnect rearms without synthetic press");
    state.buttons = pinball::platform::PadBack; controller.update(state);
    edges = input.drain_transitions();
    check(edges.size() == 1 && edges[0].action == Action::HiddenTest && edges[0].pressed,
        "first press following a neutral reconnect is accepted");
}

void configurable_mapping() {
    ControllerBindings bindings;
    bindings.trigger_press = 220;
    bindings.trigger_release = 32;
    bindings.buttons[static_cast<std::size_t>(Action::Plunger)] =
        pinball::platform::PadX | pinball::platform::PadB;
    bindings.right_trigger[static_cast<std::size_t>(Action::Menu)] = true;
    InputState input;
    ControllerAdapter controller(input, 5, bindings);
    ControllerSnapshot state;
    state.connected = true; state.buttons = pinball::platform::PadX;
    state.right_trigger = 220;
    controller.update(state);
    check(input.down(Action::Plunger) && input.down(Action::Menu) &&
        !input.down(Action::RightFlipper), "custom mapping replaces temporary defaults");
    state.buttons = pinball::platform::PadB; state.right_trigger = 33;
    controller.update(state);
    check(input.drain_transitions().size() == 2, "custom bindings OR and hysteresis");
    state.buttons = 0; state.right_trigger = 32;
    controller.update(state);
    check(!input.down(Action::Plunger) && !input.down(Action::Menu), "custom release boundary");
    bool rejected = false;
    bindings.trigger_release = bindings.trigger_press;
    try { ControllerAdapter invalid(input, 6, bindings); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "invalid hysteresis thresholds rejected");
    rejected = false;
    try { ControllerAdapter invalid(input, pinball::runtime::no_input_source); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "reserved source token rejected");
}
}

int main() {
    try {
        buttons_and_sources(); trigger_hysteresis_and_or();
        blocked_inputs_and_toggle_edges(); preserve_disconnect_policy(); user_control_layout();
        stick_normalization(); hidden_test_toggle_guard(); configurable_mapping();
        std::cout << "Platform input tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Platform input tests failed: " << error.what() << '\n';
        return 1;
    }
}

