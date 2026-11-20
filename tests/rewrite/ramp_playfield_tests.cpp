#include "playfield_test.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace pinball;
const std::uint32_t tick_ns=8333333U;
const runtime::Tick tick=8333333;

void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
void close(double actual,double expected,double tolerance,const char* message) {
    check(std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance,message);
}
void same_clock(runtime::Tick actual,runtime::Tick expected,const char* message) {
    // Compare explicit words: native record-form 64-bit zero/sign tests have
    // previously skipped long test flights on Canary.
    volatile std::uint32_t ah=static_cast<std::uint32_t>(static_cast<std::uint64_t>(actual)>>32);
    volatile std::uint32_t al=static_cast<std::uint32_t>(actual);
    volatile std::uint32_t eh=static_cast<std::uint32_t>(static_cast<std::uint64_t>(expected)>>32);
    volatile std::uint32_t el=static_cast<std::uint32_t>(expected);
    check(ah==eh&&al==el,message);
}
void advance_ns(development::PlayfieldTest& game,std::uint32_t duration) {
    const runtime::Tick begin=game.snapshot().time;
    const std::uint32_t frames=duration/tick_ns,remainder=duration%tick_ns;
    for(std::uint32_t i=0;i<frames;++i)game.advance(tick);
    if(remainder!=0)game.advance(remainder);
    same_clock(game.snapshot().time,begin+duration,"bounded interval lost simulation time");
}
void no_caps(const development::MotionSnapshot& s) {
    check(s.exhausted_steps==0&&s.moving_exhausted==0,"ramp replay exhausted collision work");
}
struct Edge {
    physics::Vec2 midpoint,outward;
    Edge(physics::Vec2 a,physics::Vec2 b) {
        midpoint=physics::multiply(physics::add(a,b),.5f);
        const physics::Vec2 d=physics::subtract(b,a);
        outward=physics::unit(physics::Vec2(d.y,-d.x));
    }
};
motion::BallState approach(const reference::TablePhysics& p,const Edge& edge,float distance,float speed) {
    return motion::BallState(physics::add(edge.midpoint,physics::multiply(edge.outward,distance)),
        physics::multiply(edge.outward,-speed),p.ball_radius);
}
motion::BallState on_plane(const reference::TablePhysics& p,
    const reference::RampPlaneProfile& plane,physics::Vec2 position,physics::Vec2 velocity) {
    motion::BallState state(position,velocity,p.ball_radius);
    state.collision_mask=2;state.plane=plane.surface;
    state.height=static_cast<float>(double(p.ball_radius)+double(state.plane.gradient.x)*position.x+
        double(state.plane.gradient.y)*position.y+state.plane.offset);
    return state;
}
void image(development::PlayfieldTest& game,scene::ResourceScene& board,
           const std::string& directory,const char* filename) {
    runtime::InputState input;game.render(board,input);
    if(!directory.empty())scene::write_bmp(directory+"/"+filename,board.surface);
}

void entry_and_backout(const table::TableModel& model,const reference::TablePhysics& p,
    scene::ResourceScene& board,const std::string& output) {
    const reference::RampPlaneProfile& plane=p.ramp_planes.at(17);
    check(plane.group==101&&plane.local_index==17,"actual ramp entrance provenance");
    const Edge mouth(plane.vertices[0],plane.vertices[1]);
    development::PlayfieldTest entered(model,approach(p,mouth,.15f,20));
    entered.advance(tick);
    const development::MotionSnapshot first=entered.snapshot();
    same_clock(first.time,tick,"first ramp frame lost time");
    check(first.ball.collision_mask==2&&first.ball.plane.active&&first.ramp_transitions>=1,
          "center crossing did not enter ramp mask before ground wall102");
    check(first.contacts==first.ramp_transitions,"solid wall bounced ball before ramp entry");
    check(physics::dot(first.ball.velocity,mouth.outward)<-10,"entry reversed the ascending velocity");
    for(unsigned i=0;i<5;++i)entered.advance(tick);
    const development::MotionSnapshot rising=entered.snapshot();
    same_clock(rising.time,tick*6,"ascending replay skipped frames");
    check(rising.ball.collision_mask==2&&rising.ball.plane.active&&rising.ball.height>p.ball_radius+.2f,
          "entered ball failed to climb the constrained surface");
    no_caps(rising);image(entered,board,output,"ramp-entered.bmp");

    development::PlayfieldTest weak(model,approach(p,mouth,.15f,.1f));
    for(unsigned i=0;i<24;++i)weak.advance(tick);
    const development::MotionSnapshot outside=weak.snapshot();
    same_clock(outside.time,tick*24,"weak approach lost time");
    check(outside.ball.collision_mask==1&&!outside.ball.plane.active&&outside.ramp_transitions==0,
          "shot reversing before the mouth entered the ramp");
    check(physics::dot(physics::subtract(outside.ball.position,mouth.midpoint),mouth.outward)>.15f,
          "weak pre-mouth shot did not back away");
    close(outside.ball.height,p.ball_radius,.00001,"ground shot changed height");no_caps(outside);

    const physics::Vec2 inside=physics::subtract(mouth.midpoint,physics::multiply(mouth.outward,.03f));
    development::PlayfieldTest backout(model,on_plane(p,plane,inside,physics::multiply(mouth.outward,.5f)));
    bool exited=false;unsigned frames=0;float previous_height=backout.snapshot().ball.height;
    for(;frames<60;++frames) {
        backout.advance(tick);const development::MotionSnapshot s=backout.snapshot();
        check(std::fabs(s.ball.height-previous_height)<.05f,"weak backout jumped height");
        previous_height=s.ball.height;
        if(s.ball.collision_mask==1){exited=true;++frames;break;}
    }
    const development::MotionSnapshot ground=backout.snapshot();
    same_clock(ground.time,tick*frames,"weak backout skipped frames");
    check(exited&&!ground.ball.plane.active&&ground.ramp_transitions==1,
          "weak inside shot did not return through the ground connector");
    check(ground.contacts==ground.ramp_transitions&&physics::dot(ground.ball.velocity,mouth.outward)>0,
          "backout bounced instead of crossing the connector");
    close(ground.ball.height,p.ball_radius,.0001,"ground connector height continuity");no_caps(ground);
    image(backout,board,output,"ramp-backed-out.bmp");
}

void full_ramp_cycle(const table::TableModel& model,const reference::TablePhysics& p,
    scene::ResourceScene& board,const std::string& output) {
    const reference::RampPlaneProfile& entrance=p.ramp_planes.at(17);
    const Edge mouth(entrance.vertices[0],entrance.vertices[1]);
    development::PlayfieldTest game(model,approach(p,mouth,.15f,40));
    bool saw_ramp=false,saw_raised=false,saw_hole=false,landed=false;
    float raised_height=0,max_height=p.ball_radius;
    unsigned frames=0;
    for(;frames<720;++frames) {
        game.advance(tick);const development::MotionSnapshot s=game.snapshot();no_caps(s);
        if(s.ball.height>max_height)max_height=s.ball.height;
        if(s.ball.collision_mask==2&&s.ball.plane.active)saw_ramp=true;
        if(s.ball.collision_mask==4&&!saw_raised) {
            saw_raised=true;raised_height=s.ball.height;
            check(!s.ball.plane.active,"upper exit retained height-plane ownership");
            close(raised_height,1.360704,.01,"upper exit preserved wrong ramp height");
            check(s.ramp_transitions>=18,"full climb skipped the ramp plane chain");
            image(game,board,output,"ramp-upper-exit.bmp");
        }
        if(s.captured_hole&&!saw_hole) {
            saw_hole=true;
            check(saw_raised&&s.hole_captures==1&&s.ball.collision_mask==4,
                  "raised hole captured without the ramp exit");
            close(s.ball.height,raised_height,.001,"raised run changed unconstrained exit height");
            image(game,board,output,"ramp-hole-arrival.bmp");
        }
        if(s.hole_landings!=0) {
            landed=true;++frames;
            check(!s.captured&&s.ball.collision_mask==1&&!s.ball.plane.active,
                  "natural ramp-hole landing did not restore ground ownership");
            close(s.ball.height,p.holes.at(0).landing_z,.00001,"natural hole landing Z");
            check(physics::length(s.ball.velocity)<.11f,"hole landing retained pre-capture XY velocity");
            break;
        }
    }
    const development::MotionSnapshot final=game.snapshot();
    same_clock(final.time,tick*frames,"full ramp flight skipped simulation time");
    check(saw_ramp&&saw_raised&&saw_hole&&landed,"strong shot did not complete the actual curved ramp/hole route");
    check(max_height>1.3f&&max_height<1.4f&&final.hole_captures==1&&final.hole_landings==1,
          "natural ramp route height or one-shot landing count");
    std::cout<<"Actual ramp40 route frames "<<frames<<", transitions "<<final.ramp_transitions
             <<", contacts "<<final.contacts<<", topZ "<<raised_height<<'\n';
}

void upper_exit(const table::TableModel& model,const reference::TablePhysics& p) {
    const reference::RampPlaneProfile& plane=p.ramp_planes.at(1);
    check(plane.group==101&&plane.local_index==1,"actual upper exit provenance");
    const Edge edge(plane.vertices[1],plane.vertices[2]);
    const physics::Vec2 inside=physics::subtract(edge.midpoint,physics::multiply(edge.outward,.03f));
    development::PlayfieldTest game(model,on_plane(p,plane,inside,physics::multiply(edge.outward,5)));
    bool exited=false;unsigned frames=0;
    for(;frames<24;++frames){game.advance(tick);if(game.snapshot().ball.collision_mask==4){exited=true;++frames;break;}}
    const development::MotionSnapshot s=game.snapshot();
    same_clock(s.time,tick*frames,"seeded upper exit lost time");
    check(exited&&!s.ball.plane.active&&s.ramp_transitions==1,"upper2-to4 connector failed");
    close(s.ball.height,1.360704,.0002,"seeded upper exit failed to preserve height");
    check(physics::dot(s.ball.velocity,edge.outward)>0,"upper exit reflected velocity");
    check(s.contacts==s.ramp_transitions,"upper connector emitted a solid bounce");no_caps(s);
    const float retained=s.ball.height;game.advance(tick);
    close(game.snapshot().ball.height,retained,0,"unconstrained raised path changed preserved Z");
}

void raised_hole_clock(const table::TableModel& model,const reference::TablePhysics& p,
    scene::ResourceScene& board,const std::string& output) {
    const reference::HoleProfile& hole=p.holes.at(0);
    motion::BallState moving(physics::add(hole.center,physics::Vec2(.12f,0)),physics::Vec2(-2,0),p.ball_radius);
    moving.collision_mask=4;moving.height=1.360704f;
    development::PlayfieldTest approach_game(model,moving);
    bool captured=false;unsigned frames=0;
    for(;frames<120;++frames){approach_game.advance(tick);if(approach_game.snapshot().captured_hole){captured=true;++frames;break;}}
    const development::MotionSnapshot arrival=approach_game.snapshot();
    same_clock(arrival.time,tick*frames,"raised-hole approach lost time");
    check(captured&&arrival.hole_captures==1&&arrival.ball.collision_mask==4&&!arrival.ball.plane.active,
          "mask4 center approach missed ramp hole104");
    close(arrival.ball.position.x,hole.center.x,0,"hole capture center X");
    close(arrival.ball.position.y,hole.center.y,0,"hole capture center Y");
    close(physics::length(arrival.ball.velocity),0,0,"hole capture retained XY speed");
    image(approach_game,board,output,"ramp-hole-captured.bmp");
    const std::vector<std::uint8_t> with_ball=board.surface.pixels;board.render();
    check(with_ball!=board.surface.pixels,"captured raised-hole ball was hidden");

    // A separate center start fixes the capture epoch at time0 so the exact
    // half-second boundary and seven legacy fall ticks have a numeric oracle.
    motion::BallState center(hole.center,physics::Vec2(),p.ball_radius);
    center.collision_mask=4;center.height=1.360704f;
    development::PlayfieldTest clock(model,center);clock.advance(1);
    check(clock.snapshot().captured_hole,"center-start hole did not capture");
    advance_ns(clock,399999999U);
    check(clock.snapshot().captured_hole&&clock.snapshot().hole_landings==0,"hole landed during wait");
    close(clock.snapshot().ball.height,center.height,0,"hole wait altered visible Z");
    clock.set_suspended(true);const runtime::Tick paused=clock.snapshot().time;clock.advance(1000000000);
    same_clock(clock.snapshot().time,paused,"pause advanced raised-hole ownership clock");
    close(clock.snapshot().ball.height,center.height,0,"pause advanced hole fall");
    clock.set_suspended(false);advance_ns(clock,100000000U);
    same_clock(clock.snapshot().time,500000000,"hole half-second boundary clock");
    check(clock.snapshot().captured_hole&&clock.snapshot().ball.collision_mask==4,
          "hole released at wait end before first fall tick");
    close(clock.snapshot().ball.height,center.height,0,"half-second hold height");
    for(unsigned i=0;i<6;++i)clock.advance(tick);
    check(clock.snapshot().captured_hole&&clock.snapshot().ball.height<center.height&&
          clock.snapshot().ball.height>hole.landing_z,"legacy fall did not descend before landing");
    clock.advance(tick);
    const development::MotionSnapshot landed=clock.snapshot();
    same_clock(landed.time,500000000+tick*7,"exact landing clock");
    check(!landed.captured&&landed.hole_captures==1&&landed.hole_landings==1&&
          landed.ball.collision_mask==1&&!landed.ball.plane.active,"legacy fall landing state");
    close(landed.ball.height,hole.landing_z,0,"literal408 landing Z");
    close(physics::length(landed.ball.velocity),0,0,"exact landing did not reset XY velocity");no_caps(landed);
    image(clock,board,output,"ramp-hole-landed.bmp");
}
}

int main(int argc,char** argv) {
    try {
        const table::TableModel model=table::TableModel::load(argc>1?argv[1]:"game:\\PINBALL.DAT");
        const reference::TablePhysics profile(model);
        check(profile.ramp_planes.size()==20&&profile.holes.size()==1,"actual ramp resources absent");
        scene::SceneLayout layout;layout.width=600;layout.height=416;
        scene::ResourceScene board=scene::static_resource_scene(model,layout);
        const std::string output=argc>2?argv[2]:"";
        entry_and_backout(model,profile,board,output);
        full_ramp_cycle(model,profile,board,output);
        upper_exit(model,profile);
        raised_hole_clock(model,profile,board,output);
        std::cout<<"ramp_playfield_tests PASS; DAT/source-guided fixed-clock scenarios, XP runtime parity unverified\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"ramp_playfield_tests FAIL: "<<error.what()<<'\n';return 1;
    }
}