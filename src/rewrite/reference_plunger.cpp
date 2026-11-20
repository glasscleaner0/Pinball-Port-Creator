#include "reference_plunger.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace pinball {
namespace reference {
namespace {
void valid_time(runtime::Tick value) {
    // Explicit high-bit test also covers 64-bit Tick on the 32-bit PPC target.
    if ((static_cast<std::uint64_t>(value) >> 63) != 0)
        throw std::invalid_argument("negative plunger time");
}
void valid_order(runtime::Tick before, runtime::Tick after) {
    valid_time(after);
    if (static_cast<std::uint64_t>(after) < static_cast<std::uint64_t>(before))
        throw std::invalid_argument("backward plunger time");
}
struct ChargeGrid {
    std::uint32_t maximum, quantum, steps;
    runtime::Tick full_time;
};
ChargeGrid grid_for(const PlungerConfig& config) {
    if (!config.image_count || !config.increments_per_image)
        throw std::invalid_argument("empty plunger charge or image count");
    const std::uint32_t limit = (std::numeric_limits<std::uint32_t>::max)();
    if (config.image_count > limit / config.increments_per_image)
        throw std::overflow_error("plunger charge divisor overflow");
    const double maximum = config.maximum_charge;
    if (!std::isfinite(maximum) || maximum <= 0 || std::floor(maximum) != maximum ||
        maximum > (std::numeric_limits<std::int32_t>::max)())
        throw std::invalid_argument("maximum plunger charge must be a positive signed32 integer");
    valid_time(config.cadence);
    valid_time(config.release_window);
    if (!config.cadence || !config.release_window)
        throw std::invalid_argument("nonpositive plunger interval");
    const std::uint32_t whole_maximum = static_cast<std::uint32_t>(maximum);
    const std::uint32_t divisor = static_cast<std::uint32_t>(config.image_count) * config.increments_per_image;
    const std::uint32_t quantum = whole_maximum / divisor;
    if (!quantum) throw std::invalid_argument("plunger configuration gives zero integer charge increment");
    const std::uint32_t steps = whole_maximum / quantum + (whole_maximum % quantum ? 1 : 0);
    const std::uint64_t intervals = steps - 1;
    const std::uint64_t largest = static_cast<std::uint64_t>((std::numeric_limits<runtime::Tick>::max)());
    if (intervals && static_cast<std::uint64_t>(config.cadence) > largest / intervals)
        throw std::overflow_error("plunger full pull duration overflow");
    const ChargeGrid result = {whole_maximum, quantum, steps,
        static_cast<runtime::Tick>(intervals * static_cast<std::uint64_t>(config.cadence))};
    return result;
}
runtime::Tick deadline(runtime::Tick now, runtime::Tick duration) {
    // Both operands have clear sign bits, so their unsigned sum cannot wrap.
    const std::uint64_t sum = static_cast<std::uint64_t>(now) +
        static_cast<std::uint64_t>(duration);
    if ((sum >> 63) != 0)
        throw std::overflow_error("plunger release deadline overflow");
    return static_cast<runtime::Tick>(sum);
}
}

PlungerConfig::PlungerConfig(std::size_t images, float maximum,
    runtime::Tick interval, runtime::Tick window, std::uint32_t subdivisions)
    : image_count(images), maximum_charge(maximum), cadence(interval),
      release_window(window), increments_per_image(subdivisions) {}

Plunger::Plunger(const PlungerConfig& config, runtime::Tick now)
    : config_(config), maximum_charge_(0), quantum_(0), full_steps_(0), full_pull_time_(0),
      charge_steps_(0), now_(0), press_time_(0), window_end_(0), held_(false), window_open_(false) {
    const ChargeGrid grid=grid_for(config_);
    maximum_charge_=grid.maximum;quantum_=grid.quantum;full_steps_=grid.steps;full_pull_time_=grid.full_time;
    reset(now);
}

void Plunger::advance(runtime::Tick now) {
    valid_order(now_, now);
    if (held_) {
        const std::uint64_t elapsed = static_cast<std::uint64_t>(now) -
            static_cast<std::uint64_t>(press_time_);
        const std::uint64_t intervals = elapsed / static_cast<std::uint64_t>(config_.cadence);
        // Compare before adding the immediate first step: UINT64_MAX never
        // needs to be incremented, nor does a huge gap need a callback loop.
        charge_steps_ = intervals >= static_cast<std::uint64_t>(full_steps_ - 1)
            ? full_steps_ : static_cast<std::uint32_t>(intervals) + 1;
    }
    if (window_open_ && static_cast<std::uint64_t>(now) >=
        static_cast<std::uint64_t>(window_end_)) {
        window_open_ = false;
        charge_steps_ = 0;
    }
    now_ = now;
}

bool Plunger::set_pressed(bool pressed, runtime::Tick now, runtime::TransitionCause cause) {
    // Work on a copy so even a release near the clock limit is transactional.
    Plunger next(*this);
    next.advance(now);
    if (cause != runtime::TransitionCause::Physical) {
        next.cancel(now);
        *this = next;
        return false;
    }
    bool released = false;
    if (pressed && !next.held_) {
        next.held_ = true;
        next.press_time_ = now;
        next.charge_steps_ = 1;
        next.window_open_ = false;
        next.window_end_ = 0;
    } else if (!pressed && next.held_) {
        next.window_end_ = deadline(now, config_.release_window);
        next.held_ = false;
        next.window_open_ = true;
        released = true;
    }
    *this = next;
    return released;
}

void Plunger::cancel(runtime::Tick now) {
    valid_order(now_, now);
    now_ = now;
    press_time_ = now;
    window_end_ = 0;
    charge_steps_ = 0;
    held_ = false;
    window_open_ = false;
}

void Plunger::reset(runtime::Tick now) {
    valid_time(now);
    now_ = now;
    press_time_ = now;
    window_end_ = 0;
    charge_steps_ = 0;
    held_ = false;
    window_open_ = false;
}

std::uint32_t Plunger::charge_units() const {
    const std::uint64_t amount=static_cast<std::uint64_t>(charge_steps_)*quantum_;
    return amount>=maximum_charge_?maximum_charge_:static_cast<std::uint32_t>(amount);
}
double Plunger::charge_value() const {
    return charge_units();
}
float Plunger::charge() const { return static_cast<float>(charge_value()); }
float Plunger::pull() const {
    return static_cast<float>(charge_value() / maximum_charge_);
}
std::size_t Plunger::image_frame() const {
    if (!held_) return 0;
    // Use the clamped integer charge, not a fraction of elapsed increments:
    // the final increment can be smaller when maximum is not a multiple of
    // quantum. The validated signed32 maximum keeps this product in uint64.
    return static_cast<std::size_t>(static_cast<std::uint64_t>(charge_units()) *
        static_cast<std::uint64_t>(config_.image_count - 1) / maximum_charge_);
}
bool Plunger::held() const { return held_; }
runtime::Tick Plunger::time() const { return now_; }
const PlungerConfig& Plunger::config() const { return config_; }
runtime::Tick Plunger::full_pull_time() const { return full_pull_time_; }

runtime::Tick Plunger::window_remaining(runtime::Tick now) {
    advance(now);
    return window_open_ ? window_end_ - now_ : 0;
}

float Plunger::boost(runtime::Tick now, float unit_random) {
    if (!std::isfinite(unit_random) || unit_random < 0 || unit_random > 1)
        throw std::invalid_argument("plunger random value outside unit interval");
    Plunger next(*this);
    next.advance(now);
    const double value = next.window_open_
        ? next.charge_value() * (1.0 + 0.1 * static_cast<double>(unit_random)) : 0.0;
    if (!std::isfinite(value) || value > (std::numeric_limits<float>::max)())
        throw std::overflow_error("plunger boost exceeds float range");
    *this = next;
    return static_cast<float>(value);
}

} // namespace reference
} // namespace pinball
