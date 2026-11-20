#include "table_shake.hpp"

#include <climits>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace pinball::visual;
using pinball::runtime::Tick;
using pinball::scene::Rect;
using pinball::scene::Surface;

void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& operation, const std::string& fragment)
{
    try { operation(); }
    catch (const std::invalid_argument& error) {
        require(std::string(error.what()).find(fragment) != std::string::npos,
            "wrong table-shake rejection: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("expected table-shake rejection containing " + fragment);
}

void position(const TableShake& shake, Tick now, int dx, int dy)
{
    const ShakeOffset actual = shake.offset(now);
    require(actual.dx == dx && actual.dy == dy, "incorrect held table offset");
}

void held_lifetime_and_release()
{
    TableShake shake;
    require(!shake.active(0), "new shake must be inactive");
    shake.trigger(ShakeDirection::Left, 100);
    position(shake, 100, -2, -1);
    position(shake, 400000099, -2, -1);
    position(shake, 400000100, 0, 0);
    require(!shake.active(400000100), "hold must expire at the exact duration");
    // offset() is a pure query, not a hidden timer advancement.
    position(shake, 101, -2, -1);
    shake.release(ShakeDirection::Left, 102);
    position(shake, 102, 0, 0);
    shake.trigger(ShakeDirection::Right, 103);
    position(shake, 103, 2, -1);
    shake.cancel(104);
    position(shake, 104, 0, 0);
    shake.trigger(ShakeDirection::Up, 105);
    position(shake, 105, 0, -1);
    shake.reset();
    position(shake, 0, 0, 0);
    require(shake.config().duration == 400000000, "default hold must be400ms");
}

void concurrent_bounded_holds()
{
    TableShake shake(ShakeConfig(3, 2, 20));
    shake.trigger(ShakeDirection::Left, 0);
    shake.trigger(ShakeDirection::Right, 5);
    shake.trigger(ShakeDirection::Up, 10);
    position(shake, 10, 0, -6);
    position(shake, 20, 3, -4); // Left expires independently.
    shake.trigger(ShakeDirection::Right, 21);
    shake.trigger(ShakeDirection::Right, 22); // Refresh, never stack.
    position(shake, 22, 3, -4);
    shake.release(ShakeDirection::Up, 23);
    position(shake, 23, 3, -2);
    position(shake, 41, 3, -2);
    position(shake, 42, 0, 0);
    TableShake fractional(ShakeConfig(2.9, 1.9, 5));
    fractional.trigger(ShakeDirection::Left, 0);
    position(fractional, 0, -2, -1);
    TableShake zero(ShakeConfig(0, 0, 5));
    zero.trigger(ShakeDirection::Right, 0);
    require(zero.active(0), "zero-amplitude hold remains logically active");
    position(zero, 0, 0, 0);
    require(!zero.active(5), "zero-amplitude hold must still expire");
    TableShake maximum(ShakeConfig(static_cast<double>(INT_MAX),
        static_cast<double>(INT_MAX / 3), 1));
    maximum.trigger(ShakeDirection::Left, 0);
    maximum.trigger(ShakeDirection::Right, 0);
    maximum.trigger(ShakeDirection::Up, 0);
    position(maximum, 0, 0, -(INT_MAX / 3) * 3);
}

void clock_boundaries_and_rejections()
{
    const Tick boundaries[] = {2147483648LL, 4294967296LL};
    for (std::size_t i = 0; i < 2; ++i) {
        const Tick boundary = boundaries[i];
        TableShake shake(ShakeConfig(2, 1, 20), boundary - 10);
        shake.trigger(ShakeDirection::Left, boundary - 10);
        position(shake, boundary + 9, -2, -1);
        position(shake, boundary + 10, 0, 0);
        shake.cancel(boundary + 10);
        require(!shake.active(boundary + 10), "cancel must work above32-bit boundaries");
    }
    const Tick maximum = (std::numeric_limits<Tick>::max)();
    TableShake near_end(ShakeConfig(2, 1, 20), maximum - 10);
    near_end.trigger(ShakeDirection::Up, maximum - 10);
    position(near_end, maximum, 0, -1);
    near_end.cancel(maximum);
    position(near_end, maximum, 0, 0);
    TableShake longest(ShakeConfig(2, 1, maximum));
    longest.trigger(ShakeDirection::Right, 0);
    position(longest, 2147483648LL, 2, -1);
    position(longest, 4294967296LL, 2, -1);
    position(longest, maximum - 1, 2, -1);
    position(longest, maximum, 0, 0);
    rejects([] { TableShake invalid(ShakeConfig(), -1); }, "nonnegative");
    TableShake shake(ShakeConfig(), 10);
    shake.trigger(ShakeDirection::Right, 11);
    rejects([&] { shake.offset(-1); }, "nonnegative");
    rejects([&] { shake.active(10); }, "precede");
    rejects([&] { shake.trigger(ShakeDirection::Left, 10); }, "precede");
    rejects([&] { shake.release(ShakeDirection::Right, 10); }, "precede");
    rejects([&] { shake.cancel(10); }, "precede");
    rejects([&] { shake.reset(-1); }, "nonnegative");
    rejects([&] { shake.trigger(static_cast<ShakeDirection>(99), 12); }, "direction");
    rejects([&] { shake.release(static_cast<ShakeDirection>(99), 12); }, "direction");
    position(shake, 11, 2, -1); // Rejections preserve every active contribution.
    shake.reset(1);
    position(shake, 1, 0, 0);
    shake.trigger(ShakeDirection::Up, 2);
    position(shake, 2, 0, -1);
}

void config_rejections()
{
    const double nan = (std::numeric_limits<double>::quiet_NaN)();
    const double infinity = (std::numeric_limits<double>::infinity)();
    rejects([] { TableShake invalid(ShakeConfig(1, 1, 0)); }, "duration");
    rejects([] { TableShake invalid(ShakeConfig(1, 1, -1)); }, "duration");
    rejects([&] { TableShake invalid(ShakeConfig(nan, 1, 1)); }, "finite");
    rejects([&] { TableShake invalid(ShakeConfig(1, infinity, 1)); }, "finite");
    rejects([] { TableShake invalid(ShakeConfig(-1, 1, 1)); }, "nonnegative");
    rejects([] { TableShake invalid(ShakeConfig(1, -1, 1)); }, "nonnegative");
    rejects([] { TableShake invalid(ShakeConfig(static_cast<double>(INT_MAX) + 1, 1, 1)); },
        "coordinate");
    rejects([] { TableShake invalid(ShakeConfig(1, static_cast<double>(INT_MAX / 3) + 1, 1)); },
        "coordinate");
}

Surface patterned()
{
    Surface result(6, 4);
    for (std::size_t i = 0; i < result.depth.size(); ++i) {
        result.pixels[i * 4U] = static_cast<std::uint8_t>(i + 1U);
        result.pixels[i * 4U + 1U] = static_cast<std::uint8_t>(100U + i);
        result.pixels[i * 4U + 2U] = static_cast<std::uint8_t>(200U - i);
        result.pixels[i * 4U + 3U] = static_cast<std::uint8_t>(30U + i);
        result.depth[i] = static_cast<std::uint16_t>(500U + i * 7U);
    }
    return result;
}

bool same_surface(const Surface& left, const Surface& right)
{
    return left.width == right.width && left.height == right.height &&
        left.pixels == right.pixels && left.depth == right.depth;
}

void same_sample(const Surface& actual, std::size_t target,
    const Surface& original, std::size_t source)
{
    for (std::size_t c = 0; c < 4; ++c)
        require(actual.pixels[target * 4U + c] == original.pixels[source * 4U + c],
            "RGBA must translate without smearing or altering alpha");
    require(actual.depth[target] == original.depth[source], "depth must follow the exact color sample");
}

void black_sample(const Surface& surface, std::size_t index)
{
    require(surface.pixels[index * 4U] == 0 && surface.pixels[index * 4U + 1U] == 0 &&
        surface.pixels[index * 4U + 2U] == 0 && surface.pixels[index * 4U + 3U] == 255 &&
        surface.depth[index] == 65535, "newly uncovered edges must be opaque black with far depth");
}

void clipped_color_and_depth_moves()
{
    const Surface original = patterned();
    Surface frame = original;
    shift_region(frame, Rect(1, 1, 3, 2), 1, 0);
    black_sample(frame, 7); black_sample(frame, 13);
    same_sample(frame, 8, original, 7); same_sample(frame, 9, original, 8);
    same_sample(frame, 14, original, 13); same_sample(frame, 15, original, 14);
    for (std::size_t y = 0; y < 4; ++y)
        for (std::size_t x = 0; x < 6; ++x)
            if (x < 1 || x >= 4 || y < 1 || y >= 3)
                same_sample(frame, y * 6 + x, original, y * 6 + x);
    frame = original;
    shift_region(frame, Rect(0, 0, 3, 4), -1, -1);
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 6; ++x) {
            const std::size_t index = y * 6 + x;
            if (x >= 3) same_sample(frame, index, original, index); // Sidebar stays anchored.
            else if (x < 2 && y < 3) same_sample(frame, index, original, (y + 1) * 6 + x + 1);
            else black_sample(frame, index);
        }
    }
    frame = original;
    shift_region(frame, Rect(-2, -1, 5, 3), 1, 1); // Clipped ROI is[0,3)x[0,2).
    same_sample(frame, 7, original, 0); same_sample(frame, 8, original, 1);
    black_sample(frame, 0); black_sample(frame, 1); black_sample(frame, 2); black_sample(frame, 6);
    same_sample(frame, 3, original, 3); same_sample(frame, 12, original, 12);
    frame = original;
    shift_region(frame, Rect(4, 2, INT_MAX, INT_MAX), -1, -1);
    same_sample(frame, 16, original, 23);
    black_sample(frame, 17); black_sample(frame, 22); black_sample(frame, 23);
    same_sample(frame, 15, original, 15);
}

void extreme_regions_no_wrap_and_fresh_frames()
{
    const Surface original = patterned();
    const Rect empty_regions[] = {Rect(INT_MIN, INT_MIN, INT_MAX, INT_MAX),
        Rect(INT_MAX, INT_MAX, INT_MAX, INT_MAX), Rect(1, 1, 0, 3), Rect(1, 1, -1, 3),
        Rect(1, 1, 3, -1)};
    for (std::size_t i = 0; i < sizeof(empty_regions) / sizeof(empty_regions[0]); ++i) {
        Surface frame = original;
        shift_region(frame, empty_regions[i], INT_MIN, INT_MAX);
        require(same_surface(frame, original), "empty or disjoint ROI must not change any sample");
    }
    const int extremes[] = {INT_MIN, INT_MAX};
    for (std::size_t i = 0; i < 2; ++i) {
        Surface frame = original;
        shift_region(frame, Rect(1, 1, 3, 2), extremes[i], extremes[1 - i]);
        for (std::size_t y = 0; y < 4; ++y)
            for (std::size_t x = 0; x < 6; ++x)
                if (x >= 1 && x < 4 && y >= 1 && y < 3) black_sample(frame, y * 6 + x);
                else same_sample(frame, y * 6 + x, original, y * 6 + x);
    }
    Surface frame = original;
    shift_region(frame, Rect(INT_MIN, INT_MAX, INT_MAX, INT_MIN), 0, 0);
    require(same_surface(frame, original), "zero offset must be byte-exact no-op");
    shift_region(frame, Rect(0, 0, 6, 4), 6, 0);
    for (std::size_t i = 0; i < frame.depth.size(); ++i) black_sample(frame, i);
    // Caller rebuilds each frame, so the effect cannot drift or modify background.
    frame = original;
    TableShake shake;
    shake.trigger(ShakeDirection::Left, 0);
    const ShakeOffset delta = shake.offset(0);
    shift_region(frame, Rect(0, 0, 3, 4), delta.dx, delta.dy);
    require(!same_surface(frame, original), "live shake must visibly move the rendered table");
    same_sample(original, 0, patterned(), 0);
    shake.cancel(1);
    frame = original;
    const ShakeOffset settled = shake.offset(1);
    shift_region(frame, Rect(0, 0, 3, 4), settled.dx, settled.dy);
    require(same_surface(frame, original), "fresh settled frame must exactly restore color and depth");
}

void malformed_surface_rejections()
{
    Surface missing;
    rejects([&] { shift_region(missing, Rect(0, 0, 1, 1), 1, 1); }, "positive");
    Surface malformed = patterned();
    malformed.depth.pop_back();
    rejects([&] { shift_region(malformed, Rect(0, 0, 6, 4), 0, 0); }, "storage");
    malformed = patterned(); malformed.pixels.pop_back();
    rejects([&] { shift_region(malformed, Rect(0, 0, 6, 4), 1, 0); }, "storage");
    Surface huge;
    huge.width = static_cast<std::uint32_t>(INT_MAX) + 1U;
    huge.height = 1;
    rejects([&] { shift_region(huge, Rect(0, 0, 1, 1), 1, 1); }, "coordinate");
    huge.width = static_cast<std::uint32_t>(INT_MAX); huge.height = huge.width;
    const std::uint64_t bytes = static_cast<std::uint64_t>(huge.width) * huge.height * 4U;
    if (bytes > (std::numeric_limits<std::size_t>::max)())
        rejects([&] { shift_region(huge, Rect(0, 0, 1, 1), 1, 1); }, "overflows");
    else if (bytes > huge.pixels.max_size() || bytes / 4U > huge.depth.max_size())
        rejects([&] { shift_region(huge, Rect(0, 0, 1, 1), 1, 1); }, "capacity");
    else rejects([&] { shift_region(huge, Rect(0, 0, 1, 1), 1, 1); }, "storage");
}
} // namespace

int main()
{
    try {
        held_lifetime_and_release();
        concurrent_bounded_holds();
        clock_boundaries_and_rejections();
        config_rejections();
        clipped_color_and_depth_moves();
        extreme_regions_no_wrap_and_fresh_frames();
        malformed_surface_rejections();
        std::cout << "table shake tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "table shake test failure: " << error.what() << '\n';
        return 1;
    }
}
