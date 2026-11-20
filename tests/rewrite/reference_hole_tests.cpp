#include "reference_hole.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using pinball::reference::HoleDrop;
using pinball::reference::HoleDropConfig;
using pinball::runtime::Tick;
const Tick hold = 500000000;
const Tick cadence = pinball::runtime::ticks_per_second / 120;

void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}
void near(float actual, double expected, const std::string& message)
{
    require(std::fabs(static_cast<double>(actual) - expected) <= 0.000002, message);
}
template<class Error>
void rejects(const std::function<void()>& operation, const std::string& fragment)
{
    try { operation(); }
    catch (const Error& error) {
        require(std::string(error.what()).find(fragment) != std::string::npos,
            "wrong hole rejection: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("expected hole rejection containing " + fragment);
}
void same_state(const HoleDrop& a, const HoleDrop& b)
{
    require(a.captured() == b.captured() && a.falling() == b.falling() &&
        a.landed() == b.landed() && a.height() == b.height() &&
        a.vertical_velocity() == b.vertical_velocity() && a.time() == b.time() &&
        a.until_boundary() == b.until_boundary(), "advance chunking must be state-identical");
}

void inactive_wait_and_legacy_ticks()
{
    HoleDrop drop(HoleDropConfig(hold,5));
    require(!drop.captured() && !drop.falling() && !drop.landed() &&
        drop.height() == 0 && drop.vertical_velocity() == 0 && drop.until_boundary() == 0,
        "new controller must be inactive with finite neutral outputs");
    require(!drop.advance(10) && drop.time() == 10, "inactive advance must update clock without landing");
    const Tick start = 10;
    const float z = 1.360704f, target = 0.325771987f;
    drop.capture(z,target,start);
    require(drop.captured() && !drop.falling() && !drop.landed() &&
        drop.until_boundary() == hold, "capture must enter a full wait");
    require(!drop.advance(start + hold - 1) && drop.until_boundary() == 1,
        "wait is exclusive of its end and must retain one remaining nanosecond");
    near(drop.height(),z,"waiting must preserve actual captured height");
    require(!drop.advance(start + hold) && drop.falling() && drop.captured() &&
        drop.until_boundary() == cadence, "wait end starts falling but applies no fall step yet");
    require(!drop.advance(start + hold + cadence - 1), "fall does not advance before its cadence boundary");
    near(drop.height(),z,"partial fall interval must not move Z");
    const double per_tick = 5.0 * static_cast<double>(cadence) / pinball::runtime::ticks_per_second;
    require(!drop.advance(start + hold + cadence), "first fall step must not land this profile");
    near(drop.vertical_velocity(),-per_tick,"first vertical accumulator step must be acceleration*dt");
    near(drop.height(),static_cast<double>(z) - per_tick,
        "legacy Z uses the vertical accumulator directly, without a second dt");
    require(!drop.advance(start + hold + cadence * 2), "second fall step should stay above target");
    near(drop.vertical_velocity(),-per_tick * 2,"second tick must accumulate vertical velocity");
    near(drop.height(),static_cast<double>(z) - per_tick * 3,"descent must use the triangular sum");
    require(!drop.advance(start + hold + cadence * 6), "sixth tick is still above actual hole target");
    near(drop.height(),static_cast<double>(z) - per_tick * 21,"sixth-tick height must preserve legacy recurrence");
    require(drop.advance(start + hold + cadence * 7), "seventh tick must produce exactly one landing event");
    require(drop.landed() && !drop.captured() && !drop.falling() && drop.height() == target &&
        drop.vertical_velocity() == 0 && drop.until_boundary() == 0,
        "landing must clamp to literal DAT target and reset vertical accumulator");
    require(!drop.advance(start + hold + cadence * 7) && !drop.advance(start + hold + cadence * 20),
        "a sticky landed state must not repeat the landing event");
    require(drop.height() == target && drop.landed(), "inactive advance after landing must preserve landing outputs");
}

void chunking_and_boundary_durations()
{
    const HoleDropConfig config(hold,5,10000000);
    HoleDrop one(config),chunks(config),ticks(config);
    one.capture(10,0,100); chunks.capture(10,0,100); ticks.capture(10,0,100);
    const Tick samples[] = {100,1234,hold-1,hold+100,hold+101,hold+5000000,
        hold+10000100,hold+25000100,hold+150000100};
    for (std::size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i) {
        require(!chunks.advance(samples[i]), "chunk samples must precede landing");
    }
    require(!one.advance(hold+150000100), "one-shot sample must precede landing");
    for (Tick now = 100; now < hold+150000100; now += 10000000) ticks.advance(now);
    ticks.advance(hold+150000100);
    same_state(one,chunks); same_state(one,ticks);
    near(one.height(),4.0,"fifteen .05 legacy ticks descend by6");
    require(one.until_boundary() == 10000000,"exact fall tick must point to the next full cadence");
    require(!one.advance(hold+153000100) && one.until_boundary() == 7000000,
        "until_boundary must be a relative duration from a fractional tick");
    HoleDrop direct(config),partial(config);
    direct.capture(10,0,0); partial.capture(10,0,0);
    bool event = false;
    for (Tick now = 0; now <= 2000000000LL; now += 1000000) event = partial.advance(now) || event;
    require(event && direct.advance(2000000000LL), "both chunking policies must land once");
    same_state(direct,partial);
}

void below_target_zero_gravity_pause_and_reset()
{
    HoleDrop drop(HoleDropConfig(hold,5));
    drop.capture(0.2f,0.3f,0);
    for (int i=0;i<100;++i) require(!drop.advance(1234),"frozen simulation time must not consume wait");
    require(drop.height() == 0.2f && drop.until_boundary() == hold-1234,
        "pause by clock freeze must preserve height and deadline duration");
    require(!drop.advance(hold) && drop.height() == 0.2f,"below-target capture must still finish its wait first");
    require(drop.advance(hold+cadence) && drop.height() == 0.3f,"first fall tick clamps below-target capture");
    drop.capture(0.3f,0.3f,hold+cadence);
    require(!drop.landed() && drop.captured(),"new capture must clear old landing latch");
    require(!drop.advance(hold*2+cadence),"equal-height capture still waits before landing");
    require(drop.advance(hold*2+cadence*2),"equal-height capture lands at its first fall tick");
    drop.reset(0);
    require(drop.time() == 0 && !drop.captured() && !drop.landed() && drop.height() == 0,
        "explicit reset must clear state and permit a fresh earlier epoch");
    HoleDrop zero(HoleDropConfig(1,0,1));
    zero.capture(1,0,0);
    require(!zero.advance((std::numeric_limits<Tick>::max)()) && zero.falling() && zero.captured() &&
        zero.height() == 1 && zero.vertical_velocity() == 0 && zero.until_boundary() == 1,
        "zero gravity and huge gaps must be bounded without inventing a drop");
}

void huge_clocks_and_extents()
{
    const Tick boundaries[] = {2147483648LL,4294967296LL};
    for (std::size_t i=0;i<2;++i) {
        const Tick start = boundaries[i]-hold-5;
        HoleDrop absolute(HoleDropConfig(hold,5));
        absolute.capture(1,0,start);
        require(!absolute.advance(start+hold),"large absolute clock must finish wait without a tick");
        require(absolute.advance(start+hold+cadence*8),"large absolute clock must land normally");
        HoleDrop gap(HoleDropConfig(hold,5));
        gap.capture(1,0,0);
        require(gap.advance(boundaries[i]),"large elapsed age must dispatch landing exactly once");
        require(!gap.advance(boundaries[i]),"repeating large time must not repeat an event");
    }
    const Tick maximum = (std::numeric_limits<Tick>::max)();
    HoleDrop end(HoleDropConfig(20,5,1),maximum-10);
    end.capture(1,0,maximum-10);
    require(!end.advance(maximum) && !end.falling() && end.until_boundary() == 10,
        "a hold crossing representable absolute time must not wrap or form a deadline");
    HoleDrop late(HoleDropConfig(1,5,1));
    late.capture(1,0,0);
    require(late.advance(maximum) && late.height() == 0 && late.landed(),
        "billions of nominal fall ticks must fast-forward without loops or integer-sum overflow");
    HoleDrop huge(HoleDropConfig(1,5,1));
    const float extent = (std::numeric_limits<float>::max)();
    huge.capture(extent,-extent,0);
    require(!huge.advance(maximum) && std::isfinite(huge.height()) &&
        std::isfinite(huge.vertical_velocity()),"huge finite Z span must remain bounded on a huge clock jump");
}

void validation_and_transactional_overflow()
{
    const double infinity = (std::numeric_limits<double>::infinity)();
    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    rejects<std::invalid_argument>([]{HoleDropConfig invalid(0,5);},"positive");
    rejects<std::invalid_argument>([]{HoleDropConfig invalid(-1,5);},"nonnegative");
    rejects<std::invalid_argument>([]{HoleDropConfig invalid(1,5,0);},"positive");
    rejects<std::invalid_argument>([]{HoleDropConfig invalid(1,5,-1);},"nonnegative");
    rejects<std::invalid_argument>([]{HoleDropConfig invalid(1,-1);},"nonnegative");
    rejects<std::invalid_argument>([&]{HoleDropConfig invalid(1,nan);},"finite");
    rejects<std::invalid_argument>([&]{HoleDropConfig invalid(1,static_cast<float>(infinity));},"finite");
    HoleDropConfig mutated(hold,5); mutated.cadence=0;
    rejects<std::invalid_argument>([&]{HoleDrop invalid(mutated);},"positive");
    rejects<std::invalid_argument>([]{HoleDrop invalid(HoleDropConfig(hold,5),-1);},"nonnegative");
    HoleDrop drop(HoleDropConfig(hold,5),10);
    rejects<std::invalid_argument>([&]{drop.capture(nan,0,11);},"finite");
    rejects<std::invalid_argument>([&]{drop.capture(1,static_cast<float>(infinity),11);},"finite");
    require(!drop.captured() && drop.time()==10,"invalid capture must preserve inactive state/clock");
    drop.capture(1,0,11);
    rejects<std::logic_error>([&]{drop.capture(2,0,12);},"already");
    rejects<std::invalid_argument>([&]{drop.advance(10);},"backward");
    rejects<std::invalid_argument>([&]{drop.capture(2,0,10);},"backward");
    rejects<std::invalid_argument>([&]{drop.advance(-1);},"nonnegative");
    rejects<std::invalid_argument>([&]{drop.reset(-1);},"nonnegative");
    require(drop.time()==11 && drop.height()==1 && drop.until_boundary()==hold,
        "invalid operations must preserve active capture and pending boundary");
    // A finite Z span can need an unrepresentable live vertical accumulator.
    // Reject before changing the clock/phase/height; a later reset still works.
    const float extent = (std::numeric_limits<float>::max)();
    HoleDrop overflow(HoleDropConfig(1,extent,1500000000));
    overflow.capture(extent,-extent,0);
    overflow.advance(1);
    rejects<std::overflow_error>([&]{overflow.advance(1500000001);},"float range");
    require(overflow.time()==1 && overflow.height()==extent && overflow.vertical_velocity()==0 &&
        overflow.captured() && overflow.falling() && !overflow.landed() &&
        overflow.until_boundary()==1500000000,"overflow must be transactional");
    overflow.reset(); require(!overflow.captured(),"reset must recover from rejected arithmetic");
}
} // namespace

int main()
{
    try {
        inactive_wait_and_legacy_ticks();
        chunking_and_boundary_durations();
        below_target_zero_gravity_pause_and_reset();
        huge_clocks_and_extents();
        validation_and_transactional_overflow();
        std::cout<<"reference hole tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"reference hole test failure: "<<error.what()<<'\n';
        return 1;
    }
}
