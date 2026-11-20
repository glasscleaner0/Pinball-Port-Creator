#ifndef PINBALL_REWRITE_PLATFORM_INPUT_HPP
#define PINBALL_REWRITE_PLATFORM_INPUT_HPP

#include "runtime.hpp"

#include <array>
#include <cstdint>

namespace pinball {
namespace platform {

// Portable masks, deliberately independent of any platform's button constants.
enum PadButton : std::uint32_t {
    PadA = 1u << 0, PadB = 1u << 1, PadX = 1u << 2, PadY = 1u << 3,
    PadStart = 1u << 4, PadBack = 1u << 5,
    PadLeftShoulder = 1u << 6, PadRightShoulder = 1u << 7,
    PadDpadLeft = 1u << 8, PadDpadRight = 1u << 9,
    PadDpadUp = 1u << 10, PadDpadDown = 1u << 11,
    PadLeftThumb = 1u << 12, PadRightThumb = 1u << 13
};

struct ControllerSnapshot {
    bool connected;
    std::uint32_t buttons;
    std::uint8_t left_trigger;
    std::uint8_t right_trigger;
    std::int16_t left_stick_x;
    std::int16_t left_stick_y;
    ControllerSnapshot();
};

struct StickState { float x, y; };
// Raw controller orientation: positive X is right, positive Y is up.
// Normalize the full signed range, apply a .18 radial deadzone rescaled to
// unit magnitude, and cap diagonals at that magnitude. Disconnected is zero.
StickState left_stick(const ControllerSnapshot& snapshot);

struct ControllerBindings {
    std::array<std::uint32_t, runtime::action_count> buttons;
    std::array<bool, runtime::action_count> left_trigger;
    std::array<bool, runtime::action_count> right_trigger;
    std::uint8_t trigger_press;
    std::uint8_t trigger_release;
    ControllerBindings(); // Empty mapping, press160/release96 hysteresis.
    // Current user-selected controls; Menu/exit is unbound, Back is HiddenTest.
    static ControllerBindings viewer_defaults();
};

// Buttons/triggers OR within one source; InputState ORs distinct sources.
// Only physical action changes are submitted. A global release on focus loss
// or pause requires a fresh physical press, preventing held toggle retriggers.
// HiddenTest additionally requires release after reconnection; an initially
// connected held button is accepted. Other actions retain reconnect behavior.
// InputState and the adapter must share a lifetime; this source must be unique.
class ControllerAdapter {
public:
    ControllerAdapter(runtime::InputState& input, runtime::InputSource source,
        const ControllerBindings& bindings = ControllerBindings::viewer_defaults());
    void update(const ControllerSnapshot& snapshot);
    void disconnect();
    bool connected() const;
    const ControllerBindings& bindings() const;
private:
    runtime::InputState& input_;
    runtime::InputSource source_;
    ControllerBindings bindings_;
    std::array<bool, runtime::action_count> physical_actions_;
    bool connected_;
    bool ever_connected_;
    bool hidden_test_armed_;
    bool left_trigger_down_;
    bool right_trigger_down_;
};

} // namespace platform
} // namespace pinball

#endif
