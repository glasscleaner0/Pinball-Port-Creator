#include "reference_plunger.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using pinball::reference::Plunger;
using pinball::reference::PlungerConfig;
using pinball::runtime::Tick;
using pinball::runtime::TransitionCause;
const Tick ms = 1000000;
const Tick interval = 25 * ms;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void close(double actual, double expected, const char* message, double tolerance = .000001) {
    check(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance, message);
}
template<class Exception, class Operation>
void throws(Operation operation, const char* message) {
    try { operation(); } catch (const Exception&) { return; }
    throw std::runtime_error(message);
}

void quantization_and_art() {
    Plunger p(PlungerConfig(8));
    check(!p.held() && p.charge() == 0 && p.image_frame() == 0, "initial state");
    check(!p.set_pressed(true, 0), "press opened release window");
    for (unsigned k = 1; k <= 100; ++k) {
        const Tick now = static_cast<Tick>(k - 1) * interval;
        p.advance(now);
        close(p.charge(), static_cast<double>(k), "quantized charge");
        close(p.pull(), static_cast<double>(k) / 100, "quantized fraction");
        check(p.image_frame() == (7 * k) / 100, "exact art boundary");
        check(p.image_frame() != 7 || k == 100, "full art appeared before full charge");
        if (k < 100) {
            p.advance(now + interval - 1);
            close(p.charge(), static_cast<double>(k), "early increment before cadence boundary");
        }
    }
    check(p.time() == 2475 * ms && p.image_frame() == 7, "nominal full time");
    check(p.full_pull_time() == 2475 * ms, "XP integer full-pull duration");
    p.advance((std::numeric_limits<Tick>::max)());
    close(p.charge(), 100, "large gap did not saturate");
    check(p.held() && p.image_frame() == 7, "large gap lost held art");
    p.reset(); p.set_pressed(true, 0);
    close(p.charge(), 1, "XP immediate integer increment");
    p.advance(100 * ms);close(p.charge(), 5, "XP 100 ms charge");
    p.advance(500 * ms);close(p.charge(), 21, "XP 500 ms charge");
    p.advance(1000 * ms);close(p.charge(), 41, "XP one-second charge");
    // The supplied reconstruction used fractional 1.5625 increments and would
    // yield 64.0625 here. Exact-target XP truncates that increment to 1.
    check(p.charge()!=64.0625f, "reconstruction fractional quantum returned");
    p.set_pressed(true, 1000 * ms);
    close(p.charge(), 41, "repeat press restarted charge");
}

void clock_partition_and_configured_images() {
    Plunger whole(PlungerConfig(8)), divided(PlungerConfig(8));
    whole.set_pressed(true, 123); divided.set_pressed(true, 123);
    for (Tick t = 123; t < 2475 * ms + 123; t += 7000003) divided.advance(t);
    divided.advance(2475 * ms + 123); whole.advance(2475 * ms + 123);
    check(whole.charge() == divided.charge() && whole.pull() == divided.pull() &&
        whole.image_frame() == divided.image_frame(), "charge depends on block subdivision");
    Plunger other(PlungerConfig(3, 90, 10 * ms, 7 * ms, 2));
    other.set_pressed(true, 5);
    close(other.charge(), 15, "configured immediate charge");
    other.advance(20 * ms + 5);
    close(other.charge(), 45, "configured cadence and subdivisions");
    check(other.image_frame() == 1, "configured art count");
    other.advance(50 * ms + 5);
    check(other.image_frame() == 2 && other.pull() == 1, "configured full art");
    check(other.set_pressed(false, 50 * ms + 5), "configured release missing");
    check(other.window_remaining(50 * ms + 5) == 7 * ms, "configured window length");
    check(other.full_pull_time() == 50 * ms, "configured full-pull duration");

    // 200/(8*8) truncates to 3; the last quantum must clamp 201 to 200.
    // Redistributing 200 across 67 equal fractional steps would be incorrect.
    Plunger uneven(PlungerConfig(8, 200));
    check(uneven.full_pull_time() == 1650 * ms, "nondivisible maximum full duration");
    uneven.set_pressed(true, 0);
    close(uneven.charge(), 3, "truncated nondivisible integer quantum");
    uneven.advance(1625 * ms);
    close(uneven.charge(), 198, "penultimate nondivisible charge");
    check(uneven.image_frame() == 6 && uneven.pull() < 1, "nondivisible art reached full early");
    uneven.advance(1650 * ms);
    close(uneven.charge(), 200, "final integer quantum did not clamp");
    check(uneven.image_frame() == 7 && uneven.pull() == 1, "clamped charge art/fraction");
}

void collision_window_and_repress() {
    Plunger p(PlungerConfig(8));
    p.set_pressed(true, 0);
    check(p.set_pressed(false, 100 * ms), "physical release did not open window");
    check(!p.held() && p.image_frame() == 0, "release did not restore rest art immediately");
    close(p.charge(), 5, "release did not retain charge");
    check(p.window_remaining(100 * ms) == interval, "release begin is not inclusive");
    close(p.boost(100 * ms, 0), 5, "minimum randomized boost");
    close(p.boost(100 * ms, 1), 5.5, "maximum randomized boost");
    close(p.boost(100 * ms, 0), 5, "boost query unexpectedly consumed contact");
    check(!p.set_pressed(false, 110 * ms), "repeated release reopened window");
    check(p.window_remaining(110 * ms) == 15 * ms, "repeated release extended window");
    check(p.window_remaining(125 * ms - 1) == 1, "window ended early");
    close(p.boost(125 * ms - 1, .5f), 5.25, "last instant of boost window");
    check(p.boost(125 * ms, 1) == 0, "window end is not exclusive");
    check(p.charge() == 0 && p.pull() == 0 && p.window_remaining(125 * ms) == 0,
        "expired release retained charge");

    p.reset(); p.set_pressed(true, 0); p.set_pressed(false, 100 * ms);
    p.set_pressed(true, 110 * ms);
    close(p.charge(), 1, "repress reused released charge");
    check(p.held() && p.boost(110 * ms, 1) == 0, "repress left old release window active");
    p.advance(125 * ms);
    close(p.charge(), 1, "old release deadline erased new press");
    check(p.set_pressed(false, 125 * ms), "fresh repress cycle could not release");
    check(p.window_remaining(125 * ms) == interval, "fresh cycle window missing");
}

void forced_release_and_reset() {
    const TransitionCause causes[] = {TransitionCause::FocusLost, TransitionCause::Disconnected,
        TransitionCause::Paused, TransitionCause::ExplicitRelease};
    for (std::size_t i = 0; i < sizeof(causes) / sizeof(causes[0]); ++i) {
        Plunger p(PlungerConfig(8)); p.set_pressed(true, 0);
        check(!p.set_pressed(false, 1000 * ms, causes[i]), "nonphysical release armed boost");
        check(!p.held() && p.charge() == 0 && p.boost(1000 * ms, 1) == 0,
            "nonphysical release retained charge");
        check(!p.set_pressed(true, 1000 * ms, causes[i]) && !p.held(),
            "nonphysical press armed charge");
        p.set_pressed(true, 1000 * ms); p.set_pressed(false, 1100 * ms);
        p.cancel(1100 * ms);
        check(p.boost(1100 * ms, 1) == 0, "cancel retained open window");
        p.set_pressed(true, 1200 * ms); p.cancel(1300 * ms);
        check(!p.set_pressed(false, 1400 * ms), "cancel replayed release");
        p.reset(0);
        check(p.time() == 0 && !p.held() && p.pull() == 0, "fresh epoch reset");
    }
}

void validation_and_overflow() {
    const Tick smallest = (std::numeric_limits<Tick>::min)();
    const Tick largest = (std::numeric_limits<Tick>::max)();
    const float infinity = (std::numeric_limits<float>::infinity)();
    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    throws<std::invalid_argument>([] { Plunger p(PlungerConfig(0)); }, "zero images accepted");
    throws<std::invalid_argument>([] { Plunger p(PlungerConfig(8, 100, interval, interval, 0)); }, "zero subdivisions accepted");
    throws<std::invalid_argument>([] { Plunger p(PlungerConfig(8, 0)); }, "zero charge accepted");
    throws<std::invalid_argument>([] { Plunger p(PlungerConfig(8, 100.5f)); }, "fractional maximum accepted");
    throws<std::invalid_argument>([] { Plunger p(PlungerConfig(13)); }, "zero truncated quantum accepted");
    throws<std::invalid_argument>([] { Plunger p(PlungerConfig(8, 63)); }, "below-divisor maximum accepted");
    throws<std::invalid_argument>([] { Plunger p(PlungerConfig(8, 2147483648.0f)); }, "maximum above signed32 accepted");
    throws<std::invalid_argument>([&] { Plunger p(PlungerConfig(8, infinity)); }, "infinite charge accepted");
    throws<std::invalid_argument>([&] { Plunger p(PlungerConfig(8, nan)); }, "NaN charge accepted");
    throws<std::invalid_argument>([] { Plunger p(PlungerConfig(8, 100, 0)); }, "zero cadence accepted");
    throws<std::invalid_argument>([&] { Plunger p(PlungerConfig(8, 100, smallest)); }, "high-bit cadence accepted");
    throws<std::invalid_argument>([&] { Plunger p(PlungerConfig(8, 100, interval, smallest)); }, "high-bit window accepted");
    throws<std::invalid_argument>([&] { Plunger p(PlungerConfig(8), smallest); }, "high-bit initial time accepted");
    const std::uint32_t u32max = (std::numeric_limits<std::uint32_t>::max)();
    throws<std::overflow_error>([&] { Plunger p(PlungerConfig(u32max, 100, 1, 1, 2)); }, "quantum divisor overflow accepted");
    throws<std::overflow_error>([&] { Plunger p(PlungerConfig(8, 100, largest)); }, "full-pull duration overflow accepted");

    Plunger p(PlungerConfig(8), 100);
    p.set_pressed(true, 100);
    throws<std::invalid_argument>([&] { p.advance(99); }, "backward advance accepted");
    throws<std::invalid_argument>([&] { p.cancel(smallest); }, "negative cancel accepted");
    throws<std::invalid_argument>([&] { p.reset(smallest); }, "negative reset accepted");
    throws<std::invalid_argument>([&] { p.boost(1000, nan); }, "NaN random accepted");
    throws<std::invalid_argument>([&] { p.boost(1000, -1); }, "negative random accepted");
    throws<std::invalid_argument>([&] { p.boost(1000, 1.01f); }, "random above one accepted");
    check(p.time() == 100 && p.held() && p.charge() == 1.0f, "invalid call changed state");

    Plunger overflow(PlungerConfig(8), largest);
    overflow.set_pressed(true, largest);
    throws<std::overflow_error>([&] { overflow.set_pressed(false, largest); }, "release deadline overflow accepted");
    check(overflow.held() && overflow.time() == largest && overflow.charge() == 1.0f,
        "overflow release partially changed state");
    Plunger exact(PlungerConfig(8), largest - interval);
    exact.set_pressed(true, largest - interval); exact.set_pressed(false, largest - interval);
    check(exact.window_remaining(largest - 1) == 1 && exact.window_remaining(largest) == 0,
        "representable maximum deadline failed");

    Plunger immediate(PlungerConfig(1, 100, largest, interval, 1));
    check(immediate.full_pull_time() == 0, "immediate full charge should not overflow duration");
    immediate.set_pressed(true, 0);
    check(immediate.charge() == 100 && immediate.image_frame() == 0,
        "one-step full charge or one-image art");
    immediate.advance(largest);
    check(immediate.charge() == 100, "single-quantum saturation after huge gap");
}
}

int main() {
    try {
        quantization_and_art();
        clock_partition_and_configured_images();
        collision_window_and_repress();
        forced_release_and_reset();
        validation_and_overflow();
        std::cout << "reference_plunger_tests PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "reference_plunger_tests FAIL: " << error.what() << '\n';
        return 1;
    }
}
