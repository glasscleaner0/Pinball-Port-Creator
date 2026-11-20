#include "reference_hole.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace pinball {
namespace reference {
namespace {

void require_time(runtime::Tick value)
{
    if ((static_cast<std::uint64_t>(value) >> 63U) != 0)
        throw std::invalid_argument("hole drop time must be nonnegative");
}

void validate_config(const HoleDropConfig& config)
{
    require_time(config.hold);
    require_time(config.cadence);
    if (config.hold == 0 || config.cadence == 0)
        throw std::invalid_argument("hole drop hold and cadence must be positive");
    if (!std::isfinite(config.gravity_acceleration) || config.gravity_acceleration < 0)
        throw std::invalid_argument("hole drop acceleration must be finite and nonnegative");
}

float finite_float(double value)
{
    const double maximum = (std::numeric_limits<float>::max)();
    if (!std::isfinite(value) || value > maximum || value < -maximum)
        throw std::overflow_error("hole drop result exceeds finite float range");
    return static_cast<float>(value);
}

} // namespace

HoleDropConfig::HoleDropConfig(runtime::Tick hold_time, float acceleration,
    runtime::Tick fall_cadence)
    : hold(hold_time), gravity_acceleration(acceleration), cadence(fall_cadence)
{
    validate_config(*this);
}

HoleDrop::HoleDrop(const HoleDropConfig& config, runtime::Tick now)
    : config_(config), now_(0), captured_at_(0), captured_z_(0), landing_z_(0),
      height_(0), vertical_velocity_(0), captured_(false), falling_(false), landed_(false)
{
    validate_config(config_); // Public config fields may have been changed.
    reset(now);
}

void HoleDrop::validate_now(runtime::Tick now) const
{
    require_time(now);
    if (static_cast<std::uint64_t>(now) < static_cast<std::uint64_t>(now_))
        throw std::invalid_argument("hole drop time must not move backward");
}

void HoleDrop::capture(float z, float landing_z, runtime::Tick now)
{
    validate_now(now);
    if (!std::isfinite(z) || !std::isfinite(landing_z))
        throw std::invalid_argument("hole drop heights must be finite");
    if (captured_)
        throw std::logic_error("hole drop is already captured");
    now_ = now;
    captured_at_ = now;
    captured_z_ = z;
    landing_z_ = landing_z;
    height_ = z;
    vertical_velocity_ = 0;
    captured_ = true;
    falling_ = false;
    landed_ = false;
}

bool HoleDrop::advance(runtime::Tick now)
{
    validate_now(now);
    if (!captured_) {
        now_ = now;
        return false;
    }
    const std::uint64_t elapsed = static_cast<std::uint64_t>(now) -
        static_cast<std::uint64_t>(captured_at_);
    const std::uint64_t hold = static_cast<std::uint64_t>(config_.hold);
    if (elapsed < hold) {
        now_ = now;
        return false;
    }
    const std::uint64_t ticks = (elapsed - hold) /
        static_cast<std::uint64_t>(config_.cadence);
    float next_height = captured_z_;
    float next_velocity = 0;
    bool landing = false;
    if (ticks != 0) {
        const double count = static_cast<double>(ticks);
        const double per_tick = static_cast<double>(config_.gravity_acceleration) *
            (static_cast<double>(config_.cadence) / runtime::ticks_per_second);
        // Tick counts are bounded by INT64_MAX. With finite float inputs,
        // count*(count+1)/2 and the product stay within double range. No loop,
        // integer triangular-number multiplication or deadline addition occurs.
        const double descent = per_tick * (count * (count + 1.0) * 0.5);
        const double gap = static_cast<double>(captured_z_) - landing_z_;
        landing = captured_z_ <= landing_z_ || descent >= gap;
        if (landing) {
            next_height = landing_z_;
        } else {
            next_height = finite_float(static_cast<double>(captured_z_) - descent);
            next_velocity = finite_float(-per_tick * count);
        }
    }
    // Results are checked before changing any state, including the clock.
    now_ = now;
    height_ = next_height;
    vertical_velocity_ = next_velocity;
    falling_ = !landing;
    captured_ = !landing;
    landed_ = landing;
    return landing;
}

void HoleDrop::reset(runtime::Tick now)
{
    require_time(now);
    now_ = now;
    captured_at_ = now;
    captured_z_ = 0;
    landing_z_ = 0;
    height_ = 0;
    vertical_velocity_ = 0;
    captured_ = false;
    falling_ = false;
    landed_ = false;
}

bool HoleDrop::captured() const { return captured_; }
bool HoleDrop::falling() const { return falling_; }
bool HoleDrop::landed() const { return landed_; }
float HoleDrop::height() const { return height_; }
float HoleDrop::vertical_velocity() const { return vertical_velocity_; }
runtime::Tick HoleDrop::time() const { return now_; }

runtime::Tick HoleDrop::until_boundary() const
{
    if (!captured_) return 0;
    const std::uint64_t elapsed = static_cast<std::uint64_t>(now_) -
        static_cast<std::uint64_t>(captured_at_);
    const std::uint64_t hold = static_cast<std::uint64_t>(config_.hold);
    if (elapsed < hold)
        return static_cast<runtime::Tick>(hold - elapsed);
    const std::uint64_t cadence = static_cast<std::uint64_t>(config_.cadence);
    return static_cast<runtime::Tick>(cadence - (elapsed - hold) % cadence);
}

} // namespace reference
} // namespace pinball
