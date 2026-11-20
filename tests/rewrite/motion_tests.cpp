#include "motion.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace pinball::motion;
using pinball::runtime::TransitionCause;
const Tick second = pinball::runtime::ticks_per_second;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, const char* message, double tolerance = 0.00001) {
    check(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance, message);
}
template<class Exception, class Operation>
void throws(Operation operation, const char* message) {
    try { operation(); } catch (const Exception&) { return; }
    throw std::runtime_error(message);
}
PlungerConfig plunger_config() {
    return PlungerConfig(second, second / 2, Vec2(0, -2), 2, 10, 0.125f);
}
void owner_activation_deactivates_all_shapes() {
    // Use the production separation margin: radius.1 is not exactly
    // representable at x3.9, so zero-slop wall placement may round inward.
    BallStepper stepper(MotionConfig(Vec2(),second,16,.0001f));
    std::vector<StaticCollider> bodies;
    bodies.push_back(StaticCollider::segment(Vec2(0,-2),Vec2(0,2),1));
    bodies.push_back(StaticCollider::segment(Vec2(2,-2),Vec2(2,2),1));
    bodies.push_back(StaticCollider::segment(Vec2(4,-2),Vec2(4,2),1));
    for(unsigned i=0;i<2;++i) {
        bodies[i].reference_response=true;bodies[i].material=pinball::response::Material(1,1,3,5);
        bodies[i].deactivate_on_activation=true;bodies[i].response_owner=42;
    }
    BallState ball(Vec2(.25f,0),Vec2(-10,0),.1f);
    const MotionStep result=stepper.step(ball,second*3/10,bodies);
    check(result.contacts.size()==2 && result.contacts[0].collider_index==0 && result.contacts[0].activated &&
          result.contacts[1].collider_index==2,"same-owner second shape remained solid or unrelated wall disabled");
    check(bodies[0].enabled && bodies[1].enabled,"owner deactivation mutated caller colliders");
    check(!result.contact_budget_exhausted,"owner deactivation exhausted collision budget");
    ball=BallState(Vec2(1.75f,0),Vec2(10,0),.1f);
    const MotionStep fresh=stepper.step(ball,second/20,bodies);
    check(fresh.contacts.size()==1 && fresh.contacts[0].collider_index==1 && fresh.contacts[0].activated,
          "local owner deactivation leaked into later solve");
    bodies[0].response_owner=(std::numeric_limits<std::size_t>::max)();
    bodies[1].response_owner=(std::numeric_limits<std::size_t>::max)();
    ball=BallState(Vec2(.25f,0),Vec2(-10,0),.1f);
    const MotionStep separate=stepper.step(ball,second/5,bodies);
    check(separate.contacts.size()>=2 && separate.contacts[1].collider_index==1,
          "default no-owner sentinel grouped independent shapes");
    bodies[0].reference_response=false;
    throws<std::invalid_argument>([&]{stepper.step(ball,0,bodies);},"generic physical response accepted activation drop policy");
}
void directed_capture_release_overlap() {
    BallStepper stepper(MotionConfig(Vec2(),second,8,.0001f));
    std::vector<StaticCollider> gate(1,StaticCollider::segment(Vec2(-2,0),Vec2(2,0),.6f,false));
    gate[0].captures_ball=true;
    BallState leaving(Vec2(0,.24f),Vec2(0,35),.3f);
    MotionStep result=stepper.step(leaving,second/10,gate);
    check(!result.captured && result.contacts.empty(),"outgoing overlap was swallowed by a directed capture");
    close(leaving.position.y,3.74,"outgoing overlap failed to escape capture strip");
    BallState tangent(Vec2(0,.24f),Vec2(1,0),.3f);
    result=stepper.step(tangent,second/10,gate);
    check(!result.captured&&result.contacts.empty(),"tangent overlap entered a one-way capture");
    BallState still(Vec2(0,.24f),Vec2(),.3f);
    check(!stepper.step(still,second/10,gate).captured,"stationary overlap entered directed capture");
    BallState entering(Vec2(0,.24f),Vec2(0,-2),.3f);
    check(stepper.step(entering,second/10,gate).captured,"incoming overlap lost terminal capture");
    BallState returned(Vec2(0,1),Vec2(0,-10),.3f);
    result=stepper.step(returned,second/10,gate);
    check(result.captured&&result.contacts.size()==1,"genuine inward return failed to recapture");
    BallState back(Vec2(0,-1),Vec2(0,10),.3f);
    check(!stepper.step(back,second/10,gate).captured,"backside pass entered directed capture");
    gate[0].two_sided=true;BallState both(Vec2(0,.24f),Vec2(0,35),.3f);
    check(stepper.step(both,second/10,gate).captured,"two-sided terminal overlap semantics changed");
    gate[0].captures_ball=false;gate[0].two_sided=false;
    BallState solid(Vec2(0,.24f),Vec2(0,35),.3f);
    result=stepper.step(solid,second/10,gate);
    check(!result.contacts.empty()&&result.contacts[0].hit.initial_overlap,"solid-wall penetration handling changed");
    std::vector<StaticCollider> point(1,StaticCollider::segment(Vec2(),Vec2(),0,false));
    point[0].captures_ball=true;BallState point_overlap(Vec2(.1f,0),Vec2(1,0),.3f);
    check(stepper.step(point_overlap,1,point).captured,"degenerate segment point semantics changed");
    std::vector<StaticCollider> sensor(1,StaticCollider::center_capture(Vec2(),.08f));
    BallState center(Vec2(),Vec2(),.3f);
    check(stepper.step(center,1,sensor).captured,"center sensor containment semantics changed");
}
void nonphysical_event_preserves_surface() {
    BallStepper stepper(MotionConfig(Vec2(),second,8,0));
    std::vector<StaticCollider> events(1,StaticCollider::event_segment(Vec2(1,-2),Vec2(1,2),4));
    BallState ball(Vec2(0,0),Vec2(4,0),.5f);ball.collision_mask=4;
    ball.plane.active=true;ball.plane.gradient=Vec2(.5f,.2f);ball.plane.offset=.7f;ball.plane.id=19;
    MotionStep result=stepper.step(ball,second,events);
    check(result.contacts.size()==1 && result.contacts[0].triggered && !result.contacts[0].transitioned &&
          !result.contacts[0].boost_applied && !result.contacts[0].activated && !result.captured,
          "plain center event acted as physical response or surface transition");
    close(result.contacts[0].seconds_from_start,.25,"event inflated the moving ball radius");
    close(ball.position.x,4,"event stopped forward travel");close(ball.velocity.x,4,"event changed velocity");
    check(ball.collision_mask==4 && ball.plane.active && ball.plane.id==19,"event cleared selected layer/plane");
    close(ball.height,3.2,"event lost constrained height");
    ball=BallState(Vec2(2,0),Vec2(-4,0),.5f);ball.collision_mask=4;
    result=stepper.step(ball,second,events);check(result.contacts.empty(),"backface event fired");
    ball=BallState(Vec2(0,2.1f),Vec2(4,0),.5f);ball.collision_mask=4;
    result=stepper.step(ball,second,events);check(result.contacts.empty(),"event inflated finite endpoints");
    ball=BallState(Vec2(0,0),Vec2(4,0),.5f);
    result=stepper.step(ball,second,events);check(result.contacts.empty(),"event ignored listen mask");
    ball=BallState(Vec2(1,0),Vec2(4,0),.5f);ball.collision_mask=4;
    result=stepper.step(ball,second,events);
    check(result.contacts.size()==1 && result.contacts[0].seconds_from_start==0 && !result.contact_budget_exhausted,
          "zero-time plain event repeated or missed");
    close(ball.position.x,5,"zero-time event consumed remaining flight");
}
void component_activation_and_step_latch() {
    BallStepper stepper(MotionConfig(Vec2(),second,64,0));
    std::vector<StaticCollider> walls;
    walls.push_back(StaticCollider::segment(Vec2(0,-2),Vec2(0,2),1));
    walls.push_back(StaticCollider::segment(Vec2(2,-2),Vec2(2,2),1));
    walls[0].reference_response=true;
    walls[0].material=pinball::response::Material(1,1,2,5);
    BallState ball(Vec2(.25f,0),Vec2(-10,0),.1f);
    MotionStep result=stepper.step(ball,second/2,walls);
    unsigned activations=0,boosts=0;
    for(std::size_t i=0;i<result.contacts.size();++i)if(result.contacts[i].collider_index==0) {
        activations+=result.contacts[i].activated;boosts+=result.contacts[i].boost_applied;
    }
    check(activations>1 && boosts>1,"default collider unexpectedly latched repeated responses");
    walls[0].latch_response=true;ball=BallState(Vec2(.25f,0),Vec2(-10,0),.1f);
    result=stepper.step(ball,second/2,walls);activations=boosts=0;
    for(std::size_t i=0;i<result.contacts.size();++i)if(result.contacts[i].collider_index==0) {
        activations+=result.contacts[i].activated;boosts+=result.contacts[i].boost_applied;
    }
    check(result.contacts.size()>3 && activations==1 && boosts==1 && !result.contact_budget_exhausted,
          "latched collider activated again inside one CCD step");
    close(result.contacts[0].incident_normal_speed,10,"pre-response impact speed was lost");
    close(walls[0].material.threshold,2,"per-step latch mutated supplied collider material");
    ball=BallState(Vec2(.25f,0),Vec2(-10,0),.1f);
    result=stepper.step(ball,second/20,walls);
    check(result.contacts.size()==1 && result.contacts[0].activated,"per-step latch leaked into next step");
    walls[0].material.threshold=10;ball=BallState(Vec2(.25f,0),Vec2(-10,0),.1f);
    result=stepper.step(ball,second/20,walls);
    check(result.contacts.size()==1 && result.contacts[0].boost_applied && !result.contacts[0].activated,
          "inclusive physics threshold must not become strict animation activation");
    walls[0].material=pinball::response::Material(1,1,2,0);ball=BallState(Vec2(.25f,0),Vec2(-10,0),.1f);
    result=stepper.step(ball,second/20,walls);
    check(result.contacts.size()==1 && !result.contacts[0].boost_applied && result.contacts[0].activated,
          "strict activation must remain observable without a nonzero kick");
    walls[0].reference_response=false;
    throws<std::invalid_argument>([&]{stepper.step(ball,0,walls);},"latch on generic response accepted");
}
void plunger_charge_and_one_shot() {
    TimedPlunger plunger(plunger_config());
    check(plunger.phase() == PlungerPhase::Idle && !plunger.held(), "initial plunger state");
    check(!plunger.set_pressed(true, 0, true).fired, "press launched");
    plunger.update(second / 4);
    close(plunger.pull(), 0.25, "quarter charge");
    check(!plunger.set_pressed(true, second / 2, true).fired, "repeated press launched");
    close(plunger.pull(), 0.5, "repeat restarted pull");
    const LaunchEvent partial = plunger.set_pressed(false, second / 2, true);
    check(partial.fired && !plunger.held(), "partial release did not fire");
    close(partial.pull, 0.5, "launch charge differs from displayed charge");
    close(partial.delta_velocity.x, 0, "launch x direction");
    close(partial.delta_velocity.y, -6, "explicit launch speed mapping");
    check(!plunger.set_pressed(false, second / 2, true).fired, "release fired twice");
    check(plunger.phase() == PlungerPhase::Returning, "release did not begin return");
    plunger.update(second * 5 / 8);
    close(plunger.pull(), 0.25, "partial return duration");
    plunger.set_pressed(true, second * 5 / 8, true);
    close(plunger.pull(), 0.25, "repress jumped pose");
    plunger.update(second * 7 / 8);
    close(plunger.pull(), 0.5, "repress charge did not continue");
    plunger.update(second * 4);
    close(plunger.pull(), 1, "held pull did not saturate");
    const LaunchEvent full = plunger.set_pressed(false, second * 4, true);
    check(full.fired, "full release missing");
    close(full.delta_velocity.y, -10, "full explicit impulse");
    plunger.update(second * 4 + second / 2);
    close(plunger.pull(), 0, "full return did not settle");
    check(plunger.phase() == PlungerPhase::Idle, "settled phase not idle");
    check(!plunger.set_pressed(false, second * 5, true).fired, "settle emitted another impulse");

    TimedPlunger direct(plunger_config()), divided(plunger_config());
    direct.set_pressed(true, 0, true); divided.set_pressed(true, 0, true);
    for (int i = 1; i <= 100; ++i) divided.update(second * i / 100);
    direct.update(second);
    close(direct.pull(), divided.pull(), "charge depends on update subdivision", 0);
}
void plunger_tap_seat_and_cancellation() {
    TimedPlunger plunger(plunger_config());
    plunger.set_pressed(true, 0, true);
    check(!plunger.set_pressed(false, second / 16, true).fired, "below-threshold tap fired");
    plunger.cancel(second / 8);
    plunger.set_pressed(true, second / 8, false);
    check(!plunger.set_pressed(false, second * 2, false).fired, "empty seat launched");
    check(!plunger.set_pressed(false, second * 2, true).fired, "seated-after-release replayed launch");
    const TransitionCause causes[] = { TransitionCause::FocusLost, TransitionCause::Disconnected,
        TransitionCause::Paused, TransitionCause::ExplicitRelease };
    for (std::size_t i = 0; i < sizeof(causes) / sizeof(causes[0]); ++i) {
        plunger.reset();
        plunger.set_pressed(true, 0, true);
        check(!plunger.set_pressed(false, second, true, causes[i]).fired,
              "non-gameplay release launched");
        check(!plunger.held() && plunger.phase() == PlungerPhase::Idle && plunger.pull() == 0,
              "forced release retained charge");
        check(!plunger.set_pressed(false, second * 2, true).fired, "cancel replayed release");
        check(!plunger.set_pressed(true, second * 2, true, causes[i]).fired && !plunger.held(),
              "nonphysical press armed plunger");
    }
    plunger.reset(); plunger.set_pressed(true, 0, true); plunger.cancel(second);
    check(!plunger.set_pressed(false, second * 2, true).fired, "explicit cancellation launched");
    plunger.set_pressed(true, second * 2, true);
    check(plunger.set_pressed(false, second * 3, true).fired, "fresh post-cancel cycle could not launch");
    TimedPlunger zero(PlungerConfig(second, second, Vec2(1, 0), 0, 0, 0));
    zero.set_pressed(true, 0, true);
    check(!zero.set_pressed(false, second, true).fired, "zero-speed configuration emitted launch");
}
void plunger_time_boundaries() {
    const Tick maximum = std::numeric_limits<Tick>::max();
    const Tick high_word_only = Tick(4294967296LL);
    TimedPlunger wide(PlungerConfig(high_word_only, high_word_only, Vec2(1, 0), 0, 1, 0));
    wide.set_pressed(true, 0, true);
    wide.update(high_word_only / 2);
    close(wide.pull(), 0.5, "positive zero-low-word duration treated as zero" );
    check(wide.set_pressed(false, high_word_only, true).fired, "zero-low-word release lost" );
    TimedPlunger plunger(PlungerConfig(maximum, maximum, Vec2(1, 0), 0, 1, 0));
    plunger.set_pressed(true, 0, true);
    plunger.update(Tick(2147483648LL));
    check(plunger.pull() > 0, "positive low-32 sign boundary rejected");
    plunger.update(Tick(6442450944LL));
    plunger.update(maximum);
    close(plunger.pull(), 1, "maximum Tick hold overflowed");
    check(plunger.set_pressed(false, maximum, true).fired, "maximum Tick release lost");
    plunger.update(maximum);
    plunger.reset(maximum - 10);
    plunger.set_pressed(true, maximum - 10, true); plunger.update(maximum);
    check(plunger.pull() > 0, "near-limit delta overflowed");
    throws<std::invalid_argument>([&] { plunger.update(maximum - 1); }, "backward time accepted");
    throws<std::invalid_argument>([&] { plunger.update(-1); }, "negative time accepted");
    throws<std::invalid_argument>([&] { plunger.reset(Tick(-4294967296LL)); },
                                  "negative high word with zero low word accepted");
    check(plunger.time() == maximum, "failed time validation changed clock");
    plunger.reset(0);
    check(plunger.time() == 0 && plunger.pull() == 0 && !plunger.held(), "fresh epoch reset failed");
}
void free_flight_and_subdivision() {
    const std::vector<StaticCollider> empty;
    BallStepper stepper(MotionConfig(Vec2(2, 6), second, 8, 0));
    BallState whole(Vec2(1, 2), Vec2(3, -4), 1), halves = whole, small = whole;
    const MotionStep result = stepper.step(whole, second, empty);
    close(whole.position.x, 5, "ballistic x position");
    close(whole.position.y, 1, "ballistic y position");
    close(whole.velocity.x, 5, "ballistic x velocity");
    close(whole.velocity.y, 2, "ballistic y velocity");
    close(result.simulated_seconds, 1, "simulated free-flight time");
    check(result.contacts.empty() && !result.contact_budget_exhausted && result.discarded_seconds == 0,
          "free flight discarded time or invented contacts");
    stepper.step(halves, second / 2, empty); stepper.step(halves, second / 2, empty);
    close(halves.position.x, whole.position.x, "x changed under dt subdivision");
    close(halves.position.y, whole.position.y, "y changed under dt subdivision");
    close(halves.velocity.y, whole.velocity.y, "velocity changed under dt subdivision");
    for (int i = 0; i < 1000; ++i) stepper.step(small, second / 1000, empty);
    close(small.position.x, whole.position.x, "small-step x drift", 0.0002);
    close(small.position.y, whole.position.y, "small-step y drift", 0.0002);
    close(small.velocity.y, whole.velocity.y, "small-step velocity drift", 0.0002);
    const BallState before = whole;
    const MotionStep zero = stepper.step(whole, 0, empty);
    check(whole.position.x == before.position.x && whole.velocity.y == before.velocity.y &&
          zero.simulated_seconds == 0 && zero.contacts.empty(), "zero step changed state");
    const Tick high_word_only = Tick(4294967296LL);
    BallStepper highword(MotionConfig(Vec2(), high_word_only, 1, 0));
    BallState moving(Vec2(), Vec2(1, 0), 1);
    const MotionStep high_step = highword.step(moving, high_word_only, empty);
    close(moving.position.x, 4.294967296, "positive zero-low-word step treated as zero" );
    check(high_step.simulated_seconds > 4, "zero-low-word time not accounted" );
    BallStepper wide(MotionConfig(Vec2(), std::numeric_limits<Tick>::max(), 1, 0));
    BallState stationary(Vec2(), Vec2(), 1);
    const MotionStep long_step = wide.step(stationary, std::numeric_limits<Tick>::max(), empty);
    close(long_step.simulated_seconds, double(std::numeric_limits<Tick>::max()) / second,
          "maximum Tick duration overflowed", 0);
}
void swept_contacts_and_order() {
    BallStepper stepper(MotionConfig(Vec2(), second, 8, 0.00001f));
    std::vector<StaticCollider> colliders;
    colliders.push_back(StaticCollider::disk(Vec2(), 1, 1));
    BallState ball(Vec2(-5, 0), Vec2(10, 0), 1);
    MotionStep result = stepper.step(ball, second, colliders);
    check(result.contacts.size() == 1 && !result.contact_budget_exhausted,
          "single disk contact not followed by free flight");
    close(result.contacts[0].seconds_from_start, 0.3, "disk impact time");
    close(ball.position.x, -9, "disk reflection remainder", 0.0001);
    close(ball.velocity.x, -10, "disk reflection velocity");
    colliders.clear();
    colliders.push_back(StaticCollider::segment(Vec2(0, -100), Vec2(0, 100), 1));
    ball = BallState(Vec2(-1000, 0), Vec2(200000, 0), 0.1f);
    result = stepper.step(ball, second / 100, colliders);
    check(result.contacts.size() == 1, "high-speed line tunneled");
    close(ball.velocity.x, -200000, "high-speed reflection");
    close(ball.position.x, -1000.2, "high-speed remainder", 0.002);
    colliders.clear();
    colliders.push_back(StaticCollider::disk(Vec2(), 0.1f, 1));
    ball = BallState(Vec2(-1000, 0), Vec2(200000, 0), 0.1f);
    result = stepper.step(ball, second / 100, colliders);
    check(result.contacts.size() == 1 && ball.position.x < -1000,
          "high-speed disk tunneled");
    colliders.clear();
    colliders.push_back(StaticCollider::segment(Vec2(3, -10), Vec2(3, 10), 1));
    colliders.push_back(StaticCollider::segment(Vec2(0, -10), Vec2(0, 10), 1));
    ball = BallState(Vec2(-5, 0), Vec2(10, 0), 1);
    result = stepper.step(ball, second, colliders);
    check(result.contacts.size() == 1 && result.contacts[0].collider_index == 1,
          "input order overrode earlier impact time");
    colliders[0] = StaticCollider::segment(Vec2(0, -10), Vec2(0, 10), 0);
    ball = BallState(Vec2(-5, 0), Vec2(10, 0), 1);
    result = stepper.step(ball, second, colliders);
    check(result.contacts.size() == 1 && result.contacts[0].collider_index == 0,
          "equal-time input ordering unstable");
    close(ball.velocity.x, 0, "equal-time first material did not win");
    colliders[0] = StaticCollider::segment(Vec2(0, -10), Vec2(0, 10), 1, false);
    colliders.resize(1);
    ball = BallState(Vec2(5, 0), Vec2(-10, 0), 1);
    result = stepper.step(ball, second, colliders);
    check(result.contacts.empty(), "one-sided back blocked ball");
    close(ball.position.x, -5, "one-sided pass-through position");
}
void contact_budgets_and_acceleration() {
    BallStepper limited(MotionConfig(Vec2(), second, 2, 0.0001f));
    std::vector<StaticCollider> walls;
    walls.push_back(StaticCollider::segment(Vec2(0, -10), Vec2(0, 10), 1));
    walls.push_back(StaticCollider::segment(Vec2(10, -10), Vec2(10, 10), 1));
    BallState ball(Vec2(5, 0), Vec2(100, 0), 1);
    const MotionStep exhausted = limited.step(ball, second, walls);
    check(exhausted.contact_budget_exhausted && exhausted.contacts.size() == 2,
          "contact cap not enforced");
    check(exhausted.discarded_seconds > 0.8 && ball.position.x >= 1 && ball.position.x <= 9,
          "exhausted drift crossed wall or hid dropped time");
    close(exhausted.simulated_seconds + exhausted.discarded_seconds, 1,
          "contact-budget time accounting");
    check(exhausted.contacts[0].collider_index == 1 && exhausted.contacts[1].collider_index == 0,
          "sequential wall order changed");
    walls.clear();
    walls.push_back(StaticCollider::disk(Vec2(), 1, 1));
    ball = BallState(Vec2(), Vec2(), 1);
    const MotionStep overlap = limited.step(ball, second, walls);
    check(overlap.contacts.size() == 1 && overlap.contacts[0].hit.initial_overlap,
          "initial penetration not corrected");
    check(ball.position.x > 2, "initial penetration persisted");
    walls.push_back(StaticCollider::disk(Vec2(3, 0), 1, 1));
    ball = BallState(Vec2(1.5f, 0), Vec2(), 1);
    const MotionStep trapped = limited.step(ball, second, walls);
    check(trapped.contact_budget_exhausted && trapped.contacts.size() == 2,
          "incompatible overlaps did not terminate at cap");
    close(trapped.simulated_seconds, 0, "zero-time corrections consumed time");
    close(trapped.discarded_seconds, 1, "trapped time was not explicit");
    BallStepper falling(MotionConfig(Vec2(0, -2), second, 4, 0.0001f));
    walls.clear();
    walls.push_back(StaticCollider::segment(Vec2(-10, 0), Vec2(10, 0), 1));
    ball = BallState(Vec2(0, 2), Vec2(), 1);
    const MotionStep floor = falling.step(ball, second, walls);
    check(floor.contacts.size() == 1, "accelerating ball did not hit endpoint floor");
    close(ball.velocity.y, 2, "response did not use acceleration at impact");
    close(ball.position.y, 1.0001, "accelerated floor contact position");
}
void validation_and_atomicity() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    throws<std::invalid_argument>([] { MotionConfig(Vec2(), 0, 1, 0); }, "zero max step accepted");
    throws<std::invalid_argument>([] { MotionConfig(Vec2(), -1, 1, 0); }, "negative max step accepted");
    throws<std::invalid_argument>([] { MotionConfig(Vec2(), second, 0, 0); }, "zero contact budget accepted");
    throws<std::invalid_argument>([] { MotionConfig(Vec2(), second, 1, -1); }, "negative slop accepted");
    throws<std::invalid_argument>([&] { MotionConfig(Vec2(nan, 0), second, 1, 0); }, "NaN acceleration accepted");
    throws<std::invalid_argument>([] { StaticCollider::disk(Vec2(), -1, 1); }, "negative disk radius accepted");
    throws<std::invalid_argument>([] { StaticCollider::disk(Vec2(), 1, 1.01f); }, "energy-adding material accepted");
    throws<std::invalid_argument>([&] { BallState(Vec2(), Vec2(infinity, 0), 1); }, "infinite velocity accepted");
    throws<std::invalid_argument>([] { BallState(Vec2(), Vec2(), 0); }, "zero ball radius accepted");
    BallStepper stepper(MotionConfig(Vec2(), second, 1, 0));
    std::vector<StaticCollider> colliders;
    BallState ball(Vec2(1, 2), Vec2(3, 4), 1);
    throws<std::invalid_argument>([&] { stepper.step(ball, -1, colliders); }, "negative duration accepted");
    throws<std::invalid_argument>([&] { stepper.step(ball, std::numeric_limits<Tick>::min(), colliders); },
                                  "minimum Tick duration accepted");
    throws<std::invalid_argument>([&] { stepper.step(ball, second + 1, colliders); }, "time budget ignored");
    colliders.push_back(StaticCollider::disk(Vec2(), 1, 1));
    colliders[0].kind = static_cast<ColliderKind>(99);
    throws<std::invalid_argument>([&] { stepper.step(ball, 0, colliders); }, "invalid collider tag accepted");
    check(ball.position.x == 1 && ball.velocity.y == 4, "invalid step partially modified ball");
    colliders.clear();
    ball.radius = nan;
    throws<std::invalid_argument>([&] { stepper.step(ball, second, colliders); }, "mutated invalid ball accepted");
    const float limit = std::numeric_limits<float>::max();
    ball = BallState(Vec2(limit, 0), Vec2(limit, 0), 1);
    throws<std::overflow_error>([&] { stepper.step(ball, second, colliders); }, "position overflow wrapped");
    check(ball.position.x == limit && ball.velocity.x == limit, "overflow partially committed ball");
    MotionConfig mutable_config(Vec2(), second, 1, 0);
    mutable_config.max_contacts = 0;
    throws<std::invalid_argument>([&] { BallStepper invalid(mutable_config); }, "mutated motion config accepted");

    throws<std::invalid_argument>([] { PlungerConfig(0, second, Vec2(1, 0), 0, 1, 0); },
                                  "zero pull duration accepted");
    throws<std::invalid_argument>([] { PlungerConfig(second, -1, Vec2(1, 0), 0, 1, 0); },
                                  "negative return duration accepted");
    throws<std::invalid_argument>([] { PlungerConfig(second, second, Vec2(), 0, 1, 0); },
                                  "zero launch direction accepted");
    throws<std::invalid_argument>([] { PlungerConfig(second, second, Vec2(1, 0), 2, 1, 0); },
                                  "reversed speed interval accepted");
    throws<std::invalid_argument>([] { PlungerConfig(second, second, Vec2(1, 0), 0, 1, 1.1f); },
                                  "invalid pull threshold accepted");
    throws<std::invalid_argument>([&] { PlungerConfig(second, second, Vec2(1, 0), 0, infinity, 0); },
                                  "nonfinite launch speed accepted");
    throws<std::invalid_argument>([] { TimedPlunger invalid(plunger_config(), -1); },
                                  "negative initial time accepted");
    PlungerConfig mutable_plunger = plunger_config();
    mutable_plunger.minimum_pull = nan;
    throws<std::invalid_argument>([&] { TimedPlunger invalid(mutable_plunger); },
                                  "mutated plunger config accepted");
}
void terminal_capture() {
    BallStepper stepper(MotionConfig(Vec2(),second,8,.0001f));
    std::vector<StaticCollider> colliders;
    colliders.push_back(StaticCollider::segment(Vec2(4,-10),Vec2(4,10),1));
    colliders.push_back(StaticCollider::segment(Vec2(0,-10),Vec2(0,10),1));
    check(!colliders[0].captures_ball&&!colliders[1].captures_ball,"factory enabled capture");
    colliders[1].captures_ball=true;
    colliders[1].reference_response=true;
    colliders[1].material=pinball::response::Material(1,1,-1,100);
    BallState ball(Vec2(-3,0),Vec2(10,0),1);
    MotionStep result=stepper.step(ball,second,colliders);
    check(result.captured&&result.capture_collider_index==1&&result.contacts.size()==1,
          "earliest capture did not terminate before later wall");
    check(!result.contact_budget_exhausted&&!result.moving_work_exhausted&&
          !result.contacts[0].boost_applied&&!result.contacts[0].moving,"capture applied force or exhaustion");
    close(ball.position.x,-1.0001,"capture center placement",.0001);
    check(ball.velocity.x==0&&ball.velocity.y==0,"capture retained kinetic energy");
    close(result.simulated_seconds,.2,"capture time");close(result.discarded_seconds,.8,"inactive remainder");
    colliders[0]=StaticCollider::segment(Vec2(0,-10),Vec2(0,10),1);
    colliders[1]=StaticCollider::segment(Vec2(4,-10),Vec2(4,10),1);
    colliders[1].captures_ball=true;
    ball=BallState(Vec2(-3,0),Vec2(10,0),1);
    result=stepper.step(ball,second,colliders);
    check(!result.captured&&result.capture_collider_index==0&&result.contacts.size()==1,
          "later capture bypassed an earlier rebound");
    check(ball.velocity.x<0,"earlier wall did not reflect");

    colliders.clear();colliders.push_back(StaticCollider::disk(Vec2(),1,1));colliders[0].captures_ball=true;
    ball=BallState(Vec2(-5,0),Vec2(10,0),1);
    result=stepper.step(ball,second,colliders);
    check(result.captured&&result.capture_collider_index==0,"disk capture missing");
    close(result.simulated_seconds,.3,"disk capture time");close(ball.position.x,-2.0001,"disk capture placement",.0001);
    ball=BallState(Vec2(),Vec2(10,0),1);
    result=stepper.step(ball,0,colliders);
    check(!result.captured&&ball.velocity.x==10,"zero-duration step captured ball");
    result=stepper.step(ball,second,colliders);
    check(result.captured&&result.contacts[0].hit.initial_overlap&&ball.velocity.x==0,
          "initial overlap capture failed");
    close(result.simulated_seconds,0,"overlap capture consumed active time");
    close(result.discarded_seconds,1,"overlap inactive remainder");

    // One ordinary response followed by a terminal capture is bounded and
    // must not be mistaken for a response-budget exhaustion.
    colliders.clear();
    colliders.push_back(StaticCollider::segment(Vec2(0,-10),Vec2(0,10),1));
    colliders.push_back(StaticCollider::segment(Vec2(-5,-10),Vec2(-5,10),1));
    colliders[1].captures_ball=true;
    ball=BallState(Vec2(-3,0),Vec2(10,0),1);
    result=BallStepper(MotionConfig(Vec2(),second,1,.0001f)).step(ball,second,colliders);
    check(result.captured&&result.contacts.size()==2&&!result.contact_budget_exhausted,
          "terminal capture was blocked by response budget");
    close(result.simulated_seconds+result.discarded_seconds,1,"capture total time");

    const pinball::flipper::FlipperConfig config(Vec2(),Vec2(2,0),Vec2(0,2),.2f,.2f,
        second,second*2,.75f,.5f,.9f,pinball::flipper::ResponseMode::ReferenceMaterial);
    const std::vector<pinball::flipper::FlipperMotion> moving(1,pinball::flipper::FlipperMotion(config,0,1));
    colliders.assign(1,StaticCollider::segment(Vec2(0,.6f),Vec2(2,.6f),1));
    colliders[0].captures_ball=true;
    ball=BallState(Vec2(1,.4f),Vec2(),.1f);
    result=stepper.step(ball,second,colliders,moving,pinball::flipper::MovingSweepConfig(128,8192,.00002,1e-8));
    check(result.captured&&result.contacts.size()==2&&result.contacts[0].moving&&
          result.contacts[0].boost_applied&&result.flipper_yields.size()==1,
          "capture lost earlier validated flipper yield");
    check(result.flipper_yields[0].seconds_from_start<result.simulated_seconds,
          "capture precedes its earlier flipper yield");
    check(ball.velocity.x==0&&ball.velocity.y==0&&!result.contact_budget_exhausted&&
          !result.moving_work_exhausted,"yield/capture retained energy or reported exhaustion");
}
void center_capture_sensors() {
    BallStepper stepper(MotionConfig(Vec2(),second,8,.5f));
    std::vector<StaticCollider> sensors(1,StaticCollider::center_capture(Vec2(),.1f));
    check(sensors[0].enabled && sensors[0].captures_ball && sensors[0].center_trigger &&
          !sensors[0].reference_response,"center capture factory flags");
    check(!StaticCollider::disk(Vec2(),1,1).center_trigger &&
          !StaticCollider::segment(Vec2(),Vec2(1,0),1).center_trigger,
          "ordinary factories enabled center sensors");

    // A visible ball can overlap the sensor's circle without its center
    // entering. Capture must not be enlarged by the visible ball radius.
    BallState ball(Vec2(-2,.2f),Vec2(4,0),1);
    MotionStep result=stepper.step(ball,second,sensors);
    check(!result.captured&&result.contacts.empty(),"center sensor inflated by ball radius");
    close(ball.position.x,2,"center sensor near-miss path");
    close(ball.position.y,.2,"center sensor near-miss y");

    ball=BallState(Vec2(-2,0),Vec2(4,0),1);
    result=stepper.step(ball,second,sensors);
    check(result.captured&&result.capture_collider_index==0&&result.contacts.size()==1,
          "center sensor crossing missed");
    close(result.simulated_seconds,.475,"center trigger used expanded TOI");
    close(result.discarded_seconds,.525,"center trigger inactive time");
    close(ball.position.x,-.1,"center trigger added ball radius or separation slop");
    close(result.contacts[0].hit.point.x,ball.position.x,"center event point differs from center");
    check(ball.velocity.x==0&&ball.velocity.y==0&&!result.contact_budget_exhausted&&
          !result.moving_work_exhausted&&!result.contacts[0].boost_applied,
          "center capture retained energy or reported exhaustion");

    // Sweep the center through a much smaller sensor in one fast step.
    sensors[0]=StaticCollider::center_capture(Vec2(),.01f);
    ball=BallState(Vec2(-10,0),Vec2(2000,0),.3f);
    result=stepper.step(ball,second/100,sensors);
    check(result.captured&&result.contacts.size()==1,"fast center crossing tunneled");
    close(result.simulated_seconds,.004995,"fast center capture TOI",.00000001);
    close(ball.position.x,-.01,"fast center capture position",.00001);

    sensors[0]=StaticCollider::center_capture(Vec2(),.1f);
    ball=BallState(Vec2(.05f,0),Vec2(),1);
    result=stepper.step(ball,0,sensors);
    check(!result.captured&&ball.position.x==.05f,"zero-duration center sensor captured");
    result=stepper.step(ball,second,sensors);
    check(result.captured&&result.contacts[0].hit.initial_overlap,
          "stationary center containment missed");
    close(result.simulated_seconds,0,"initial center capture consumed time");
    close(result.discarded_seconds,1,"initial center capture lost inactive time");
    close(ball.position.x,.05,"initial center capture teleported ball");
    close(ball.position.y,0,"initial center capture changed y");

    sensors.push_back(StaticCollider::segment(Vec2(-.5f,-10),Vec2(-.5f,10),1));
    ball=BallState(Vec2(-2,0),Vec2(4,0),1);
    result=stepper.step(ball,second,sensors);
    check(!result.captured&&result.contacts.size()==1&&result.contacts[0].collider_index==1,
          "center sensor bypassed earlier physical wall");
    close(ball.velocity.x,-4,"earlier wall reflection changed");

    throws<std::invalid_argument>([]{StaticCollider::center_capture(Vec2(),0);},
                                  "zero center trigger accepted");
    throws<std::invalid_argument>([]{StaticCollider::center_capture(Vec2(),-1);},
                                  "negative center trigger accepted");
    throws<std::invalid_argument>([]{StaticCollider::center_capture(Vec2(),std::numeric_limits<float>::infinity());},
                                  "infinite center trigger accepted");
    throws<std::invalid_argument>([]{StaticCollider::center_capture(Vec2(),std::numeric_limits<float>::quiet_NaN());},
                                  "NaN center trigger accepted");
    sensors.assign(1,StaticCollider::segment(Vec2(),Vec2(1,0),1));
    sensors[0].center_trigger=true;sensors[0].captures_ball=true;
    ball=BallState(Vec2(2,3),Vec2(4,5),1);
    throws<std::invalid_argument>([&]{stepper.step(ball,second,sensors);},
                                  "mutated segment center sensor accepted");
    check(ball.position.x==2&&ball.velocity.y==5,"invalid center sensor partially committed ball");
}
void enabled_collider_search() {
    BallStepper stepper(MotionConfig(Vec2(),second,8,.0001f));
    std::vector<StaticCollider> colliders(1,StaticCollider::center_capture(Vec2(),.1f));
    colliders[0].enabled=false;
    BallState ball(Vec2(-2,0),Vec2(4,0),1);
    MotionStep result=stepper.step(ball,second,colliders);
    check(!result.captured&&result.contacts.empty(),"disabled center trigger captured");
    close(ball.position.x,2,"disabled center trigger blocked free flight");
    colliders[0].enabled=true;
    ball=BallState(Vec2(-2,0),Vec2(4,0),1);
    result=stepper.step(ball,second,colliders);
    check(result.captured&&result.contacts.size()==1,"re-enabled center trigger did not capture");
    close(result.simulated_seconds,.475,"re-enabled center capture TOI");

    colliders[0].enabled=false;
    ball=BallState(Vec2(.05f,0),Vec2(),1);
    result=stepper.step(ball,second,colliders);
    check(!result.captured&&ball.position.x==.05f,"disabled contained-center trigger captured");
    colliders[0].enabled=true;
    result=stepper.step(ball,second,colliders);
    check(result.captured&&result.contacts[0].hit.initial_overlap,
          "re-enabled contained-center trigger missed");

    colliders[0]=StaticCollider::segment(Vec2(0,-10),Vec2(0,10),1);
    check(colliders[0].enabled&&StaticCollider::disk(Vec2(),1,1).enabled,
          "ordinary collider factory disabled geometry");
    colliders[0].enabled=false;
    ball=BallState(Vec2(-2,0),Vec2(4,0),1);
    result=stepper.step(ball,second,colliders);
    check(result.contacts.empty()&&ball.velocity.x==4,"disabled wall reflected ball");
    close(ball.position.x,2,"disabled wall blocked free flight");
    colliders[0].enabled=true;
    ball=BallState(Vec2(-2,0),Vec2(4,0),1);
    result=stepper.step(ball,second,colliders);
    check(result.contacts.size()==1&&ball.velocity.x==-4,"re-enabled wall did not reflect");

    // Disabling is an eligibility policy, not permission for malformed data.
    colliders[0].enabled=false;
    colliders[0].radius=std::numeric_limits<float>::quiet_NaN();
    ball=BallState(Vec2(2,3),Vec2(4,5),1);
    throws<std::invalid_argument>([&]{stepper.step(ball,second,colliders);},
                                  "disabled malformed collider skipped validation");
    check(ball.position.x==2&&ball.velocity.y==5,"invalid disabled collider partially committed ball");
}
SurfaceTransition active_surface(std::uint32_t mask,std::size_t id,Vec2 gradient,float offset) {
    SurfaceTransition target;
    target.target_mask=mask;target.plane.active=true;target.plane.id=id;
    target.plane.gradient=gradient;target.plane.offset=offset;
    target.plane.field_force=Vec2(3,-4);target.plane.drag=.2f;
    return target;
}
StaticCollider vertical_transition(float x,std::uint32_t listen,const SurfaceTransition& target) {
    return StaticCollider::transition_segment(Vec2(x,1),Vec2(x,-1),listen,target);
}
void surface_center_crossing() {
    BallStepper stepper(MotionConfig(Vec2(),second,8,.25f));
    BallState ball(Vec2(2,0),Vec2(-4,0),1);
    check(ball.collision_mask==1&&ball.height==1&&!ball.plane.active&&
          ball.plane.id==(std::numeric_limits<std::size_t>::max)(),"ball surface defaults");
    const SurfaceTransition target=active_surface(2,7,Vec2(.5f,.25f),2);
    std::vector<StaticCollider> edges(1,vertical_transition(0,1,target));
    check(edges[0].center_crossing&&!edges[0].captures_ball&&!edges[0].center_trigger&&
          edges[0].collision_mask==1,"transition factory flags");
    check(StaticCollider::disk(Vec2(),1,1).collision_mask==
          (std::numeric_limits<std::uint32_t>::max)()&&!StaticCollider::disk(Vec2(),1,1).center_crossing,
          "ordinary collider mask defaults");
    MotionStep result=stepper.step(ball,second,edges);
    check(result.contacts.size()==1&&result.contacts[0].transitioned&&
          !result.contacts[0].moving&&!result.contacts[0].boost_applied&&!result.captured,
          "point crossing did not emit a pure transition");
    close(result.contacts[0].seconds_from_start,.5,"transition used ball-radius offset");
    close(result.contacts[0].hit.point.x,0,"transition event center");
    close(ball.position.x,-2,"transition added a separation push");
    close(ball.velocity.x,-4,"transition changed velocity");
    close(ball.height,2,"active plane height after remaining movement");
    check(ball.collision_mask==2&&ball.plane.active&&ball.plane.id==7&&
          ball.plane.field_force.x==3&&ball.plane.drag==.2f,"transition lost surface metadata");

    ball=BallState(Vec2(2,1.2f),Vec2(-4,0),1);
    result=stepper.step(ball,second,edges);
    check(result.contacts.empty()&&ball.collision_mask==1,"transition had inflated endpoint caps");
    ball=BallState(Vec2(0,0),Vec2(0,4),1);
    result=stepper.step(ball,second,edges);
    check(result.contacts.empty(),"parallel grazing triggered transition");
    ball=BallState(Vec2(-2,0),Vec2(4,0),1);
    result=stepper.step(ball,second,edges);
    check(result.contacts.empty()&&ball.collision_mask==1,"backface triggered directed transition");
    ball=BallState(Vec2(2,0),Vec2(-4,0),10);
    result=stepper.step(ball,second,edges);
    close(result.contacts[0].seconds_from_start,.5,"large visible radius changed center TOI");
    ball=BallState(Vec2(1000,0),Vec2(-200000,0),.1f);
    result=stepper.step(ball,second/100,edges);
    check(result.contacts.size()==1&&result.contacts[0].transitioned,"fast center transition tunneled");
    close(result.contacts[0].seconds_from_start,.005,"fast center transition TOI",.00000001);
    close(ball.position.x,-1000,"fast transition lost remaining travel",.001);

    // The selected edge may retain the same mask. Suppress that edge for
    // the rest of this fixed step, including an exact zero-time crossing.
    SurfaceTransition unchanged;unchanged.target_mask=1;
    edges[0]=vertical_transition(0,1,unchanged);
    ball=BallState(Vec2(0,0),Vec2(-4,0),1);
    result=stepper.step(ball,second,edges);
    check(result.contacts.size()==1&&!result.contact_budget_exhausted,
          "zero-time same-mask transition repeated");
    close(result.simulated_seconds,1,"same-mask transition prevented free flight");
    close(ball.position.x,-4,"same-mask transition displaced center");
    ball=BallState(Vec2(0,0),Vec2(-4,0),1);
    result=stepper.step(ball,0,edges);
    check(result.contacts.empty()&&ball.collision_mask==1,"zero-duration transition applied");

    edges.clear();edges.push_back(vertical_transition(0,1,target));
    SurfaceTransition other=target;other.target_mask=4;
    edges.push_back(vertical_transition(0,1,other));
    ball=BallState(Vec2(2,0),Vec2(-4,0),1);
    result=stepper.step(ball,second,edges);
    check(result.contacts.size()==1&&result.contacts[0].collider_index==0&&ball.collision_mask==2,
          "equal-time transition order changed");
}
void mask_handoff_and_surface_height() {
    BallStepper stepper(MotionConfig(Vec2(),second,8,.0001f));
    SurfaceTransition ramp=active_surface(2,10,Vec2(.5f,0),5);
    std::vector<StaticCollider> edges;
    StaticCollider ground=StaticCollider::segment(Vec2(-1,-10),Vec2(-1,10),1);
    ground.collision_mask=1;edges.push_back(ground);
    StaticCollider raised=StaticCollider::segment(Vec2(-2,-10),Vec2(-2,10),1);
    raised.collision_mask=2;edges.push_back(raised);
    edges.push_back(vertical_transition(0,1,ramp));
    BallState ball(Vec2(2,0),Vec2(-4,0),.25f);
    MotionStep result=stepper.step(ball,second,edges);
    check(result.contacts.size()==2&&result.contacts[0].collider_index==2&&
          result.contacts[0].transitioned&&result.contacts[1].collider_index==1&&
          !result.contacts[1].transitioned,"mask handoff did not precede ground wall");
    close(result.contacts[0].seconds_from_start,.5,"entry time before ground wall");
    close(result.contacts[1].seconds_from_start,.9375,"raised-mask wall time");
    close(ball.velocity.x,4,"new-mask wall did not reflect");
    close(ball.height,ball.radius+.5*ball.position.x+5,"height not synced after physical contact");

    edges.clear();
    edges.push_back(vertical_transition(0,1,ramp));
    StaticCollider capture=StaticCollider::center_capture(Vec2(-1,0),.1f);
    capture.collision_mask=2;edges.push_back(capture);
    ball=BallState(Vec2(2,0),Vec2(-4,0),.25f);
    result=BallStepper(MotionConfig(Vec2(),second,1,.0001f)).step(ball,second,edges);
    check(result.captured&&result.contacts.size()==2&&result.contacts[0].transitioned&&
          !result.contacts[1].transitioned&&!result.contact_budget_exhausted,
          "capture after mask handoff lost event or response-budget policy");
    close(ball.height,ball.radius+.5*ball.position.x+5,"captured constrained height");

    // Adjacent surfaces are continuous at x=-1, but use different gradients.
    edges.clear();edges.push_back(vertical_transition(0,1,active_surface(2,10,Vec2(1,0),5)));
    edges.push_back(vertical_transition(-1,2,active_surface(2,11,Vec2(2,0),6)));
    SurfaceTransition exit;exit.target_mask=4;
    edges.push_back(vertical_transition(-2,2,exit));
    ball=BallState(Vec2(1,0),Vec2(-4,0),1);
    result=stepper.step(ball,second,edges);
    check(result.contacts.size()==3&&ball.collision_mask==4&&!ball.plane.active,
          "adjacent plane / upper exit mask sequence");
    for(std::size_t i=0;i<result.contacts.size();++i){
        check(result.contacts[i].transitioned,"plane edge reflected instead of transitioning");
        close(result.contacts[i].seconds_from_start,.25*(i+1),"plane edge chronological order");
    }
    close(ball.position.x,-3,"plane transitions altered remaining XY movement");
    close(ball.height,3,"inactive exit did not preserve prior constrained height");
    close(ball.velocity.x,-4,"plane updates changed velocity");

    // Back out of the same mouth, then exercise an explicit destination Z.
    edges.assign(1,StaticCollider::transition_segment(Vec2(0,-1),Vec2(0,1),2,exit));
    edges[0].transition.target_mask=1;
    ball=BallState(Vec2(-.5f,0),Vec2(1,0),1);ball.collision_mask=2;
    ball.plane=active_surface(2,10,Vec2(1,0),5).plane;
    result=stepper.step(ball,second,edges);
    check(result.contacts.size()==1&&ball.collision_mask==1&&!ball.plane.active,
          "backout did not return to ground");
    close(ball.position.x,.5,"backout XY continuity");
    close(ball.height,6,"backout preserve-height policy");
    edges[0].transition.set_height=true;edges[0].transition.height_offset=.25f;
    ball=BallState(Vec2(-.5f,0),Vec2(1,0),1);ball.collision_mask=2;
    ball.plane=active_surface(2,10,Vec2(1,0),5).plane;
    result=stepper.step(ball,second,edges);
    close(ball.height,1.25,"explicit exit height offset");

    // A foreign-layer ordinary wall is ignored; generic all-bit walls still hit.
    edges.assign(1,ground);edges[0].collision_mask=2;
    ball=BallState(Vec2(2,0),Vec2(-4,0),.25f);
    result=stepper.step(ball,second,edges);
    check(result.contacts.empty()&&ball.position.x==-2,"foreign-layer static collider blocked ground");
    edges[0].collision_mask=(std::numeric_limits<std::uint32_t>::max)();
    ball=BallState(Vec2(2,0),Vec2(-4,0),.25f);ball.height=9;
    result=stepper.step(ball,second,edges);
    check(result.contacts.size()==1&&ball.velocity.x==4&&ball.height==9,
          "generic all-bit collider or unconstrained height changed");
}
void transition_budgets_and_moving_masks() {
    BallStepper stepper(MotionConfig(Vec2(),second,8,.0001f));
    SurfaceTransition same;
    std::vector<StaticCollider> edges;
    edges.push_back(vertical_transition(1,1,same));edges.push_back(vertical_transition(0,1,same));
    BallState ball(Vec2(2,0),Vec2(-4,0),1);
    const MotionStep limited=BallStepper(MotionConfig(Vec2(),second,1,.0001f)).step(ball,second,edges);
    check(limited.contact_budget_exhausted&&limited.contacts.size()==1&&limited.contacts[0].transitioned,
          "transition work did not use contact budget");
    close(limited.simulated_seconds,.25,"transition budget validated time");
    close(limited.discarded_seconds,.75,"transition budget discarded time");
    close(ball.position.x,1,"transition budget drifted past unvalidated event");

    const pinball::flipper::FlipperConfig config(Vec2(),Vec2(2,0),Vec2(0,2),.2f,.2f,second,second,1);
    const std::vector<pinball::flipper::FlipperMotion> moving(1,pinball::flipper::FlipperMotion(config,0,0));
    const pinball::flipper::MovingSweepConfig work(128,8192,.00002,1e-8);
    edges.clear();ball=BallState(Vec2(1,1),Vec2(0,-2),.1f);ball.collision_mask=2;
    MotionStep result=stepper.step(ball,second,edges,moving,work);
    check(result.contacts.empty()&&result.moving_iterations==0,"nonground flipper was queried");
    close(ball.position.y,-1,"nonground ball reflected from ground flipper");
    BallState ground_ball(Vec2(1,1),Vec2(0,-2),.1f);
    result=stepper.step(ground_ball,second,edges,moving,work);
    check(result.contacts.size()==1&&result.contacts[0].moving&&!result.contacts[0].transitioned,
          "generic ground flipper response changed");
    ball=BallState(Vec2(1,1),Vec2(0,-2),.1f);ball.collision_mask=2;
    result=BallStepper(MotionConfig(Vec2(),second,8,.0001f,0,2)).step(ball,second,edges,moving,work);
    check(result.contacts.size()==1&&result.contacts[0].moving,"configured moving mask ignored");

    SurfaceTransition raised;raised.target_mask=2;
    edges.push_back(StaticCollider::transition_segment(Vec2(0,.8f),Vec2(2,.8f),1,raised));
    ball=BallState(Vec2(1,1),Vec2(0,-2),.1f);
    result=stepper.step(ball,second,edges,moving,work);
    check(result.contacts.size()==1&&result.contacts[0].transitioned&&ball.collision_mask==2,
          "mask change failed to suppress a later ground flipper");
    close(ball.position.y,-1,"mask transition flipper skip lost free flight");
}
void surface_validation_and_atomicity() {
    const float nan=(std::numeric_limits<float>::quiet_NaN)();
    const float infinity=(std::numeric_limits<float>::infinity)();
    SurfaceTransition target;
    target.target_mask=0;
    throws<std::invalid_argument>([&]{vertical_transition(0,1,target);},"zero transition target accepted");
    target=SurfaceTransition();target.plane.gradient.x=nan;
    throws<std::invalid_argument>([&]{vertical_transition(0,1,target);},"NaN inactive plane gradient accepted");
    target=SurfaceTransition();target.plane.field_force.y=infinity;
    throws<std::invalid_argument>([&]{vertical_transition(0,1,target);},"infinite plane field accepted");
    target=SurfaceTransition();target.plane.drag=-1;
    throws<std::invalid_argument>([&]{vertical_transition(0,1,target);},"negative plane drag accepted");
    target=SurfaceTransition();target.height_offset=nan;
    throws<std::invalid_argument>([&]{vertical_transition(0,1,target);},"NaN exit height accepted");
    target=SurfaceTransition();
    throws<std::invalid_argument>([&]{StaticCollider::transition_segment(Vec2(),Vec2(),1,target);},
                                  "zero-length transition accepted");
    BallStepper stepper(MotionConfig(Vec2(),second,8,0));
    std::vector<StaticCollider> edges(1,vertical_transition(0,1,target));
    BallState ball(Vec2(1,0),Vec2(-4,0),1);ball.height=nan;
    throws<std::invalid_argument>([&]{stepper.step(ball,second,edges);},"nonfinite ball height accepted");
    ball=BallState(Vec2(1,0),Vec2(-4,0),1);ball.plane.offset=infinity;
    throws<std::invalid_argument>([&]{stepper.step(ball,second,edges);},"nonfinite ball plane accepted");
    ball=BallState(Vec2(1,0),Vec2(-4,0),1);
    edges[0].transition.plane.drag=nan;edges[0].enabled=false;
    throws<std::invalid_argument>([&]{stepper.step(ball,second,edges);},
                                  "disabled malformed transition skipped validation");
    check(ball.position.x==1&&ball.collision_mask==1&&ball.height==1,"invalid transition partially committed");
    const float maximum=(std::numeric_limits<float>::max)();
    edges[0]=vertical_transition(0,1,active_surface(2,1,Vec2(maximum,0),maximum));
    throws<std::overflow_error>([&]{stepper.step(ball,second,edges);},"surface height overflow accepted");
    check(ball.position.x==1&&ball.collision_mask==1&&ball.height==1&&!ball.plane.active,
          "surface height overflow partially committed");
}
void pre_query_speed_limit() {
    const std::vector<StaticCollider> empty;
    BallStepper limited(MotionConfig(Vec2(),second,8,.0001f,60));
    BallStepper generic(MotionConfig(Vec2(),second,8,.0001f));
    check(generic.config().maximum_query_speed==0,"default query cap is not disabled");
    BallState ball(Vec2(),Vec2(80,60),1);
    limited.step(ball,second,empty);
    close(ball.velocity.x,48,"pre-query cap changed direction x");
    close(ball.velocity.y,36,"pre-query cap changed direction y");
    close(ball.position.x,48,"initial over-limit velocity moved before cap");
    close(ball.position.y,36,"initial capped trajectory y");
    ball=BallState(Vec2(),Vec2(100,0),1);
    limited.step(ball,0,empty);
    check(ball.velocity.x==100&&ball.position.x==0,"zero-duration no-query step changed velocity");

    std::vector<StaticCollider> colliders;
    colliders.push_back(StaticCollider::segment(Vec2(-100,0),Vec2(100,0),1));
    colliders[0].reference_response=true;
    colliders[0].material=pinball::response::Material(1,1,0,100);
    ball=BallState(Vec2(0,2),Vec2(0,-4),1);
    MotionStep capped=limited.step(ball,second,colliders);
    check(capped.contacts.size()==1&&capped.contacts[0].boost_applied,"powered contact not recorded under cap");
    close(capped.contacts[0].seconds_from_start,.25,"cap changed pre-impact collision time");
    close(ball.velocity.y,60,"powered response was not capped before next query");
    close(ball.position.y,46.0001,"post-impact travel exceeded configured cap",.0002);
    check(!capped.contact_budget_exhausted&&!capped.moving_work_exhausted,"cap reported exhaustion");
    BallState uncapped(Vec2(0,2),Vec2(0,-4),1);
    generic.step(uncapped,second,colliders);
    close(uncapped.velocity.y,104,"default generic mode lost uncapped boost");
    close(uncapped.position.y,79.0001,"default generic post-impact travel changed",.0002);

    // This far wall is reached by the uncapped path but is beyond the capped
    // remaining travel. The cap must change the next query, not just output.
    colliders.push_back(StaticCollider::segment(Vec2(-100,60),Vec2(100,60),1));
    ball=BallState(Vec2(0,2),Vec2(0,-4),1);
    capped=limited.step(ball,second,colliders);
    check(capped.contacts.size()==1,"capped movement reached an unreachable later wall");
    uncapped=BallState(Vec2(0,2),Vec2(0,-4),1);
    const MotionStep fast=generic.step(uncapped,second,colliders);
    check(fast.contacts.size()==2&&fast.contacts[1].collider_index==1,"uncapped control did not reach far wall");

    // An actually reachable nearer wall must still be selected after boost.
    colliders.insert(colliders.begin(),StaticCollider::segment(Vec2(-100,30),Vec2(100,30),1));
    ball=BallState(Vec2(0,2),Vec2(0,-4),1);
    capped=limited.step(ball,second,colliders);
    check(capped.contacts.size()==2&&capped.contacts[0].collider_index==1&&capped.contacts[1].collider_index==0,
          "pre-query cap changed earliest reachable wall ordering");
    close(capped.contacts[1].seconds_from_start,.25+(28-.0001)/60,
          "second contact did not use capped travel time",.00001);
    close(ball.velocity.y,-60,"subsequent wall response changed capped speed");

    // At the exact endpoint a boost is preserved as an event/velocity until
    // a later movement query; no above-limit travel occurs in this step.
    colliders.resize(0);
    colliders.push_back(StaticCollider::segment(Vec2(-100,0),Vec2(100,0),1));
    colliders[0].reference_response=true;
    colliders[0].material=pinball::response::Material(1,1,0,100);
    ball=BallState(Vec2(0,2),Vec2(0,-4),1);
    const MotionStep endpoint=limited.step(ball,second/4,colliders);
    check(endpoint.contacts.size()==1&&endpoint.contacts[0].boost_applied,"endpoint boost event lost");
    close(ball.velocity.y,104,"endpoint response was capped without another query");
    close(ball.position.y,1.0001,"endpoint response traveled after impact",.0001);
    limited.step(ball,second/4,empty);
    close(ball.velocity.y,60,"following movement query failed to cap endpoint response");
    close(ball.position.y,16.0001,"following capped movement distance",.0002);

    throws<std::invalid_argument>([]{MotionConfig(Vec2(),second,1,0,-1);},"negative query cap accepted");
    throws<std::invalid_argument>([]{MotionConfig(Vec2(),second,1,0,std::numeric_limits<float>::infinity());},
                                  "infinite query cap accepted");
    throws<std::invalid_argument>([]{MotionConfig(Vec2(),second,1,0,std::numeric_limits<float>::quiet_NaN());},
                                  "NaN query cap accepted");
    MotionConfig changed(Vec2(),second,1,0,60);changed.maximum_query_speed=-1;
    throws<std::invalid_argument>([&]{BallStepper invalid(changed);},"mutated invalid query cap accepted");
}
void directional_solid_overlap() {
    const BallStepper stepper(MotionConfig(Vec2(),second,8,.0001f));
    std::vector<StaticCollider> walls(1,StaticCollider::segment(Vec2(0,-2),Vec2(0,2),1,false));
    check(!walls[0].directional_approach_only,"ordinary segment unexpectedly directional");
    walls[0].directional_approach_only=true;
    BallState ball(Vec2(-.1f,0),Vec2(-1,0),.3f);
    MotionStep result=stepper.step(ball,second/10,walls);
    check(result.contacts.empty(),"one-way solid caught outgoing overlap");close(ball.position.x,-.2,"one-way exit was displaced");
    ball=BallState(Vec2(-.1f,0),Vec2(0,1),.3f);result=stepper.step(ball,second/10,walls);
    check(result.contacts.empty(),"one-way solid caught tangent overlap");
    ball=BallState(Vec2(-.1f,0),Vec2(1,0),.3f);result=stepper.step(ball,second/10,walls);
    check(result.contacts.size()==1&&ball.velocity.x<0,"one-way solid lost inward response");
    walls[0].directional_approach_only=false;ball=BallState(Vec2(-.1f,0),Vec2(-1,0),.3f);
    result=stepper.step(ball,second/10,walls);check(!result.contacts.empty(),"ordinary wall lost overlap correction");
    walls[0].two_sided=true;walls[0].directional_approach_only=true;
    throws<std::invalid_argument>([&]{stepper.step(ball,0,walls);},"directional two-sided wall accepted");
}
void total_speed_at_contact() {
    const BallStepper stepper(MotionConfig(Vec2(),second,8,.0001f));
    std::vector<StaticCollider> edges(1,StaticCollider::event_segment(Vec2(1,-2),Vec2(1,2),1));
    BallState ball(Vec2(),Vec2(3,4),.1f);
    MotionStep result=stepper.step(ball,second,edges);
    check(result.contacts.size()==1&&result.contacts[0].triggered,"incoming speed sensor fixture");
    close(result.contacts[0].incoming_speed,5,"sensor must report total incoming speed");
    edges[0]=StaticCollider::segment(Vec2(1,-2),Vec2(1,2),.5f);
    ball=BallState(Vec2(),Vec2(3,4),.1f);result=stepper.step(ball,second/2,edges);
    check(result.contacts.size()==1,"incoming speed wall fixture");
    close(result.contacts[0].incoming_speed,5,"wall speed was measured after response");
    const BallStepper limited(MotionConfig(Vec2(),second,8,.0001f,20));
    edges[0]=StaticCollider::event_segment(Vec2(1,-2),Vec2(1,2),1);
    ball=BallState(Vec2(),Vec2(30,40),.1f);result=limited.step(ball,second/5,edges);
    check(result.contacts.size()==1,"limited speed sensor fixture");
    close(result.contacts[0].incoming_speed,20,"event speed ignored pre-query clamp");
}
void launch_impulse_once() {
    TimedPlunger plunger(plunger_config());
    BallState seated(Vec2(10, 20), Vec2(), 1);
    plunger.set_pressed(true, 0, true);
    LaunchEvent event = plunger.set_pressed(false, second, true);
    if (event.fired) seated.velocity = pinball::physics::add(seated.velocity, event.delta_velocity);
    event = plunger.set_pressed(false, second, false);
    if (event.fired) seated.velocity = pinball::physics::add(seated.velocity, event.delta_velocity);
    plunger.update(second * 2);
    close(seated.velocity.y, -10, "launch impulse applied more than once");
    check(seated.position.x == 10 && seated.position.y == 20, "launch teleported seated ball");
}
} // namespace
int main() {
    try {
        directional_solid_overlap(); total_speed_at_contact(); owner_activation_deactivates_all_shapes(); directed_capture_release_overlap(); nonphysical_event_preserves_surface(); component_activation_and_step_latch(); plunger_charge_and_one_shot();
        plunger_tap_seat_and_cancellation();
        plunger_time_boundaries();
        free_flight_and_subdivision();
        swept_contacts_and_order();
        contact_budgets_and_acceleration();
        validation_and_atomicity();
        launch_impulse_once();
        terminal_capture();
        center_capture_sensors();
        enabled_collider_search();
        surface_center_crossing();
        mask_handoff_and_surface_height();
        transition_budgets_and_moving_masks();
        surface_validation_and_atomicity();
        pre_query_speed_limit();
        std::cout << "motion checks passed; parameters and curved sweeps are development policies, not XP equivalence\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
