#include "table_shake.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pinball {
namespace visual {
namespace {

bool negative(runtime::Tick time)
{
    return (static_cast<std::uint64_t>(time) >> 63U) != 0;
}

void require_time(runtime::Tick time)
{
    if (negative(time))
        throw std::invalid_argument("table shake time must be nonnegative");
}

std::size_t direction_index(ShakeDirection direction)
{
    switch (direction) {
    case ShakeDirection::Left: return 0;
    case ShakeDirection::Right: return 1;
    case ShakeDirection::Up: return 2;
    }
    throw std::invalid_argument("unknown table shake direction");
}

int pixel_amplitude(double value, int maximum)
{
    if (!std::isfinite(value) || value < 0)
        throw std::invalid_argument("table shake amplitude must be finite and nonnegative");
    const double pixels = std::floor(value);
    if (pixels > static_cast<double>(maximum))
        throw std::invalid_argument("table shake amplitude exceeds coordinate range");
    return static_cast<int>(pixels);
}

void validate_surface(const scene::Surface& surface)
{
    if (surface.width == 0 || surface.height == 0)
        throw std::invalid_argument("table shift surface dimensions must be positive");
    const std::uint32_t coordinate_max =
        static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    if (surface.width > coordinate_max || surface.height > coordinate_max)
        throw std::invalid_argument("table shift surface exceeds coordinate range");
    const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
    if (static_cast<std::size_t>(surface.height) > maximum / surface.width / 4U)
        throw std::invalid_argument("table shift RGBA extent overflows address space");
    const std::size_t count = static_cast<std::size_t>(surface.width) * surface.height;
    if (count * 4U > surface.pixels.max_size() || count > surface.depth.max_size())
        throw std::invalid_argument("table shift extent exceeds vector capacity");
    if (surface.pixels.size() != count * 4U || surface.depth.size() != count)
        throw std::invalid_argument("table shift dimensions do not match color/depth storage");
}

bool live(runtime::Tick now, runtime::Tick start, runtime::Tick duration)
{
    // No deadline addition: a hold beginning near INT64_MAX remains safe.
    return static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(start) <
        static_cast<std::uint64_t>(duration);
}

} // namespace

ShakeConfig::ShakeConfig(double horizontal, double vertical, runtime::Tick hold_duration)
    : horizontal_pixels(horizontal), vertical_pixels(vertical), duration(hold_duration)
{
}

TableShake::TableShake(const ShakeConfig& config, runtime::Tick now)
    : config_(config), horizontal_(0), vertical_(0), last_event_(now)
{
    require_time(now);
    if (negative(config_.duration) || config_.duration == 0)
        throw std::invalid_argument("table shake duration must be positive");
    horizontal_ = pixel_amplitude(config_.horizontal_pixels, (std::numeric_limits<int>::max)());
    vertical_ = pixel_amplitude(config_.vertical_pixels, (std::numeric_limits<int>::max)() / 3);
    started_.fill(now);
    held_.fill(false);
}

void TableShake::validate_now(runtime::Tick now) const
{
    require_time(now);
    if (static_cast<std::uint64_t>(now) < static_cast<std::uint64_t>(last_event_))
        throw std::invalid_argument("table shake time must not precede the last command");
}

void TableShake::trigger(ShakeDirection direction, runtime::Tick now)
{
    const std::size_t index = direction_index(direction);
    validate_now(now);
    started_[index] = now;
    held_[index] = true;
    last_event_ = now;
}

void TableShake::release(ShakeDirection direction, runtime::Tick now)
{
    const std::size_t index = direction_index(direction);
    validate_now(now);
    held_[index] = false;
    last_event_ = now;
}

void TableShake::cancel(runtime::Tick now)
{
    validate_now(now);
    held_.fill(false);
    last_event_ = now;
}

void TableShake::reset(runtime::Tick now)
{
    require_time(now);
    started_.fill(now);
    held_.fill(false);
    last_event_ = now;
}

ShakeOffset TableShake::offset(runtime::Tick now) const
{
    validate_now(now);
    const bool left = held_[0] && live(now, started_[0], config_.duration);
    const bool right = held_[1] && live(now, started_[1], config_.duration);
    const bool up = held_[2] && live(now, started_[2], config_.duration);
    ShakeOffset result = {0, 0};
    if (left) { result.dx -= horizontal_; result.dy -= vertical_; }
    if (right) { result.dx += horizontal_; result.dy -= vertical_; }
    if (up) result.dy -= vertical_;
    return result;
}

bool TableShake::active(runtime::Tick now) const
{
    validate_now(now);
    for (std::size_t i = 0; i < held_.size(); ++i)
        if (held_[i] && live(now, started_[i], config_.duration)) return true;
    return false;
}

const ShakeConfig& TableShake::config() const
{
    return config_;
}

void shift_region(scene::Surface& surface, scene::Rect region, int dx, int dy)
{
    validate_surface(surface);
    if ((dx == 0 && dy == 0) || region.width <= 0 || region.height <= 0) return;
    const std::int64_t left = (std::max)(std::int64_t(0), std::int64_t(region.x));
    const std::int64_t top = (std::max)(std::int64_t(0), std::int64_t(region.y));
    const std::int64_t right = (std::min)(std::int64_t(surface.width),
        std::int64_t(region.x) + region.width);
    const std::int64_t bottom = (std::min)(std::int64_t(surface.height),
        std::int64_t(region.y) + region.height);
    if (left >= right || top >= bottom) return;

    const std::size_t width = static_cast<std::size_t>(right - left);
    const std::size_t height = static_cast<std::size_t>(bottom - top);
    const std::size_t count = width * height; // A subset of the validated surface.
    // Finish both allocations before modifying any target samples.
    std::vector<std::uint8_t> colors(count * 4U);
    std::vector<std::uint16_t> depths(count);
    for (std::size_t y = 0; y < height; ++y) {
        const std::size_t source = (static_cast<std::size_t>(top) + y) * surface.width +
            static_cast<std::size_t>(left);
        const std::size_t scratch = y * width;
        std::copy_n(surface.pixels.data() + source * 4U, width * 4U,
            colors.data() + scratch * 4U);
        std::copy_n(surface.depth.data() + source, width, depths.data() + scratch);
    }
    for (std::int64_t y = top; y < bottom; ++y) {
        for (std::int64_t x = left; x < right; ++x) {
            const std::size_t target = static_cast<std::size_t>(y) * surface.width +
                static_cast<std::size_t>(x);
            const std::int64_t source_x = x - std::int64_t(dx);
            const std::int64_t source_y = y - std::int64_t(dy);
            if (source_x >= left && source_x < right && source_y >= top && source_y < bottom) {
                const std::size_t source = static_cast<std::size_t>(source_y - top) * width +
                    static_cast<std::size_t>(source_x - left);
                std::copy_n(colors.data() + source * 4U, 4U, surface.pixels.data() + target * 4U);
                surface.depth[target] = depths[source];
            } else {
                surface.pixels[target * 4U] = 0;
                surface.pixels[target * 4U + 1U] = 0;
                surface.pixels[target * 4U + 2U] = 0;
                surface.pixels[target * 4U + 3U] = 255;
                surface.depth[target] = 65535U;
            }
        }
    }
}

} // namespace visual
} // namespace pinball
