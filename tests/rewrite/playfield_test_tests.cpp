#include "playfield_test.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace pinball;
namespace {
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void near(float a,float b,float t,const char* m){check(std::fabs(a-b)<=t,m);}
runtime::InputTransition edge(runtime::Action a,bool down,runtime::TransitionCause c=runtime::TransitionCause::Physical){
    const runtime::InputTransition e={1,1,a,down,c};return e;
}
void run_for(development::PlayfieldTest& game,runtime::Tick duration){
    const runtime::Tick begin=game.snapshot().time,requested=duration;
    for(;;){
        volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(duration)>>32);
        volatile std::uint32_t low=static_cast<std::uint32_t>(duration);
        if(high==0&&low==0)break;
        const runtime::Tick part=(high!=0||low>8333333U)?8333333:low;
        game.advance(part);duration-=part;
    }
    check(game.snapshot().time-begin==requested,"long interval skipped simulation time");
}
void settle(development::PlayfieldTest& game){
    for(unsigned i=0;i<600&&!game.snapshot().seated;++i)game.advance(8333333);
    if(!game.snapshot().seated){
        const development::MotionSnapshot s=game.snapshot();
        std::cerr<<"feed p "<<s.ball.position.x<<','<<s.ball.position.y<<" v "<<s.ball.velocity.x<<','<<s.ball.velocity.y
            <<" contacts "<<s.contacts<<" caps "<<s.exhausted_steps<<'\n';
        throw std::runtime_error("feed did not settle at striker");
    }
}
std::uint64_t hash(const scene::Surface& s){
    std::uint64_t v=14695981039346656037ULL;
    for(std::size_t i=0;i<s.pixels.size();++i){v^=s.pixels[i];v*=1099511628211ULL;}return v;
}
void save(const std::string& out,const char* name,const scene::Surface& surface){
    if(!out.empty())scene::write_bmp(out+"/"+name,surface);
}
void capture_regressions(const table::TableModel& model,scene::ResourceScene& scene,
                         const runtime::InputState& input,const std::string& out) {
    reference::TablePhysics profile(model);
    const motion::StaticCollider& gate=profile.static_colliders[profile.sinks[1].collider_index];
    const physics::Vec2 middle=physics::multiply(physics::add(gate.first,gate.second),.5f);
    const physics::Vec2 edge_vector=physics::subtract(gate.second,gate.first);
    const physics::Vec2 front=physics::unit(physics::Vec2(-edge_vector.y,edge_vector.x));
    development::PlayfieldTest worm(model,physics::add(middle,physics::multiply(front,profile.ball_radius+.08f)),
                                   physics::multiply(front,-2));
    for(unsigned i=0;i<120 && !worm.snapshot().captured;++i)worm.advance(8333333);
    check(worm.snapshot().captured && !worm.snapshot().captured_kickout && worm.snapshot().sink_captures==1,"directed wormhole did not capture");
    check(worm.snapshot().score==2500,"wormhole did not score once at capture");
    check(physics::length(worm.snapshot().ball.velocity)==0,"captured ball kept moving");
    worm.render(scene,input);save(out,"reference-captured.bmp",scene.surface);
    const scene::Surface hidden=scene.surface;scene.render();
    check(hash(scene.surface)==hash(hidden),"wormhole ball was still drawn");
    const runtime::Tick captured_time=worm.snapshot().time;
    worm.set_suspended(true);worm.advance(1000000000);
    check(worm.snapshot().time==captured_time && worm.snapshot().captured,"pause advanced wormhole timer");
    worm.set_suspended(false);run_for(worm,1500000000);
    check(worm.snapshot().captured && worm.snapshot().sink_ejections==0,"wormhole ejected early");
    run_for(worm,500000000);
    check(!worm.snapshot().captured && worm.snapshot().sink_ejections==1,"two-second wormhole did not eject");
    check(worm.snapshot().score==2500,"wormhole ejection scored a second time");
    check(physics::length(worm.snapshot().ball.velocity)>45 && physics::length(worm.snapshot().ball.velocity)<53,"wormhole raw direction/speed lost");
    worm.render(scene,input);save(out,"reference-ejected.bmp",scene.surface);

    // Exact stationary position reproduced in0.0.6 below the purple ramp.
    const reference::KickoutProfile& kick=profile.kickouts[0];
    development::PlayfieldTest pocket(model,physics::Vec2(3.6124716f,-.22356f),physics::Vec2());
    const float initial_distance=physics::length(physics::subtract(kick.center,pocket.snapshot().ball.position));
    run_for(pocket,41666665);
    check(pocket.snapshot().captured_kickout || physics::length(physics::subtract(kick.center,pocket.snapshot().ball.position))<initial_distance,"black-hole field did not pull out of the old wedge");
    for(unsigned i=0;i<600 && !pocket.snapshot().captured;++i)pocket.advance(8333333);
    if(!pocket.snapshot().captured_kickout) {
        const development::MotionSnapshot s=pocket.snapshot();
        std::cerr<<"pocket p "<<s.ball.position.x<<','<<s.ball.position.y<<" v "<<s.ball.velocity.x<<','<<s.ball.velocity.y<<" contacts "<<s.contacts<<'\n';
    }
    check(pocket.snapshot().captured_kickout && pocket.snapshot().kickout_captures==1,"purple-ramp pocket did not capture at the black hole");
    check(pocket.snapshot().score==20000,"black-hole capture award");
    near(pocket.snapshot().ball.position.x,kick.center.x,0,"black-hole hold X");
    near(pocket.snapshot().ball.position.y,kick.center.y,0,"black-hole hold Y");
    pocket.render(scene,input);save(out,"black-hole-captured.bmp",scene.surface);
    run_for(pocket,1000000000);
    check(pocket.snapshot().captured_kickout && pocket.snapshot().kickout_ejections==0,"black hole ejected before1.5s");
    const runtime::Tick paused=pocket.snapshot().time;
    pocket.set_suspended(true);pocket.advance(1000000000);
    check(pocket.snapshot().time==paused && pocket.snapshot().captured_kickout,"pause advanced black-hole hold");
    pocket.set_suspended(false);run_for(pocket,500000000);
    const development::MotionSnapshot ejected=pocket.snapshot();
    check(!ejected.captured && ejected.kickout_ejections==1,"black hole did not kick out after1.5s");
    check(ejected.ball.velocity.x<0 && ejected.ball.velocity.y<0,"black-hole throw direction wrong");
    check(physics::length(ejected.ball.velocity)>45 && physics::length(ejected.ball.velocity)<53,"black-hole diagonal throw speed wrong");
    check(ejected.kickout_cooldown>0 && ejected.kickout_cooldown<=50000000,"black-hole cooldown not started");
    pocket.render(scene,input);save(out,"black-hole-ejected.bmp",scene.surface);
    run_for(pocket,ejected.kickout_cooldown);
    check(pocket.snapshot().kickout_cooldown==0 && pocket.snapshot().kickout_captures==1,"cooldown failed to prevent immediate recapture");
    check(pocket.snapshot().exhausted_steps==0 && pocket.snapshot().moving_exhausted==0,"pocket recovery exhausted work");
    pocket.reset();
    check(pocket.snapshot().kickout_captures==0 && pocket.snapshot().kickout_cooldown==0 && !pocket.snapshot().captured,"reset retained black-hole state");
}
void test_toggle(development::PlayfieldTest& game) {
    game.input(edge(runtime::Action::HiddenTest,true));
    game.input(edge(runtime::Action::HiddenTest,false));
}
void test_aim(development::PlayfieldTest& game,const visual::Camera& camera,physics::Vec2 target) {
    const motion::BallState ball=game.snapshot().ball;
    const visual::ProjectedPoint from=visual::project(camera,ball.position.x,ball.position.y,ball.height);
    const visual::ProjectedPoint to=visual::project(camera,target.x,target.y,ball.height);
    const double dx=to.x-from.x,dy=to.y-from.y,length=std::sqrt(dx*dx+dy*dy);
    game.set_test_stick(length?static_cast<float>(dx/length):0,length?static_cast<float>(-dy/length):0);
}
void hidden_test_controls(const table::TableModel& model,scene::ResourceScene& scene,
                          const runtime::InputState& input,const std::string& out) {
    const reference::TablePhysics profile(model);
    const visual::Camera camera=visual::camera_from_table(model);
    development::PlayfieldTest hover(model);
    check(!hover.snapshot().hidden_test,"hidden mode starts enabled");
    hover.input(edge(runtime::Action::HiddenTest,true,runtime::TransitionCause::FocusLost));
    check(!hover.snapshot().hidden_test,"nonphysical press toggled hidden mode");
    hover.input(edge(runtime::Action::Plunger,true));
    hover.input(edge(runtime::Action::NudgeLeft,true));
    test_toggle(hover);
    check(hover.snapshot().hidden_test&&hover.snapshot().charge==0&&hover.snapshot().shake_x==0,
          "hidden entry retained plunger charge or nudge effect");
    const motion::BallState still=hover.snapshot().ball;
    run_for(hover,200000000);
    near(hover.snapshot().ball.position.x,still.position.x,0,"neutral hidden gravity X drift");
    near(hover.snapshot().ball.position.y,still.position.y,0,"neutral hidden gravity Y drift");
    check(physics::length(hover.snapshot().ball.velocity)==0&&!hover.snapshot().seated,
          "neutral hidden ball velocity/seat");
    hover.set_test_stick(0,1);
    run_for(hover,100000000);
    const visual::ProjectedPoint before=visual::project(camera,still.position.x,still.position.y,still.height);
    const development::MotionSnapshot steered=hover.snapshot();
    const visual::ProjectedPoint after=visual::project(camera,steered.ball.position.x,steered.ball.position.y,steered.ball.height);
    check(after.y<before.y-5,"stick up did not move visually upward");
    hover.set_test_stick(0,0);
    const physics::Vec2 neutral=hover.snapshot().ball.position;
    run_for(hover,100000000);
    near(hover.snapshot().ball.position.x,neutral.x,0,"neutral release kept X motion");
    near(hover.snapshot().ball.position.y,neutral.y,0,"neutral release kept Y motion");

    hover.set_test_stick(0,1);hover.set_suspended(true);
    const runtime::Tick paused=hover.snapshot().time;hover.set_test_stick(1,1);hover.advance(1000000000);
    check(hover.snapshot().time==paused&&hover.snapshot().hidden_test,"pause changed hidden state or clock");
    hover.set_suspended(false);run_for(hover,50000000);
    near(hover.snapshot().ball.position.x,neutral.x,0,"resume retained paused stick X");
    near(hover.snapshot().ball.position.y,neutral.y,0,"resume retained paused stick Y");
    hover.set_suspended(true);test_toggle(hover);
    check(!hover.snapshot().hidden_test,"Back could not disable hidden mode while paused");
    hover.set_suspended(false);
    check(physics::length(hover.snapshot().ball.velocity)==0,"mode off did not begin stationary");
    run_for(hover,100000000);
    check(hover.snapshot().ball.position.y>neutral.y+.01f,"mode off did not restore gravity");

    // The right visual direction heads toward the real x=-6.25 ground wall.
    // Holding a command into it must query geometry instead of teleporting.
    development::PlayfieldTest wall(model,physics::Vec2(-5.3f,6),physics::Vec2());
    test_toggle(wall);wall.set_test_stick(1,0);
    for(unsigned i=0;i<90;++i){
        wall.advance(8333333);
        check(wall.snapshot().ball.position.x>=-5.9503f,"hidden steering crossed the real wall");
    }
    check(wall.snapshot().contacts>0&&wall.snapshot().ball.position.x<-5.5f,
          "hidden stick failed to reach/collide with wall");
    wall.set_test_stick(0,0);const physics::Vec2 stopped=wall.snapshot().ball.position;run_for(wall,50000000);
    near(wall.snapshot().ball.position.x,stopped.x,0,"wall release retained X drift");
    near(wall.snapshot().ball.position.y,stopped.y,0,"wall release retained Y drift");
    check(wall.snapshot().exhausted_steps==0&&wall.snapshot().moving_exhausted==0,"hidden wall work cap");
    wall.render(scene,input);save(out,"hidden-test-wall.bmp",scene.surface);

    // Existing component ownership survives toggling. Its ordinary timer and
    // throw run, then one complete free tick clears the sensor before hovering.
    const reference::KickoutProfile& kick=profile.kickouts[0];
    development::PlayfieldTest captured(model,kick.center,physics::Vec2());
    captured.advance(1);check(captured.snapshot().captured_kickout,"hidden capture fixture failed");
    test_toggle(captured);captured.set_test_stick(1,1);
    run_for(captured,1499999999LL);
    check(captured.snapshot().hidden_test&&!captured.snapshot().captured&&
          captured.snapshot().kickout_captures==1&&captured.snapshot().kickout_ejections==1,
          "hidden toggle interrupted component capture/ejection");
    captured.set_test_stick(0,0);captured.advance(1);captured.advance(8333332);
    check(physics::length(physics::subtract(captured.snapshot().ball.position,kick.center))>kick.capture_radius,
          "fragmented hidden ejection failed to clear sensor");
    captured.advance(8333333);const physics::Vec2 released=captured.snapshot().ball.position;
    run_for(captured,100000000);
    near(captured.snapshot().ball.position.x,released.x,0,"neutral after component throw X drift");
    near(captured.snapshot().ball.position.y,released.y,0,"neutral after component throw Y drift");
    check(captured.snapshot().kickout_cooldown==0,"hidden cooldown did not expire");
    for(unsigned i=0;i<120&&captured.snapshot().kickout_captures<2;++i){
        test_aim(captured,camera,kick.center);captured.advance(8333333);
    }
    check(captured.snapshot().kickout_captures==2&&captured.snapshot().captured_kickout,
          "hidden kickout sensor did not rearm after cooldown");
    check(captured.snapshot().exhausted_steps==0&&captured.snapshot().moving_exhausted==0,"hidden component work cap");
    captured.reset();check(!captured.snapshot().hidden_test&&!captured.snapshot().captured,"new game retained hidden mode");

    bool rejected=false;try{wall.set_test_stick(1.01f,0);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"unnormalized hidden stick accepted");
    rejected=false;try{wall.set_test_stick((std::numeric_limits<float>::quiet_NaN)(),0);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"nonfinite hidden stick accepted");
    std::cout<<"hidden-test steering/collision/capture checks passed\n";
}
struct ComponentApproach {
    std::size_t group;
    float x,y,vx,vy;
    std::uint32_t mask;
    float height;
};
// Finite actual-DAT approach fixtures independently measured against the
// complete masked profile; full evidence is component-approaches.csv.
const ComponentApproach component_approaches[]={
    {372,.6270002127f,-3.463601828f,-18.47759056f,-7.653668404f,1,.3f},
    {380,-.7075189352f,-6.618031025f,-20,0,1,.3f},
    {388,1.921058059f,-5.960477829f,-20,0,1,.3f},
    {396,6.439453125f,-10.95511913f,-20,0,1,.3f},
    {405,5.850002289f,4,-20,0,4,1.360703945f},
    {413,7.666612148f,3.718129873f,-18.47759056f,-7.653668404f,4,1.360703945f},
    {421,7.021295071f,4.907898903f,-20,0,4,1.360703945f},
    {469,3.844627142f,8.781398773f,-18.43506813f,-7.755531788f,1,.3f},
    {470,-3.844627142f,8.781398773f,18.43506813f,-7.755531788f,1,.3f},
    {471,-4.003639221f,-3.576652527f,18.03631973f,-8.642403603f,1,.3f},
    {472,4.535847187f,-2.124511003f,-9.923371315f,-17.36452484f,1,.3f}
};
motion::BallState component_start(const reference::TablePhysics& p,const ComponentApproach& a) {
    motion::BallState state(physics::Vec2(a.x,a.y),physics::Vec2(a.vx,a.vy),p.ball_radius);
    state.collision_mask=a.mask;state.height=a.height;return state;
}
const reference::ComponentProfile& component_profile(const reference::TablePhysics& p,std::size_t group) {
    for(std::size_t i=0;i<p.components.size();++i)if(p.components[i].group==group)return p.components[i];
    throw std::runtime_error("missing actual hit component");
}
motion::BallState weak_component_start(const reference::TablePhysics& p,
    const reference::ComponentProfile& component,const ComponentApproach& approach) {
    motion::BallState state=component_start(p,approach);
    physics::SweepHit earliest;earliest.time=1;
    for(std::size_t i=0;i<component.collider_indices.size();++i) {
        const motion::StaticCollider& c=p.static_colliders[component.collider_indices[i]];
        const physics::SweepHit hit=c.kind==motion::ColliderKind::Disk?
            physics::sweep_circle_circle(state.position,state.velocity,state.radius,c.first,c.radius,1):
            physics::sweep_circle_segment(state.position,state.velocity,state.radius,c.first,c.second,1,c.two_sided);
        if(hit.hit&&(!earliest.hit||hit.time<earliest.time))earliest=hit;
    }
    check(earliest.hit&&!earliest.initial_overlap,"component approach fixture is not a clean sweep");
    const physics::Vec2 direction=physics::unit(state.velocity);
    const float distance=physics::length(state.velocity)*earliest.time-.0001f;
    check(distance>0,"component approach has no exterior gap");
    state.position=physics::add(state.position,physics::multiply(direction,distance));
    state.velocity=physics::multiply(direction,.25f);return state;
}
void assert_component_sprite(development::PlayfieldTest& game,scene::ResourceScene& board,
    const runtime::InputState& input,std::size_t group) {
    game.render(board,input);bool found=false;
    const development::ComponentVisualState expected=game.component_visual(group);
    for(std::size_t i=0;i<board.instances.size();++i)if(board.instances[i].resource_group==group) {
        found=true;check(board.instances[i].visible==expected.visible&&
            board.instances[i].state_index==expected.frame,"component reaction not reflected by scene sprite");
    }
    check(found,"component reaction has no render instance");
}
void component_reactions(const table::TableModel& model,scene::ResourceScene& board,
                         const runtime::InputState& input,const std::string& output) {
    const reference::TablePhysics profile(model);const visual::Camera camera=visual::camera_from_table(model);
    check(profile.components.size()==11,"actual hit component count");
    unsigned bumpers=0,rebounds=0;
    for(unsigned fixture=0;fixture<sizeof(component_approaches)/sizeof(component_approaches[0]);++fixture) {
        const ComponentApproach& approach=component_approaches[fixture];
        const reference::ComponentProfile& component=component_profile(profile,approach.group);
        const bool bumper=component.kind==reference::HitComponentKind::Bumper;
        if(bumper)++bumpers;else ++rebounds;
        development::PlayfieldTest weak(model,weak_component_start(profile,component,approach));
        weak.advance(8333333);
        check(weak.snapshot().contacts>0&&weak.snapshot().score==0&&weak.snapshot().bumper_hits==0&&weak.snapshot().rebound_hits==0&&
            !weak.component_visual(component.group).active,"weak component contact fired a reaction");
        development::PlayfieldTest strong(model,component_start(profile,approach));
        const development::ComponentVisualState idle=strong.component_visual(component.group);
        check(!idle.active&&idle.visible==bumper&&idle.frame==0,"default component visual state");
        strong.advance(8333333);
        development::ComponentVisualState fired=strong.component_visual(component.group);
        check(fired.active&&fired.visible&&fired.frame==(bumper?1u:0u)&&fired.remaining>0,
              "eligible component hit did not fire source frame");
        check(strong.snapshot().active_flashes>0&&
              (bumper?strong.snapshot().bumper_hits>0:strong.snapshot().rebound_hits>0),
              "component hit/flash counters missing");
        check(strong.snapshot().score==profile.rules.event_group_awards.at(component.group),
              "eligible component award does not match source owner");
        assert_component_sprite(strong,board,input,component.group);
        if(fixture==0)save(output,"bumper-fired.bmp",board.surface);
        if(fixture==7)save(output,"rebound-fired.bmp",board.surface);

        // Hidden Test keeps the real collision path, making repeated contact
        // a direct check of bumper suppression rather than a synthetic event.
        test_toggle(strong);
        const std::uint32_t initial_hits=bumper?strong.snapshot().bumper_hits:strong.snapshot().rebound_hits;
        physics::Vec2 target;
        const motion::StaticCollider& first=profile.static_colliders[component.collider_indices[0]];
        if(bumper)target=first.first;
        else target=physics::add(physics::Vec2(approach.x,approach.y),
                               physics::multiply(physics::unit(physics::Vec2(approach.vx,approach.vy)),1));
        test_aim(strong,camera,target);run_for(strong,40000000);strong.set_test_stick(0,0);
        fired=strong.component_visual(component.group);
        check(fired.active&&fired.remaining>0,"component flash expired before its source deadline");
        if(bumper)check(strong.snapshot().bumper_hits==initial_hits,"active bumper retriggered before cooldown");
        else check(strong.snapshot().rebound_hits>initial_hits,"active rebound did not accept repeated eligible hits");
        const runtime::Tick remaining=fired.remaining;
        strong.set_suspended(true);const runtime::Tick paused=strong.snapshot().time;strong.advance(1000000000);
        check(strong.snapshot().time==paused&&strong.component_visual(component.group).remaining==remaining,
              "pause advanced component reaction timer");
        strong.set_suspended(false);
        run_for(strong,remaining-1);
        check(strong.component_visual(component.group).active,"component expired before exact contact-relative deadline");
        strong.advance(1);
        const development::ComponentVisualState expired=strong.component_visual(component.group);
        check(!expired.active&&expired.visible==bumper&&expired.frame==0,"component expiry failed to restore idle/hidden");
        if(!bumper)check(expired.remaining>0,"rebound retrigger replaced earlier timer instead of preserving queued expiry");
        assert_component_sprite(strong,board,input,component.group);
        // Re-hit the clean ground fixture; other owners share this timer
        // path, while steering around their surrounding walls is separate.
        if(bumper&&fixture==0) {
            test_aim(strong,camera,target);
            for(unsigned i=0;i<60&&strong.snapshot().bumper_hits==initial_hits;++i)strong.advance(8333333);
            strong.set_test_stick(0,0);
            check(strong.snapshot().bumper_hits>initial_hits&&strong.component_visual(component.group).active,
                  "bumper threshold did not rearm after cooldown");
        } else if(!bumper) {
            run_for(strong,100000000);
            check(!strong.component_visual(component.group).active&&strong.component_visual(component.group).remaining==0,
                  "queued rebound expiries did not drain");
        }
        check(strong.snapshot().exhausted_steps==0&&strong.snapshot().moving_exhausted==0,"component reaction replay exhausted work");
        strong.reset();
        check(strong.snapshot().bumper_hits==0&&strong.snapshot().rebound_hits==0&&strong.snapshot().active_flashes==0&&
              !strong.component_visual(component.group).active&&strong.component_visual(component.group).remaining==0,
              "reset retained reaction counters or timers");
        assert_component_sprite(strong,board,input,component.group);
    }
    check(bumpers==7&&rebounds==4,"component scenario coverage");
    std::cout<<"component reaction checks passed:7bumpers/4rebounds, weak/strong/retrigger/pause/reset\n";
}
motion::BallState rule_event_start(const reference::TablePhysics& p,std::size_t index) {
    const motion::StaticCollider& event=p.static_colliders[index];
    const physics::Vec2 axis=physics::subtract(event.second,event.first);
    const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
    const physics::Vec2 middle=physics::multiply(physics::add(event.first,event.second),.5f);
    motion::BallState state(physics::add(middle,physics::multiply(front,.01f)),
        physics::multiply(front,-5),p.ball_radius);
    state.collision_mask=event.collision_mask;state.height=state.collision_mask==4?1.360704f:p.ball_radius;
    return state;
}
motion::BallState actual_drain_start(const reference::TablePhysics& p) {
    const motion::StaticCollider& drain=p.static_colliders[p.rules.drain_collider_index];
    const physics::Vec2 axis=physics::subtract(drain.second,drain.first);
    const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
    const physics::Vec2 middle=physics::multiply(physics::add(drain.first,drain.second),.5f);
    return motion::BallState(physics::add(middle,physics::multiply(front,p.ball_radius-.001f)),
        physics::multiply(front,-1),p.ball_radius);
}
void finite_game_flow(const table::TableModel& model,scene::ResourceScene& board,
                      const runtime::InputState& input,const std::string& output) {
    const reference::TablePhysics p(model);
    // Plain events are not ramp plane transitions and must not grant points
    // merely from entering/exiting the purple surface.
    development::PlayfieldTest base_ramp(model,rule_event_start(p,p.rules.ramp_score_collider_index));
    base_ramp.advance(8333333);
    check(base_ramp.snapshot().score==5000&&base_ramp.snapshot().ramp_transitions==0,
          "ramp score line base award or event classification");
    development::PlayfieldTest reflex(model,rule_event_start(p,p.rules.skill_gate_collider_index));
    reflex.advance(8333333);check(reflex.snapshot().shoot_again,"actual skill gate did not arm timed save");
    reflex.set_replay_ball(rule_event_start(p,p.rules.skill_clear_collider_index));reflex.advance(8333333);
    reflex.set_replay_ball(rule_event_start(p,p.rules.ramp_score_collider_index));reflex.advance(8333333);
    check(reflex.snapshot().score==25000,"skill clear incorrectly canceled the existing reflex award window");

    // A physical initial drain is saved, hides the ball, and re-serves the
    // same ball after the independently documented drain/feed delays.
    development::PlayfieldTest saved(model,actual_drain_start(p),development::SessionMode::FiniteRound);
    saved.advance(1);
    check(saved.snapshot().saved_drains==1&&saved.snapshot().balls_remaining==3&&
          !saved.snapshot().ball_active&&saved.snapshot().score==0,"initial protected drain charged a ball/bonus");
    saved.set_suspended(true);const runtime::Tick paused=saved.snapshot().time;saved.advance(1000000000);
    check(saved.snapshot().time==paused&&!saved.snapshot().ball_active,"pause aged drain/serve flow");
    saved.set_suspended(false);run_for(saved,p.rules.drain_delay+p.rules.feed_delay-1);
    check(saved.snapshot().ball_active&&saved.snapshot().ball_number==1&&saved.snapshot().serves==2&&
          saved.snapshot().shoot_again,"saved drain failed to re-serve the same protected ball");

    development::PlayfieldTest game(model,component_start(p,component_approaches[0]),development::SessionMode::FiniteRound);
    game.advance(8333333);
    check(game.snapshot().score==500&&game.snapshot().ball_number==1&&game.snapshot().player==1,
          "finite game component award/player defaults");
    const std::uint64_t expected[]={10500,35500,60500};
    for(unsigned lost=0;lost<3;++lost) {
        game.set_replay_ball(rule_event_start(p,p.rules.skill_gate_collider_index));game.advance(8333333);
        test_toggle(game);run_for(game,10000000001LL);test_toggle(game);
        check(!game.snapshot().shoot_again,"actual gate save failed to expire after5+5s");
        game.set_replay_ball(actual_drain_start(p));const runtime::Tick drained_at=game.snapshot().time;
        game.advance(1);
        check(game.snapshot().drains==lost+1&&game.snapshot().saved_drains==0&&
              game.snapshot().balls_remaining==2-lost&&game.snapshot().score==expected[lost]&&
              !game.snapshot().ball_active,"actual unsaved drain score/count/visibility");
        game.render(board,input);const scene::Surface hidden=board.surface;board.render();
        // The finite game still draws the numeric sidebar while the ball is
        // inactive; only the table region must match a no-ball board render.
        for(unsigned y=0;y<416;++y)for(unsigned x=0;x<386;++x)
            for(unsigned channel=0;channel<4;++channel)
                check(hidden.pixels[(y*600+x)*4+channel]==board.surface.pixels[(y*600+x)*4+channel],
                      "drained/inactive ball was still rendered");
        run_for(game,p.rules.drain_delay-2);
        check(!game.snapshot().game_over&&!game.snapshot().ball_active,"drain timer ended early");
        game.advance(1);
        check(game.snapshot().time==drained_at+p.rules.drain_delay,"drain deadline clock");
        if(lost==2) {
            check(game.snapshot().game_over&&game.snapshot().ball_number==0&&!game.snapshot().serve_pending,
                  "third actual drain did not reach game over");
            run_for(game,3000000000LL);
            check(!game.snapshot().ball_active&&game.snapshot().serves==3&&game.snapshot().score==60500,
                  "game over re-fed a ball or changed final score");
            game.render(board,input);save(output,"round-game-over.bmp",board.surface);
        }else{
            check(game.snapshot().serve_pending&&!game.snapshot().ball_active,"serve did not wait after drain");
            run_for(game,p.rules.feed_delay-1);check(!game.snapshot().ball_active,"feed timer ended early");
            game.advance(1);
            check(game.snapshot().ball_active&&game.snapshot().ball_number==lost+2&&game.snapshot().shoot_again,
                  "next finite ball did not serve protected");
            near(game.snapshot().ball.position.x,p.feed_position.x,0,"served source feed X");
            near(game.snapshot().ball.position.y,p.feed_position.y,0,"served source feed Y");
        }
    }
    bool rejected=false;
    try{game.set_replay_ball(component_start(p,component_approaches[0]));}catch(const std::logic_error&){rejected=true;}
    check(rejected,"replay harness could revive a game-over ball");
    game.reset();
    check(game.snapshot().ball_active&&!game.snapshot().game_over&&game.snapshot().balls_remaining==3&&
          game.snapshot().ball_number==1&&game.snapshot().score==0&&!game.snapshot().hidden_test,
          "Y/new game failed to reset round/score/test state");
    std::cout<<"finite Session round PASS: saved drain,3charged losses,score60500,game over/new game\n";
}
motion::BallState target_approach(const reference::TablePhysics& p,unsigned target) {
    const reference::TargetProfile& owner=p.multiplier_bank.targets[target];
    motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),8333333,24,.0001f,p.field.maximum_speed));
    const float samples[3]={.25f,.5f,.75f};
    for(std::size_t i=0;i<owner.collider_indices.size();++i)for(unsigned sample=0;sample<3;++sample) {
        const motion::StaticCollider& edge=p.static_colliders[owner.collider_indices[i]];
        const physics::Vec2 axis=physics::subtract(edge.second,edge.first);
        const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
        const physics::Vec2 point=physics::add(edge.first,physics::multiply(axis,samples[sample]));
        motion::BallState state(physics::add(point,physics::multiply(front,p.ball_radius+.004f)),
            physics::multiply(front,-20),p.ball_radius);
        motion::BallState probe=state;
        const motion::MotionStep result=stepper.step(probe,8333333,p.static_colliders);
        if(!result.contacts.empty()&&!result.contacts[0].moving&&result.contacts[0].activated&&
           !result.contacts[0].hit.initial_overlap&&
           p.collider_groups[result.contacts[0].collider_index]==owner.group&&
           !result.captured&&!result.contact_budget_exhausted) {
            std::cout<<"target fixture "<<owner.group<<" p "<<state.position.x<<','<<state.position.y
                     <<" v "<<state.velocity.x<<','<<state.velocity.y<<'\n';
            return state;
        }
    }
    throw std::runtime_error("no finite front-side actual-target approach");
}
void bank_contact(development::PlayfieldTest& game,const motion::BallState& state) {
    if(game.snapshot().hidden_test)test_toggle(game);
    game.set_replay_ball(state);game.advance(8333333);
}
void bank_park(development::PlayfieldTest& game,const reference::TablePhysics& p) {
    if(!game.snapshot().hidden_test)test_toggle(game);
    game.set_replay_ball(motion::BallState(p.feed_position,physics::Vec2(),p.ball_radius));
    game.set_test_stick(0,0);
}
void assert_bank_sprites(development::PlayfieldTest& game,const reference::TablePhysics& p,
                         scene::ResourceScene& board,const runtime::InputState& input) {
    game.render(board,input);const rules::TargetBankSnapshot state=game.target_bank_snapshot();
    unsigned checked=0;
    for(std::size_t i=0;i<board.instances.size();++i) {
        const scene::SpriteInstance& instance=board.instances[i];
        for(unsigned t=0;t<3;++t)if(instance.resource_group==p.multiplier_bank.targets[t].group) {
            check(instance.visible==state.raised[t]&&instance.state_index==0,"popup target art/drop state");
            ++checked;
        }
        for(unsigned l=0;l<4;++l)if(instance.resource_group==p.multiplier_bank.lamps[l].group) {
            check(instance.visible==state.lamp_visible[l]&&instance.state_index==0,"multiplier lamp flash/state");
            ++checked;
        }
    }
    check(checked==7,"missing selected bank sprites");
}
void multiplier_bank_regressions(const table::TableModel& model,scene::ResourceScene& board,
                                 const runtime::InputState& input) {
    const reference::TablePhysics p(model);
    const motion::BallState approaches[3]={target_approach(p,0),target_approach(p,1),target_approach(p,2)};
    // Weak physical contacts still bounce, but do not drop or award a target.
    for(unsigned t=0;t<3;++t) {
        motion::BallState weak=approaches[t];weak.velocity=physics::multiply(weak.velocity,.1f);
        development::PlayfieldTest game(model,weak);
        game.advance(8333333);
        check(game.snapshot().contacts>0&&game.snapshot().target_hits==0&&game.snapshot().score==0&&
              game.target_bank_snapshot().raised[t],"weak target contact dropped/scored");
        bank_contact(game,approaches[t]);
        check(game.snapshot().target_hits==1&&game.snapshot().score==500&&!game.target_bank_snapshot().raised[t],
              "strong target contact did not drop/score once");
        bank_contact(game,approaches[t]);
        check(game.snapshot().target_hits==1&&game.snapshot().score==500,"down target body remained active");
        assert_bank_sprites(game,p,board,input);
        development::PlayfieldTest steered(model,approaches[t]);
        test_toggle(steered);
        const physics::Vec2 aim=physics::add(approaches[t].position,physics::multiply(approaches[t].velocity,.05f));
        test_aim(steered,visual::camera_from_table(model),aim);steered.advance(8333333);
        check(steered.snapshot().target_hits==1&&steered.snapshot().score==500&&
              !steered.target_bank_snapshot().raised[t],"Hidden Test steering bypassed target interaction");
        game.reset();bank_contact(game,approaches[t]);
        check(game.snapshot().target_hits==1&&game.snapshot().score==500,"reset did not restore target body");
    }
    development::PlayfieldTest bank(model,approaches[0]);
    // Use the harness only to position a live ball, never to alter score,
    // timers, target flags or multiplier. All15hits are real solver contacts.
    const std::uint32_t factors[5]={1,2,3,5,10};
    std::uint64_t expected_score=0;
    for(unsigned completion=0;completion<5;++completion) {
        for(unsigned t=0;t<3;++t) {
            bank_contact(bank,approaches[t]);
            expected_score+=(t==2?1500:500)*factors[completion];
            check(bank.snapshot().score==expected_score&&bank.snapshot().target_hits==completion*3+t+1,
                  "target points double-multiplied or used new tier");
            assert_bank_sprites(bank,p,board,input);
        }
        check(bank.snapshot().bank_completions==completion+1&&
              bank.snapshot().multiplier_tier==(completion<4?completion+1:4)&&
              bank.snapshot().score_multiplier==(completion<4?factors[completion+1]:10),
              "bank completion did not advance numeric multiplier");
        const rules::TargetBankSnapshot down=bank.target_bank_snapshot();
        check(!down.raised[0]&&!down.raised[1]&&!down.raised[2]&&down.raise_pending,
              "bank completion immediately rearmed target bodies");
        bank_park(bank,p);
        if(completion==0) {
            run_for(bank,50000000);
            check(!bank.target_bank_snapshot().lamp_visible[0],"first lamp did not start OFF");
            bank.set_suspended(true);const runtime::Tick now=bank.snapshot().time;
            bank.advance(1000000000);
            check(bank.snapshot().time==now&&!bank.target_bank_snapshot().lamp_visible[0],
                  "pause aged bank flash/rearm");
            bank.set_suspended(false);run_for(bank,60000000);
            check(bank.target_bank_snapshot().lamp_visible[0],"source100ms lamp ON phase missing");
            run_for(bank,90000000);
        }else run_for(bank,200000000);
        check(!bank.target_bank_snapshot().raised[0]&&!bank.target_bank_snapshot().raised[1]&&
              !bank.target_bank_snapshot().raised[2],"target bank rearmed early");
        run_for(bank,60000000);
        check(bank.target_bank_snapshot().raised[0]&&bank.target_bank_snapshot().raised[1]&&
              bank.target_bank_snapshot().raised[2]&&!bank.target_bank_snapshot().raise_pending,
              "target bank failed to rearm after250ms");
        assert_bank_sprites(bank,p,board,input);
    }
    check(expected_score==52500&&bank.snapshot().score_multiplier==10,"maximum-tier total");
    bank_contact(bank,component_start(p,component_approaches[0]));expected_score+=5000;
    check(bank.snapshot().score==expected_score,"ordinary bumper award ignored multiplier");
    bank_contact(bank,rule_event_start(p,p.rules.ramp_score_collider_index));expected_score+=50000;
    check(bank.snapshot().score==expected_score,"ordinary ramp award ignored multiplier");
    bank_contact(bank,rule_event_start(p,p.rules.skill_gate_collider_index));
    bank_contact(bank,rule_event_start(p,p.rules.ramp_score_collider_index));expected_score+=25000;
    check(bank.snapshot().score==expected_score,"reflex award incorrectly multiplied");
    bank_park(bank,p);run_for(bank,30000000000LL);
    check(bank.snapshot().multiplier_tier==3&&bank.snapshot().score_multiplier==5&&
          !bank.target_bank_snapshot().lamp_visible[3],"Session missed30s multiplier decay");
    assert_bank_sprites(bank,p,board,input);
    bank.reset();
    check(bank.snapshot().score==0&&bank.snapshot().target_hits==0&&bank.snapshot().bank_completions==0&&
          bank.snapshot().score_multiplier==1&&bank.snapshot().multiplier_tier==0,
          "new game retained bank score/counters/tier");
    assert_bank_sprites(bank,p,board,input);

    // Protected loss retains partial target state and coefficient through
    // actual drain/feed timers; ordinary loss resets them after an unscaled bonus.
    development::PlayfieldTest loss(model);
    for(unsigned t=0;t<3;++t)bank_contact(loss,approaches[t]);
    bank_park(loss,p);run_for(loss,260000000);
    bank_contact(loss,approaches[0]);
    check(loss.snapshot().score==3500&&loss.snapshot().score_multiplier==2,"saved-bank setup");
    bank_contact(loss,actual_drain_start(p));
    check(loss.snapshot().saved_drains==1&&loss.snapshot().score_multiplier==2&&
          !loss.target_bank_snapshot().raised[0]&&loss.snapshot().score==3500,
          "protected drain reset bank/coefficient or awarded bonus");
    run_for(loss,p.rules.drain_delay+p.rules.feed_delay);
    check(loss.snapshot().ball_active&&!loss.target_bank_snapshot().raised[0]&&
          loss.snapshot().score_multiplier==2,"saved re-serve lost partial bank");
    bank_contact(loss,rule_event_start(p,p.rules.skill_gate_collider_index));
    bank_park(loss,p);run_for(loss,10000000001LL);test_toggle(loss);
    bank_contact(loss,actual_drain_start(p));
    check(loss.snapshot().saved_drains==1&&loss.snapshot().drains==2&&loss.snapshot().score==13500&&
          loss.snapshot().score_multiplier==1&&loss.snapshot().multiplier_tier==0&&
          loss.target_bank_snapshot().raised[0]&&loss.target_bank_snapshot().raised[1]&&
          loss.target_bank_snapshot().raised[2],"ordinary loss multiplied bonus or retained bank");
    assert_bank_sprites(loss,p,board,input);
    check(bank.snapshot().exhausted_steps==0&&loss.snapshot().exhausted_steps==0&&
          loss.snapshot().moving_exhausted==0,"bank scenario exhausted work");
    std::cout<<"multiplier bank Session PASS:3targets,5completions,old-score tiers,lamps,rearm,decay,saved/ordinary loss\n";
}
motion::BallState lane_fixture(const reference::TablePhysics& p,std::size_t group) {
    const std::size_t groups[6]={520,521,522,528,529,530};
    const float x[6]={1.250000954f,.000000978f,-1.249999046f,7.267515182f,6.319341660f,5.186196804f};
    const float y[6]={-9.140520096f,-9.149652481f,-9.131938934f,2.111552954f,2.354452610f,2.652395964f};
    for(unsigned i=0;i<6;++i)if(group==groups[i]){
        motion::BallState state(physics::Vec2(x[i],y[i]),physics::Vec2(0,-20),p.ball_radius);
        state.collision_mask=i<3?1:4;state.height=i<3?p.ball_radius:1.360704f;return state;
    }
    throw std::runtime_error("missing lane fixture");
}
void lane_pass(development::PlayfieldTest& game,const reference::TablePhysics& p,std::size_t group) {
    if(game.snapshot().hidden_test)test_toggle(game);
    const std::uint32_t old=game.snapshot().lane_hits;
    game.set_replay_ball(lane_fixture(p,group));game.advance(8333333);
    check(game.snapshot().lane_hits==old+1&&!game.rollover_visual(group).visible,"lane entry did not score/hide once");
    run_for(game,50000000);
    check(game.rollover_visual(group).visible&&game.rollover_visual(group).remaining>0,"lane exit/rearm state");
}
void lane_and_medal_progression(const table::TableModel& model,scene::ResourceScene& board,
                                const runtime::InputState& input) {
    const reference::TablePhysics p(model);
    for(unsigned b=0;b<2;++b)for(unsigned lane=0;lane<3;++lane){
        const std::size_t group=p.lane_groups[b].rollover_groups[lane];
        development::PlayfieldTest game(model);
        lane_pass(game,p,group);
        check(game.snapshot().score==p.lane_groups[b].rollover_award,"lane default ordinary award");
        const std::uint32_t hit_count=game.snapshot().lane_hits;
        game.set_replay_ball(lane_fixture(p,group));game.advance(8333333);
        check(game.snapshot().lane_hits==hit_count,"rollover100ms rearm failed");
        run_for(game,150000000);
        lane_pass(game,p,group);
        check(!game.progression_snapshot().groups[b].logical_on[lane],"lit rollover did not toggle off");
        check(game.snapshot().exhausted_steps==0,"lane passage exhausted work");
    }
    development::PlayfieldTest lanes(model);
    std::uint64_t expected=0;
    for(unsigned b=0;b<2;++b){
        for(unsigned lane=0;lane<3;++lane){
            lane_pass(lanes,p,p.lane_groups[b].rollover_groups[lane]);
            expected+=p.lane_groups[b].rollover_award;
        }
        check(lanes.snapshot().score==expected&&lanes.progression_snapshot().groups[b].level==1&&
              lanes.snapshot().lane_completions==b+1,"lane collection did not upgrade associated bumpers");
        for(std::size_t i=0;i<p.lane_groups[b].bumper_groups.size();++i)
            check(lanes.component_visual(p.lane_groups[b].bumper_groups[i]).frame==3&&
                  lanes.component_visual(p.lane_groups[b].bumper_groups[i]).active,"level upgrade did not Fire new pair");
        bank_park(lanes,p);
        const runtime::Tick before=lanes.snapshot().time;lanes.set_suspended(true);lanes.advance(1000000000);
        check(lanes.snapshot().time==before,"pause aged lane/bumper progression");lanes.set_suspended(false);
        run_for(lanes,120000000);
        for(std::size_t i=0;i<p.lane_groups[b].bumper_groups.size();++i)
            check(lanes.component_visual(p.lane_groups[b].bumper_groups[i]).frame==2&&
                  !lanes.component_visual(p.lane_groups[b].bumper_groups[i]).active,"upgraded idle frame/cooldown");
        const unsigned bumper_fixture=b==0?0:4;
        bank_contact(lanes,component_start(p,component_approaches[bumper_fixture]));
        expected+=p.lane_groups[b].bumper_scores[1];
        check(lanes.snapshot().score==expected,"upgraded bumper ordinary score");
        lanes.render(board,input);
    }
    bank_park(lanes,p);run_for(lanes,5000000000LL);
    lane_pass(lanes,p,p.lane_groups[0].rollover_groups[0]);expected+=p.lane_groups[0].rollover_award;
    lanes.input(edge(runtime::Action::LeftFlipper,true));lanes.input(edge(runtime::Action::LeftFlipper,false));
    check(lanes.progression_snapshot().groups[0].logical_on[1],"LT source lane change direction");
    lanes.input(edge(runtime::Action::RightFlipper,true));lanes.input(edge(runtime::Action::RightFlipper,false));
    check(lanes.progression_snapshot().groups[0].logical_on[0],"RT source lane change direction");
    check(lanes.snapshot().score==expected,"lane change itself awarded points");

    const motion::BallState medal_approaches[3]={
        motion::BallState(physics::Vec2(1.173519373f,-2.411895514f),physics::Vec2(-6.571051598f,-18.88971329f),p.ball_radius),
        motion::BallState(physics::Vec2(.5111113191f,-2.181467772f),physics::Vec2(-6.571050644f,-18.88971519f),p.ball_radius),
        motion::BallState(physics::Vec2(-.1487601399f,-1.951922297f),physics::Vec2(-6.571084023f,-18.88970184f),p.ball_radius)};
    development::PlayfieldTest medal(model);
    const std::uint64_t totals[3]={13000,66000,69000};
    for(unsigned completion=0;completion<3;++completion){
        for(unsigned target=0;target<3;++target)bank_contact(medal,medal_approaches[target]);
        check(medal.snapshot().medal_hits==(completion+1)*3&&medal.snapshot().medal_completions==completion+1&&
              medal.snapshot().medal_tier==completion+1&&medal.snapshot().score==totals[completion],
              "medal stage score/progression");
        check(medal.snapshot().extra_balls_pending==(completion==2?1U:0U),"medal extra-ball credit stage");
        medal.render(board,input);
        bank_park(medal,p);run_for(medal,260000000);
    }
    bank_contact(medal,actual_drain_start(p));
    check(medal.snapshot().saved_drains==1&&medal.snapshot().extra_balls_pending==1&&
          medal.snapshot().medal_tier==3,"protected drain consumed medal credit/progress");
    run_for(medal,p.rules.drain_delay+p.rules.feed_delay);
    bank_contact(medal,rule_event_start(p,p.rules.skill_gate_collider_index));
    bank_park(medal,p);run_for(medal,10000000001LL);test_toggle(medal);
    bank_contact(medal,actual_drain_start(p));
    check(medal.snapshot().score==79000&&medal.snapshot().extra_balls_pending==0&&medal.snapshot().extra_balls_used==1&&
          medal.snapshot().balls_remaining==3&&medal.snapshot().ball_number==1&&medal.snapshot().medal_tier==0,
          "earned extra-ball loss did not bonus/reset/re-serve same numbered ball");
    run_for(medal,p.rules.drain_delay+p.rules.feed_delay);
    check(medal.snapshot().ball_active&&medal.snapshot().ball_number==1,"earned ball did not re-serve");
    medal.reset();lanes.reset();
    check(medal.snapshot().extra_balls_pending==0&&medal.snapshot().medal_hits==0&&
          lanes.snapshot().ground_bumper_level==0&&lanes.snapshot().raised_bumper_level==0&&
          !lanes.component_visual(372).active&&lanes.component_visual(372).frame==0,"new game retained progression");
    check(medal.snapshot().exhausted_steps==0&&medal.snapshot().moving_exhausted==0,"medal replay exhausted work");
    std::cout<<"lanes/medal Session PASS:6entry-exit sensors,toggle/rotation,bumper upgrade,3medal stages,earned extra-ball\n";
}
float sink_front_distance(const motion::StaticCollider& gate,physics::Vec2 position) {
    const physics::Vec2 edge_vector=physics::subtract(gate.second,gate.first);
    const physics::Vec2 front=physics::unit(physics::Vec2(-edge_vector.y,edge_vector.x));
    return physics::dot(physics::subtract(position,gate.first),front);
}
void wormhole_release_flights(const table::TableModel& model) {
    const reference::TablePhysics p(model);const visual::Camera camera=visual::camera_from_table(model);
    for(unsigned sink_index=0;sink_index<3;++sink_index)for(unsigned hidden=0;hidden<2;++hidden) {
        const reference::SinkProfile& sink=p.sinks[sink_index];
        const motion::StaticCollider& gate=p.static_colliders[sink.collider_index];
        const physics::Vec2 edge_vector=physics::subtract(gate.second,gate.first);
        const physics::Vec2 front=physics::unit(physics::Vec2(-edge_vector.y,edge_vector.x));
        const physics::Vec2 middle=physics::multiply(physics::add(gate.first,gate.second),.5f);
        const motion::BallState incoming(physics::add(middle,physics::multiply(front,p.ball_radius+.08f)),
            physics::multiply(front,-2),p.ball_radius);
        development::PlayfieldTest game(model,incoming);
        bool captured=false;
        for(unsigned frame=0;frame<120;++frame){game.advance(8333333);if(game.snapshot().captured){captured=true;break;}}
        check(captured&&game.snapshot().sink_captures==1&&game.snapshot().score==2500,
              "wormhole release fixture failed to capture/award once");
        if(hidden){test_toggle(game);game.set_test_stick(0,0);}
        bool ejected=false;
        for(unsigned frame=0;frame<300;++frame){
            game.advance(8333333);
            if(game.snapshot().sink_ejections!=0){ejected=true;break;}
        }
        check(ejected&&game.snapshot().sink_ejections==1&&game.snapshot().sink_captures==1&&!game.snapshot().captured,
              "wormhole recaptured its outgoing release in the ejection frame");
        // Even a release split at the final nanosecond of a public tick must
        // get a complete next movement query and clear the offset gate.
        game.advance(8333333);game.advance(8333333);
        check(game.snapshot().sink_captures==1&&!game.snapshot().captured&&
              sink_front_distance(gate,game.snapshot().ball.position)>p.ball_radius+.001f,
              "wormhole outgoing ball did not clear its own capture strip");
        std::uint32_t previous_captures=game.snapshot().sink_captures;
        std::uint32_t previous_ejections=game.snapshot().sink_ejections;
        const runtime::Tick flight_begin=game.snapshot().time;
        for(unsigned frame=0;frame<1200;++frame) {
            game.advance(8333333);const development::MotionSnapshot s=game.snapshot();
            if(s.sink_ejections>previous_ejections)
                check(s.sink_captures==previous_captures,"later wormhole release immediately self-captured");
            if(hidden)
                check(s.sink_captures==1&&s.sink_ejections==1&&s.score==2500&&!s.captured,
                      "neutral hidden wormhole escape farmed captures/score");
            previous_captures=s.sink_captures;previous_ejections=s.sink_ejections;
            check(s.exhausted_steps==0&&s.moving_exhausted==0,"wormhole release flight exhausted work");
        }
        check(game.snapshot().time-flight_begin==1200LL*8333333,"wormhole long flight skipped simulation time");
        // Normal physics may reach another legitimate component after escape.
        // A controlled genuine inward crossing must remain eligible: the fix
        // must not introduce a timer-based immunity or globally disable sinks.
        if(game.snapshot().captured) {
            for(unsigned frame=0;frame<600&&game.snapshot().captured;++frame)game.advance(8333333);
        }
        check(!game.snapshot().captured,"wormhole escape could not reach a free re-entry fixture");
        const std::uint32_t captures_before=game.snapshot().sink_captures;
        const std::uint64_t score_before=game.snapshot().score;
        const unsigned destination=game.advanced_snapshot().wormhole_destination;
        const std::uint64_t expected_award=std::uint64_t(destination?(destination==sink_index+1?5000:7500):2500)*game.snapshot().score_multiplier;
        game.set_replay_ball(incoming);
        if(hidden)test_aim(game,camera,physics::subtract(middle,physics::multiply(front,.5f)));
        bool returned=false;
        for(unsigned frame=0;frame<120;++frame){
            game.advance(8333333);
            if(game.snapshot().sink_captures>captures_before){returned=true;break;}
        }
        check(returned&&game.snapshot().captured&&game.snapshot().sink_captures==captures_before+1&&
              game.snapshot().score==score_before+expected_award,"genuine inward wormhole re-entry missed its current routing award");
        game.set_test_stick(0,0);run_for(game,500000000);
        check(game.snapshot().sink_captures==captures_before+1&&game.snapshot().score==score_before+expected_award,
              "held re-entry repeated its capture award");
    }
    std::cout<<"wormhole release flights PASS:3sinks normal/hidden,10s escape,eligible re-entry\n";
}
void test(const table::TableModel& model,const std::string& out){
    development::PlayfieldTest game(model);runtime::InputState input;
    reference::TablePhysics profile(model);
    check(game.collider_count()==profile.static_colliders.size(),"reference collider count");
    near(game.snapshot().ball.position.y,profile.feed_position.y,0,"source feed Y");
    check(!game.snapshot().seated,"feed must start above striker");
    scene::SceneLayout layout;layout.width=600;layout.height=416;
    scene::ResourceScene scene=scene::static_resource_scene(model,layout);
    const scene::Surface background=scene.background;
    game.render(scene,input);const std::uint64_t feed_hash=hash(scene.surface);
    settle(game);game.render(scene,input);save(out,"reference-seated.bmp",scene.surface);
    check(game.snapshot().ball.position.y>profile.feed_position.y,"feed did not move downhill");
    game.input(edge(runtime::Action::Plunger,true));
    near(game.snapshot().charge,1.0f,.00001f,"immediate reference charge");
    run_for(game,500000000);
    near(game.snapshot().charge,21.0f,.00001f,"reference half-second charge");
    game.render(scene,input);save(out,"reference-partial.bmp",scene.surface);
    run_for(game,game.full_pull_time()-500000000);
    near(game.snapshot().charge,100,0,"full reference charge");
    game.render(scene,input);save(out,"reference-full.bmp",scene.surface);
    game.input(edge(runtime::Action::Plunger,false));game.render(scene,input);
    check(game.snapshot().launch_window && game.snapshot().launches==0,"release must open collision window");
    for(std::size_t i=0;i<scene.instances.size();++i)if(scene.instances[i].object_type==1001)
        check(scene.instances[i].state_index==0,"release did not snap art to rest");
    run_for(game,8333333);
    check(game.snapshot().launches==1,"plunger collision did not power launch");
    const float powered_speed=physics::length(game.snapshot().ball.velocity);
    check(powered_speed>59.99f && powered_speed<=60.001f,"XP query cap was not applied after the powered collision");
    run_for(game,8333333);
    check(physics::length(game.snapshot().ball.velocity)<=60.001f,"subsequent movement exceeded XP speed cap");
    run_for(game,250000000);game.render(scene,input);save(out,"reference-flight.bmp",scene.surface);
    check(game.snapshot().ball.position.y<profile.feed_position.y,"strong launch failed to travel up lane");
    const motion::BallState before=game.snapshot().ball;
    game.render(scene,input);const scene::Surface unshaken=scene.surface;
    game.input(edge(runtime::Action::NudgeLeft,true));
    check(game.snapshot().shake_x==2 && game.snapshot().shake_y==-1,"left bumper visual direction");
    near(game.snapshot().ball.velocity.x,before.velocity.x+1,.00002f,"source nudge X");
    near(game.snapshot().ball.velocity.y,before.velocity.y+.5f,.00002f,"source nudge Y");
    game.render(scene,input);save(out,"reference-shaken.bmp",scene.surface);
    check(hash(scene.surface)!=hash(unshaken),"table image did not shake");
    for(unsigned y=0;y<416;++y)for(unsigned x=386;x<600;++x){
        const std::size_t p=y*600+x;
        check(scene.surface.depth[p]==unshaken.depth[p],"sidebar depth moved");
        for(unsigned c=0;c<4;++c)check(scene.surface.pixels[p*4+c]==unshaken.pixels[p*4+c],"sidebar moved");
    }
    game.input(edge(runtime::Action::NudgeLeft,false));game.render(scene,input);
    check(game.snapshot().shake_x==0 && game.snapshot().shake_y==0,"early release did not restore table");
    near(game.snapshot().ball.velocity.x,before.velocity.x,.00002f,"nudge inverse X");
    near(game.snapshot().ball.velocity.y,before.velocity.y,.00002f,"nudge inverse Y");
    check(hash(scene.surface)==hash(unshaken),"jolt left stale pixels");
    game.input(edge(runtime::Action::NudgeRight,true));
    run_for(game,reference::nudge_hold_duration);
    check(game.snapshot().shake_x==0 && game.snapshot().shake_y==0,"shared timer did not return table");
    capture_regressions(model,scene,input,out);
    wormhole_release_flights(model);
    hidden_test_controls(model,scene,input,out);
    component_reactions(model,scene,input,out);
    finite_game_flow(model,scene,input,out);
    multiplier_bank_regressions(model,scene,input);
    lane_and_medal_progression(model,scene,input);
    run_for(game,4000000000LL);
    check(game.snapshot().exhausted_steps==0 && game.snapshot().moving_exhausted==0,"ordinary play exhausted collision work");
    game.reset();settle(game);
    game.input(edge(runtime::Action::LeftFlipper,true));run_for(game,20000000);
    check(game.snapshot().left_flipper>.49 && game.snapshot().left_flipper<.51,"timed flipper");
    game.input(edge(runtime::Action::Plunger,true));game.input(edge(runtime::Action::NudgeLeft,true));
    game.set_suspended(true);const runtime::Tick paused=game.snapshot().time;
    check(game.snapshot().charge==0 && game.snapshot().shake_x==0,"pause did not cancel controls");
    game.advance(1000000000);check(game.snapshot().time==paused,"pause advanced simulation");
    game.set_suspended(false);run_for(game,80000000);
    check(game.snapshot().left_flipper==0 && game.snapshot().launches==0,"safe resume return");
    game.reset();game.render(scene,input);check(hash(scene.surface)==feed_hash,"reset did not restore source feed");
    check(background.pixels==scene.background.pixels && background.depth==scene.background.depth,"immutable background changed");
    std::cout<<"source-reference playfield checks passed; powered collision speed "<<powered_speed
        <<", static+striker colliders "<<game.collider_count()<<"; XP runtime fidelity remains unverified\n";
}
}
int main(int argc,char**argv){
    try{test(table::TableModel::load(argc>1?argv[1]:"game:\\PINBALL.DAT"),argc>2?argv[2]:"");return 0;}
    catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}




