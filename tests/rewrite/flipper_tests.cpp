#include "flipper.hpp"
#include "motion.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
using namespace pinball;
using flipper::FlipperConfig;
using flipper::FlipperMotion;
using flipper::TimedFlipper;
using flipper::MovingSweepConfig;
using physics::Vec2;
using runtime::Tick;
const Tick second=runtime::ticks_per_second;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void close(double value,double expected,const char* message,double tolerance=.00001){
    check(std::isfinite(value)&&std::fabs(value-expected)<=tolerance,message);
}
template<class E,class F>void throws(F f,const char* message){try{f();}catch(const E&){return;}throw std::runtime_error(message);}
FlipperConfig config(bool mirror=false,Tick rise=100000000,Tick fall=200000000){
    return FlipperConfig(Vec2(),Vec2(mirror?-4:4,0),Vec2(0,4),.2f,.2f,rise,fall,1);
}
MovingSweepConfig search(){return MovingSweepConfig(256,8192,0.000001,0.000000001);}
motion::MotionConfig motion_config(Tick maximum=second,std::size_t contacts=16,Vec2 acceleration=Vec2()){
    return motion::MotionConfig(acceleration,maximum,contacts,.0001f);
}
void geometry_and_timing(){
    TimedFlipper left(config()),right(config(true));
    close(left.pose(),0,"rest pose");
    close(left.motion().duration(),0,"rest has future motion");
    left.set_pressed(true,0);right.set_pressed(true,0);
    close(left.motion().duration(),.1,"raise phase duration");
    check(left.motion().pose().angular_velocity>0&&right.motion().pose().angular_velocity<0,
          "mirror rotation signs");
    left.update(50000000);right.update(50000000);
    close(left.pose(),.5,"partial raise");
    const flipper::FlipperPose lp=left.motion().pose(),rp=right.motion().pose();
    close(lp.tip.x,-rp.tip.x,"mirrored midpoint x");close(lp.tip.y,rp.tip.y,"mirrored midpoint y");
    left.set_pressed(true,50000000);left.update(100000000);
    close(left.pose(),1,"repeat press restarted raise");
    close(left.motion().pose().angular_velocity,0,"held raised shape still rotating");
    left.update(second);close(left.pose(),1,"held pose did not saturate");
    left.set_pressed(false,second);left.update(second+100000000);
    close(left.pose(),.5,"return timing");
    check(left.motion().pose().angular_velocity<0,"return angular velocity missing");
    left.set_pressed(true,second+100000000);left.update(second+125000000);
    close(left.pose(),.75,"repress jumped or restarted pose");
    left.set_pressed(false,second+125000000);left.update(second+275000000);
    close(left.pose(),0,"return did not settle");
    const FlipperConfig changing(Vec2(1,2),Vec2(4,0),Vec2(0,2),.5f,.1f,second,second,.8f);
    const FlipperMotion stretch(changing,0,1);
    const flipper::FlipperPose middle=stretch.pose(.5);
    close(physics::length(physics::subtract(middle.tip,middle.pivot)),3,"declared length interpolation");
    close(middle.length_velocity,-2,"radial length velocity");
    close(stretch.pose(1).tip.x,1,"raised endpoint x");close(stretch.pose(1).tip.y,4,"raised endpoint y");
    close(stretch.pose(2).angular_velocity,0,"phase end overshot into continued rotation");
    const FlipperMotion tapered(changing,0,0);
    const flipper::SurfaceSample sample=tapered.surface(Vec2(3,3),0);
    close(sample.normal.x,.1,"taper normal x");
    close(sample.normal.y,std::sqrt(.99),"taper normal y");
    close(sample.separation,.2+std::sqrt(.99)-.5,"taper distance");
    const FlipperConfig containing(Vec2(),Vec2(4,0),Vec2(0,4),5,.1f,second,second,1);
    const flipper::SurfaceSample large=FlipperMotion(containing,0,0).surface(Vec2(0,6),0);
    close(large.separation,1,"contained end disk did not reduce to larger disk");
}
void rest_raised_and_overlap(){
    motion::BallStepper stepper(motion_config());
    const std::vector<motion::StaticCollider> empty;
    std::vector<FlipperMotion> movers(1,FlipperMotion(config(),0,0));
    motion::BallState ball(Vec2(2,2),Vec2(0,-4),.2f);
    motion::MotionStep result=stepper.step(ball,second,empty,movers,search());
    check(result.contacts.size()==1&&result.contacts[0].moving,"resting flipper was not solid");
    close(ball.velocity.y,4,"rest reflection");close(ball.position.y,2.8001,"rest travel remainder",.0001);
    close(result.contacts[0].surface_velocity.y,0,"static pose transferred velocity");
    movers[0]=FlipperMotion(config(),1,0);
    ball=motion::BallState(Vec2(2,2),Vec2(-4,0),.2f);
    result=stepper.step(ball,second,empty,movers,search());
    check(result.contacts.size()==1,"raised held flipper was not solid");close(ball.velocity.x,4,"held reflection");
    movers[0]=FlipperMotion(config(),0,0);
    ball=motion::BallState(Vec2(1,0),Vec2(),.2f);
    result=stepper.step(ball,second,empty,movers,search());
    check(result.contacts.size()==1&&result.contacts[0].hit.initial_overlap,"initial overlap missing");
    check(ball.position.y>.4&&std::isfinite(ball.position.y),"overlap was not separated");
    ball=motion::BallState(Vec2(1,.4f),Vec2(0,1),.2f);
    result=stepper.step(ball,second,empty,movers,search());
    check(result.contacts.empty(),"outgoing resting contact repeated");
}
motion::MotionStep strike(bool mirror,motion::BallState& ball,Tick duration){
    const FlipperConfig c=config(mirror,duration,duration);
    const std::vector<FlipperMotion> movers(1,FlipperMotion(c,0,double(second)/duration));
    return motion::BallStepper(motion_config(duration)).step(ball,duration,
        std::vector<motion::StaticCollider>(),movers,search());
}
void moving_strike_mirror_and_fast_crossing(){
    motion::BallState left(Vec2(2.8f,2.8f),Vec2(),.2f),right(Vec2(-2.8f,2.8f),Vec2(),.2f);
    const motion::MotionStep l=strike(false,left,100000000),r=strike(true,right,100000000);
    check(!l.contacts.empty()&&!r.contacts.empty(),"moving flipper missed stationary ball");
    check(!l.moving_work_exhausted&&!r.moving_work_exhausted&&!l.contact_budget_exhausted,
          "normal upstroke exhausted work");
    check(left.velocity.x<0&&left.velocity.y>0,"upstroke impulse direction");
    check(physics::length(left.velocity)>20,"upstroke did not transfer surface motion");
    close(left.velocity.x,-right.velocity.x,"mirrored impulse x",.001);
    close(left.velocity.y,right.velocity.y,"mirrored impulse y",.001);
    const motion::MotionContact& contact=l.contacts.front();
    check(contact.moving&&physics::length(contact.surface_velocity)>0,"moving surface evidence missing");
    const float before=physics::dot(physics::subtract(Vec2(),contact.surface_velocity),contact.hit.normal);
    check(before<0,"moving contact was not approaching in relative frame");
    // Start/end shapes both miss this ball. Even a one-microsecond complete
    // stroke must find its swept interior rather than sampling those poses.
    motion::BallState fast(Vec2(2.8f,2.8f),Vec2(),.2f);
    const FlipperMotion motion(config(false,1000,1000),0,1000000);
    check(motion.surface(fast.position,0).separation>fast.radius&&
          motion.surface(fast.position,.000001).separation>fast.radius,"fast sweep fixture endpoints overlap");
    const flipper::MovingSweep hit=flipper::sweep_circle(fast.position,Vec2(),fast.radius,motion,
        0,.000001,MovingSweepConfig(256,8192,.000001,1e-13),256);
    check(hit.hit.hit&&!hit.work_budget_exhausted&&hit.time>0&&hit.time<.000001,
          "fast tip crossing tunneled");
    const std::vector<FlipperMotion> movers(1,motion);
    const motion::MotionStep quick=motion::BallStepper(motion_config(1000)).step(fast,1000,
        std::vector<motion::StaticCollider>(),movers,MovingSweepConfig(256,8192,.000001,1e-13));
    check(!quick.contacts.empty()&&!quick.moving_work_exhausted&&!quick.contact_budget_exhausted,
          "fast full-step crossing failed");
    check(std::isfinite(fast.velocity.x)&&physics::length(fast.velocity)>1000000,
          "fast moving strike lost its impulse");
    TimedFlipper returning(config());returning.set_pressed(true,0);returning.update(100000000);
    returning.set_pressed(false,100000000);
    motion::BallState down(Vec2(2.8f,2.8f),Vec2(),.2f);
    const motion::MotionStep falling=motion::BallStepper(motion_config()).step(down,200000000,
        std::vector<motion::StaticCollider>(),std::vector<FlipperMotion>(1,returning.motion()),search());
    check(!falling.contacts.empty()&&down.velocity.x>0&&down.velocity.y<0,"return motion was not collidable");
}
void earliest_order_and_budgets(){
    motion::BallStepper stepper(motion_config());
    std::vector<motion::StaticCollider> walls;
    walls.push_back(motion::StaticCollider::segment(Vec2(0,-3),Vec2(0,3),1));
    const FlipperConfig line(Vec2(),Vec2(0,3),Vec2(-3,0),0,0,second,second,0);
    std::vector<FlipperMotion> movers(1,FlipperMotion(line,0,0));
    motion::BallState ball(Vec2(-3,1),Vec2(4,0),1);
    motion::MotionStep result=stepper.step(ball,second,walls,movers,search());
    check(result.contacts.size()==1&&!result.contacts[0].moving,
          "static collider did not win equal-time tie");
    close(ball.velocity.x,-4,"equal-time static material did not win");
    walls.clear();movers.push_back(FlipperMotion(FlipperConfig(Vec2(),Vec2(0,3),Vec2(-3,0),
        0,0,second,second,1),0,0));
    ball=motion::BallState(Vec2(-3,1),Vec2(4,0),1);
    result=stepper.step(ball,second,walls,movers,search());
    check(result.contacts.size()==1&&result.contacts[0].moving&&result.contacts[0].collider_index==0,
          "moving input order did not break equal-time tie");
    close(ball.velocity.x,0,"first moving material did not win tie");
    movers[0]=FlipperMotion(FlipperConfig(Vec2(1,0),Vec2(0,3),Vec2(-3,0),
        0,0,second,second,1),0,0);
    movers[1]=FlipperMotion(FlipperConfig(Vec2(-1,0),Vec2(0,3),Vec2(-3,0),
        0,0,second,second,1),0,0);
    ball=motion::BallState(Vec2(-3,1),Vec2(4,0),.2f);
    result=stepper.step(ball,second,walls,movers,search());
    check(!result.contacts.empty()&&result.contacts[0].collider_index==1,
          "earlier moving impact lost to input order");
    movers.assign(1,FlipperMotion(config(),0,10));
    ball=motion::BallState(Vec2(2.8f,2.8f),Vec2(),.2f);
    const motion::BallState before=ball;
    result=stepper.step(ball,100000000,walls,movers,MovingSweepConfig(1,1,.000001,1e-9));
    check(result.moving_work_exhausted&&result.moving_iterations==1&&result.contacts.empty(),
          "query work cap was treated as a miss");
    close(result.simulated_seconds,0,"unvalidated work advanced world");
    close(result.discarded_seconds,.1,"work exhaustion discarded-time accounting");
    check(ball.position.x==before.position.x&&ball.velocity.y==before.velocity.y,
          "unvalidated motion changed ball");
    const flipper::MovingSweep none=flipper::sweep_circle(ball.position,ball.velocity,ball.radius,
        movers[0],0,.1,search(),0);
    check(none.work_budget_exhausted&&none.iterations==0,"zero available work was a miss");
    movers.clear();
    const FlipperConfig a(Vec2(),Vec2(.01f,0),Vec2(0,.01f),1,1,second,second,1);
    const FlipperConfig b(Vec2(3,0),Vec2(.01f,0),Vec2(0,.01f),1,1,second,second,1);
    movers.push_back(FlipperMotion(a,0,0));movers.push_back(FlipperMotion(b,0,0));
    ball=motion::BallState(Vec2(1.5f,0),Vec2(),1);
    result=motion::BallStepper(motion_config(second,1)).step(ball,second,walls,movers,search());
    check(result.contact_budget_exhausted&&result.contacts.size()==1&&result.discarded_seconds==1,
          "moving overlap contact cap failed");
}
void cancellation_phase_end_and_time(){
    TimedFlipper flipper(config());
    flipper.set_pressed(true,0);flipper.update(50000000);
    const double frozen=flipper.pose();
    const runtime::TransitionCause causes[]={runtime::TransitionCause::Paused,runtime::TransitionCause::FocusLost,
        runtime::TransitionCause::Disconnected,runtime::TransitionCause::ExplicitRelease};
    for(std::size_t i=0;i<sizeof(causes)/sizeof(causes[0]);++i){
        flipper.reset();flipper.set_pressed(true,0);flipper.update(50000000);
        flipper.set_pressed(false,50000000,causes[i]);flipper.update(second);
        close(flipper.pose(),frozen,"cancellation moved frozen pose");
        check(!flipper.held()&&flipper.motion().duration()==0&&flipper.motion().pose().angular_velocity==0,
              "cancellation retained future motion");
        flipper.set_pressed(false,second);close(flipper.pose(),frozen,"stale release restarted cancelled cycle");
        flipper.set_pressed(true,second);flipper.update(second+100000000);
        close(flipper.pose(),1,"fresh press did not rearm cancelled flipper");
    }
    flipper.cancel(second+100000000);
    motion::BallState still(Vec2(20,20),Vec2(),.2f);
    const motion::MotionStep stopped=motion::BallStepper(motion_config()).step(still,second,
        std::vector<motion::StaticCollider>(),std::vector<FlipperMotion>(1,flipper.motion()),search());
    check(stopped.contacts.empty()&&still.velocity.x==0&&still.velocity.y==0,
          "cancelled mover generated impulse");
    TimedFlipper resumed(config());
    resumed.set_pressed(true,0);resumed.cancel(50000000);
    const flipper::FlipperPose paused=resumed.motion().pose();
    resumed.update(second);resumed.resume_released(second);
    close(resumed.pose(),.5,"resume jumped frozen pose");
    close(resumed.motion().pose().tip.x,paused.tip.x,"resume moved tip immediately");
    close(resumed.motion().pose().tip.y,paused.tip.y,"resume moved tip y immediately");
    check(!resumed.held()&&resumed.motion().pose().angular_velocity<0,"resume did not start released return");
    resumed.update(second+50000000);close(resumed.pose(),.25,"resumed return timing");
    resumed.update(second+100000000);close(resumed.pose(),0,"resumed return did not settle");
    const FlipperMotion ending(config(),0,10);
    motion::BallState free(Vec2(20,20),Vec2(),.2f);
    const motion::MotionStep split=motion::BallStepper(motion_config(second,16,Vec2(1,0))).step(free,second,
        std::vector<motion::StaticCollider>(),std::vector<FlipperMotion>(1,ending),search());
    check(!split.moving_work_exhausted&&split.contacts.empty()&&split.moving_iterations>=2,
          "kinematic phase end not split or remained active");
    close(split.simulated_seconds,1,"phase-end time accounting");close(free.position.x,20.5,"phase split free flight");
    close(free.velocity.x,1,"phase split acceleration");
    const Tick high=Tick(4294967296LL),maximum=std::numeric_limits<Tick>::max();
    TimedFlipper wide(config(false,high,high));wide.set_pressed(true,0);wide.update(high/2);
    close(wide.pose(),.5,"positive high-word/zero-low-word duration rejected");
    wide.update(high);close(wide.pose(),1,"wide duration did not saturate");
    wide.update(Tick(6442450944LL));wide.reset(maximum-10);wide.set_pressed(true,maximum-10);wide.update(maximum);
    check(wide.pose()>0,"near-maximum Tick subtraction overflowed");
    throws<std::invalid_argument>([&]{wide.update(maximum-1);},"backward clock accepted");
    throws<std::invalid_argument>([&]{wide.cancel(Tick(-4294967296LL));},"negative high-word clock accepted");
    check(wide.time()==maximum,"invalid clock changed state");
    TimedFlipper maximum_duration(config(false,maximum,maximum));
    maximum_duration.set_pressed(true,0);maximum_duration.update(maximum);
    close(maximum_duration.pose(),1,"INT64_MAX raise duration overflowed");
}
void reference_policy_and_yield(){
    const FlipperConfig reference(Vec2(),Vec2(2,0),Vec2(0,2),.2f,.2f,second,2*second,
        .75f,.5f,.9f,flipper::ResponseMode::ReferenceMaterial);
    const FlipperMotion raising(reference,0,1);
    check(raising.reference_response_enabled(),"reference policy opt-in missing");
    pinball::response::CollisionResult result=raising.respond(Vec2(),Vec2(0,1),Vec2(1,.2f),.1f,0);
    close(result.velocity.x,0,"reference front stationary tangent");
    close(result.velocity.y,.6417232871,"reference front bounded stationary boost");
    check(result.boost_applied,"reference leading face did not boost");
    result=raising.respond(Vec2(0,-10),Vec2(0,1),Vec2(1,.2f),.1f,0);
    close(result.velocity.y,8.1417232871,"reference boost was not additive after material response");
    result=raising.respond(Vec2(),Vec2(0,1),Vec2(0,.2f),.1f,0);
    check(result.velocity.x==0&&result.velocity.y==0&&!result.boost_applied,"reference base zone added power");
    result=raising.respond(Vec2(0,10),Vec2(0,-1),Vec2(1,-.2f),.1f,0);
    close(result.velocity.y,-4.0955522254,"reference trailing face attenuation");
    check(!result.boost_applied,"reference trailing face boosted");
    result=raising.respond(Vec2(0,10),Vec2(0,-1),Vec2(0,-.2f),.1f,0);
    close(result.velocity.y,-7.5,"base zone incorrectly reduced elasticity");
    const FlipperMotion returning(reference,1,-.5);
    result=returning.respond(Vec2(),Vec2(1,0),Vec2(.2f,1),.1f,0);
    close(result.velocity.x,.3208616436,"return leading face bounded boost");
    result=FlipperMotion(reference,1,0).respond(Vec2(-10,0),Vec2(1,0),Vec2(.2f,1),.1f,0);
    close(result.velocity.x,7.5,"held reference passive material");check(!result.boost_applied,"held reference shape added power");
    const FlipperConfig generic(Vec2(),Vec2(2,0),Vec2(0,2),.2f,.2f,second,2*second,.75f);
    result=FlipperMotion(generic,0,1).respond(Vec2(),Vec2(0,1),Vec2(1,.2f),.1f,0);
    close(result.velocity.y,2.7488935719,"default generic physical surface policy changed");
    const FlipperMotion frozen=raising.frozen_at(.25);
    close(frozen.pose(0).normalized,.25,"frozen snapshot pose");
    close(frozen.pose(100).normalized,.25,"frozen snapshot kept rotating");
    check(frozen.duration()==0&&frozen.speed_bound()==0&&frozen.pose().angular_velocity==0,
          "frozen snapshot retained kinematics");
    result=frozen.respond(Vec2(),Vec2(0,1),Vec2(1,.2f),.1f,100);
    check(!result.boost_applied&&result.velocity.y==0,"frozen reference snapshot repeated power");
    motion::BallState once(Vec2(1,.4f),Vec2(),.1f);
    const motion::MotionStep yielding=motion::BallStepper(motion_config()).step(once,second,
        std::vector<motion::StaticCollider>(),std::vector<FlipperMotion>(1,raising),search());
    check(!yielding.contact_budget_exhausted&&!yielding.moving_work_exhausted,
          "reference yield exhausted work");
    check(yielding.contacts.size()==1&&yielding.contacts[0].boost_applied&&yielding.flipper_yields.size()==1,
          "reference kinematic face repeated a powered hit instead of yielding");
    close(yielding.flipper_yields[0].seconds_from_start,yielding.contacts[0].seconds_from_start,
          "reference yield time differs from first contact");
    check(physics::length(once.velocity)<std::fabs(physics::dot(yielding.contacts[0].surface_velocity,
        yielding.contacts[0].hit.normal)),"yield fixture did not test boost below surface speed");
    motion::BallState generic_ball(Vec2(1,.4f),Vec2(),.1f);
    const motion::MotionStep physical=motion::BallStepper(motion_config()).step(generic_ball,second,
        std::vector<motion::StaticCollider>(),std::vector<FlipperMotion>(1,FlipperMotion(generic,0,1)),search());
    check(physical.flipper_yields.empty(),"generic physical mode unexpectedly yielded");
    TimedFlipper driver(reference);driver.set_pressed(true,0);
    driver.commit_yield(200000000,500000000);
    close(driver.pose(),.2,"yield committed unvalidated remaining stroke");
    check(driver.held()&&driver.time()==500000000,"yield lost held state or end clock");
    close(driver.motion().duration(),.8,"yield did not extend phase deadline");
    driver.update(600000000);close(driver.pose(),.3,"yield changed nominal raise rate");
    driver.update(1300000000);close(driver.pose(),1,"yielded raise did not finish");
    driver.set_pressed(false,1300000000);driver.commit_yield(1500000000,1800000000);
    close(driver.pose(),.9,"return yield committed full interval");check(!driver.held(),"return yield synthesized press");
    driver.update(1900000000);close(driver.pose(),.85,"return yield changed nominal rate");
    throws<std::invalid_argument>([&]{driver.commit_yield(1800000000,2000000000);},"backward yield accepted");
    check(driver.time()==1900000000,"invalid yield changed clock");
    throws<std::invalid_argument>([&]{driver.commit_yield(2000000000,1900000000);},"reversed yield interval accepted");
    throws<std::invalid_argument>([&]{raising.respond(Vec2(),Vec2(),Vec2(),.1f,0);},"zero response normal accepted");
    throws<std::invalid_argument>([&]{raising.respond(Vec2(),Vec2(0,1),Vec2(),-1,0);},"negative response ball radius accepted");
    FlipperConfig bad=reference;bad.impulse_gain=-1;
    throws<std::invalid_argument>([&]{FlipperMotion invalid(bad,0,1);},"negative reference impulse gain accepted");
    bad=reference;bad.response_mode=static_cast<flipper::ResponseMode>(99);
    throws<std::invalid_argument>([&]{FlipperMotion invalid(bad,0,1);},"unknown response mode accepted");
}
void invalid_and_overflow(){
    const float nan=std::numeric_limits<float>::quiet_NaN(),infinity=std::numeric_limits<float>::infinity();
    throws<std::invalid_argument>([]{config(false,0,second);},"zero raise duration accepted");
    throws<std::invalid_argument>([]{config(false,second,-1);},"negative return duration accepted");
    throws<std::invalid_argument>([]{FlipperConfig(Vec2(),Vec2(),Vec2(1,0),1,1,second,second,1);},"zero tip vector accepted");
    throws<std::invalid_argument>([]{FlipperConfig(Vec2(),Vec2(1,0),Vec2(0,1),-1,1,second,second,1);},"negative radius accepted");
    throws<std::invalid_argument>([&]{FlipperConfig(Vec2(nan,0),Vec2(1,0),Vec2(0,1),1,1,second,second,1);},"nonfinite pivot accepted");
    throws<std::invalid_argument>([]{FlipperConfig(Vec2(),Vec2(1,0),Vec2(0,1),1,1,second,second,1.1f);},"energy-adding material accepted");
    throws<std::invalid_argument>([]{FlipperMotion(config(),-1,0);},"out-of-range normalized pose accepted");
    throws<std::invalid_argument>([&]{FlipperMotion(config(),0,infinity);},"nonfinite normalized velocity accepted");
    throws<std::invalid_argument>([]{TimedFlipper(config(),-1);},"negative initial clock accepted");
    throws<std::invalid_argument>([]{MovingSweepConfig(0,1,1e-5,1e-8);},"zero query budget accepted");
    throws<std::invalid_argument>([]{MovingSweepConfig(1,0,1e-5,1e-8);},"zero total budget accepted");
    throws<std::invalid_argument>([]{MovingSweepConfig(1,1,0,1e-8);},"zero distance tolerance accepted");
    throws<std::invalid_argument>([&]{MovingSweepConfig(1,1,1e-5,nan);},"nonfinite time tolerance accepted");
    const FlipperMotion body(config(),0,0);
    const double wide_max=std::numeric_limits<double>::max();
    check(std::isfinite(body.surface(wide_max,0,0).separation),"large finite query overflowed intermediate square");
    throws<std::overflow_error>([&]{body.surface(wide_max,wide_max,0);},"unrepresentable double distance accepted");
    throws<std::invalid_argument>([&]{body.pose(-1);},"negative query seconds accepted");
    throws<std::invalid_argument>([&]{flipper::sweep_circle(Vec2(),Vec2(),-1,body,0,1,search(),1);},"negative ball radius accepted");
    FlipperConfig mutated=config();mutated.tip_radius=nan;
    throws<std::invalid_argument>([&]{TimedFlipper invalid(mutated);},"mutated invalid config accepted");
    const float maximum=std::numeric_limits<float>::max();
    throws<std::overflow_error>([&]{FlipperConfig(Vec2(),Vec2(maximum,0),Vec2(0,maximum),1,1,1,1,1);},
                                "unrepresentable surface speed accepted");
    const FlipperConfig overflowing(Vec2(maximum,0),Vec2(maximum,0),Vec2(maximum,0),1,1,second,second,1);
    throws<std::overflow_error>([&]{FlipperMotion(overflowing,0,0).pose();},"unrepresentable tip wrapped");
    motion::BallState ball(Vec2(2,2),Vec2(0,-1),.2f);
    MovingSweepConfig broken=search();broken.max_total_iterations=0;
    throws<std::invalid_argument>([&]{motion::BallStepper(motion_config()).step(ball,second,
        std::vector<motion::StaticCollider>(),std::vector<FlipperMotion>(1,body),broken);},"mutated sweep config accepted");
    check(ball.position.x==2&&ball.velocity.y==-1,"invalid moving step partially committed");
}
}
int main(){
    try{
        geometry_and_timing();rest_raised_and_overlap();moving_strike_mirror_and_fast_crossing();
        earliest_order_and_budgets();cancellation_phase_end_and_time();reference_policy_and_yield();invalid_and_overflow();
        std::cout<<"moving flipper checks passed; geometry interpolation/timing/contact tolerances are explicit development policies\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
