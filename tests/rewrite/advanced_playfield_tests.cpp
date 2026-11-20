#if defined(_WIN32) && !defined(__XSDK__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "playfield_test.hpp"
#include "table_model.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace pinball;
const runtime::Tick frame=8333333;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
runtime::InputTransition edge(runtime::Action action,bool pressed) {
    const runtime::InputTransition result={1,1,action,pressed,runtime::TransitionCause::Physical};return result;
}
void toggle_test(development::PlayfieldTest& game) {
    game.input(edge(runtime::Action::HiddenTest,true));game.input(edge(runtime::Action::HiddenTest,false));
}
std::vector<audio::SoundCue> take_sounds(development::PlayfieldTest& game) {
    std::vector<audio::SoundCue> result;game.drain_sounds(result);return result;
}
std::size_t count_sound(const std::vector<audio::SoundCue>& sounds,std::size_t group,audio::SoundRole role) {
    std::size_t count=0;
    for(std::size_t i=0;i<sounds.size();++i)if(sounds[i].group==group&&sounds[i].role==role)++count;
    return count;
}
void run_for(development::PlayfieldTest& game,runtime::Tick duration) {
    const runtime::Tick requested=duration,begin=game.snapshot().time;
    for(;;) {
        volatile std::uint32_t high=static_cast<std::uint32_t>(static_cast<std::uint64_t>(duration)>>32);
        volatile std::uint32_t low=static_cast<std::uint32_t>(duration);
        if(high==0&&low==0)break;
        const runtime::Tick part=(high!=0||low>1000000000U)?1000000000:low;
        game.advance(part);duration-=part;
    }
    check(game.snapshot().time-begin==requested,"advanced replay skipped requested simulation time");
}
struct Fixture {
    std::size_t group;float x,y,vx,vy;std::uint32_t mask;float height;
};
// Whole-profile clear approaches independently measured from the 793 original
// DAT descriptors. CSV evidence is under work/physics-v11-feature-audit. These
// harness positions are not claimed to be original game trajectories.
const Fixture deployment={346,-6.043090343f,4.309700966f,20,0,1,.3f};
const Fixture ramp={101,6.384543896f,-1.346595407f,16.53057289f,11.25789261f,4,1.360703945f};
const Fixture selectors[]={
    {448,4.957827568f,3.381533861f,-19.99171448f,.5756528378f,1,.3f},
    {450,4.957827568f,4.035659790f,-19.99171448f,.5756528378f,1,.3f},
    {452,4.957827568f,4.676702499f,-19.99171448f,.5756530762f,1,.3f}
};
const Fixture ground_lanes[]={
    {520,1.250000954f,-9.140520096f,0,-20,1,.3f},
    {521,.000000978f,-9.149652481f,0,-20,1,.3f},
    {522,-1.249999046f,-9.131938934f,0,-20,1,.3f}
};
const Fixture attack_bumper={372,.6270002127f,-3.463601828f,-18.47759056f,-7.653668404f,1,.3f};
motion::BallState ball(const reference::TablePhysics& profile,const Fixture& fixture) {
    motion::BallState result(physics::Vec2(fixture.x,fixture.y),physics::Vec2(fixture.vx,fixture.vy),profile.ball_radius);
    result.collision_mask=fixture.mask;result.height=fixture.height;return result;
}
void validate_fixture(const reference::TablePhysics& profile,const Fixture& fixture,bool trigger) {
    std::vector<motion::StaticCollider> colliders=profile.static_colliders;
    for(std::size_t i=0;i<colliders.size();++i)colliders[i].enabled=true;
    motion::BallState probe=ball(profile,fixture);
    const motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),frame,24,.0001f,profile.field.maximum_speed));
    const motion::MotionStep result=stepper.step(probe,frame,colliders);
    if(result.contacts.empty())std::cerr<<"fixture desired="<<fixture.group<<" no contacts\n";
    else {
        const motion::MotionContact& first=result.contacts[0];
        if(first.moving||first.hit.initial_overlap||profile.collider_groups[first.collider_index]!=fixture.group)
            std::cerr<<"fixture desired="<<fixture.group<<" first_index="<<first.collider_index
                <<" first_group="<<(first.moving?0:profile.collider_groups[first.collider_index])
                <<" overlap="<<first.hit.initial_overlap<<" moving="<<first.moving
                <<" triggered="<<first.triggered<<" activated="<<first.activated
                <<" time="<<first.seconds_from_start<<" p="<<fixture.x<<','<<fixture.y
                <<" v="<<fixture.vx<<','<<fixture.vy<<" mask="<<fixture.mask<<'\n';
    }
    check(!result.contacts.empty()&&!result.contacts[0].moving&&!result.contacts[0].hit.initial_overlap,
          "advanced fixture lacks a clean first contact");
    check(profile.collider_groups[result.contacts[0].collider_index]==fixture.group,
          "advanced fixture first contacts another actual table component");
    check(trigger?result.contacts[0].triggered:result.contacts[0].activated,
          "advanced fixture does not activate its intended event");
    check(!result.captured&&!result.contact_budget_exhausted&&!result.moving_work_exhausted,
          "advanced fixture unexpectedly captures or exhausts work");
}
Fixture bank_fixture(const reference::TablePhysics& profile,unsigned target) {
    const reference::TargetProfile& owner=profile.multiplier_bank.targets.at(target);
    std::vector<motion::StaticCollider> colliders=profile.static_colliders;
    for(std::size_t i=0;i<colliders.size();++i)colliders[i].enabled=true;
    const motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),frame,24,.0001f,profile.field.maximum_speed));
    const float fractions[]={.25f,.5f,.75f};
    for(std::size_t i=0;i<owner.collider_indices.size();++i)for(unsigned sample=0;sample<3;++sample) {
        const motion::StaticCollider& edge=colliders.at(owner.collider_indices[i]);
        const physics::Vec2 axis=physics::subtract(edge.second,edge.first);
        const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
        const physics::Vec2 point=physics::add(edge.first,physics::multiply(axis,fractions[sample]));
        const physics::Vec2 position=physics::add(point,physics::multiply(front,profile.ball_radius+.004f));
        const physics::Vec2 velocity=physics::multiply(front,-20);
        Fixture result={owner.group,position.x,position.y,velocity.x,velocity.y,1,profile.ball_radius};
        motion::BallState probe=ball(profile,result);
        const motion::MotionStep contacts=stepper.step(probe,frame,colliders);
        if(!contacts.contacts.empty()&&!contacts.contacts[0].moving&&contacts.contacts[0].activated&&
           !contacts.contacts[0].hit.initial_overlap&&!contacts.captured&&!contacts.contact_budget_exhausted&&
           profile.collider_groups[contacts.contacts[0].collider_index]==owner.group)return result;
    }
    throw std::runtime_error("no clear source-geometry multiplier target approach");
}
void contact(development::PlayfieldTest& game,const reference::TablePhysics& profile,const Fixture& fixture) {
    if(game.snapshot().hidden_test)toggle_test(game);
    game.set_replay_ball(ball(profile,fixture));game.advance(frame);
    check(game.snapshot().exhausted_steps==0&&game.snapshot().moving_exhausted==0,
          "advanced actual contact exhausted work");
    check(game.snapshot().ball_active&&!game.snapshot().game_over&&!game.snapshot().captured,
          "advanced fixture lost ownership of the active round ball");
}
void park(development::PlayfieldTest& game,const reference::TablePhysics& profile) {
    if(!game.snapshot().hidden_test)toggle_test(game);
    game.set_replay_ball(motion::BallState(profile.feed_position,physics::Vec2(),profile.ball_radius));
    game.set_test_stick(0,0);
}
bool visible(const scene::ResourceScene& scene,std::size_t group) {
    for(std::size_t i=0;i<scene.instances.size();++i)
        if(scene.instances[i].resource_group==group)return scene.instances[i].visible;
    throw std::runtime_error("requested advanced original sprite was not cached");
}
std::uint32_t ramp_award(const reference::TablePhysics& profile) {
    return profile.rules.event_group_awards.at(profile.rules.ramp_score_group);
}
void start_mission(development::PlayfieldTest& game,const reference::TablePhysics& profile,
                   unsigned choice,rules::MissionId id,unsigned count,
                   scene::ResourceScene& board,const runtime::InputState& input) {
    check(game.mission_snapshot().phase==rules::MissionPhase::Waiting,"finite round did not begin awaiting deployment");
    take_sounds(game);contact(game,profile,deployment);
    check(count_sound(take_sounds(game),65,audio::SoundRole::Resource)==1,
          "positive deployment skill award omitted source soundwave3");
    check(game.mission_snapshot().phase==rules::MissionPhase::Selecting&&
          game.mission_snapshot().selected==rules::MissionId::None,"actual oneway4 did not open mission selection");
    contact(game,profile,selectors[choice]);
    check(game.mission_snapshot().selected==id&&game.mission_snapshot().phase==rules::MissionPhase::Selecting,
          "actual mission spot selected the wrong cadet objective");
    game.render(board,input);check(visible(board,163),"original mission-selected lamp is not visible");
    const std::uint64_t score=game.snapshot().score;
    take_sounds(game);contact(game,profile,ramp);
    check(count_sound(take_sounds(game),73,audio::SoundRole::Resource)==1,
          "accepting a selected mission omitted source soundwave24 ramp cue");
    const rules::MissionSnapshot mission=game.mission_snapshot();
    check(mission.phase==rules::MissionPhase::Active&&mission.active==id&&mission.selected==rules::MissionId::None,
          "actual ramp did not accept selected mission");
    check(mission.stage==0&&mission.remaining==count,"accepting ramp incorrectly counted as an objective shot");
    check(game.snapshot().score-score==10000U+std::uint64_t(ramp_award(profile))*game.snapshot().score_multiplier,"cadet mission start award or ordinary ramp award differs");
    game.render(board,input);check(visible(board,229)&&!visible(board,163),"original mission active/selected light transition");
    park(game,profile);
    const runtime::Tick before=game.snapshot().time;
    game.set_suspended(true);game.advance(1000000000);
    check(game.snapshot().time==before&&game.mission_snapshot().remaining==count,"pause advanced mission clock or objective");
    game.set_suspended(false);
}
void complete_check(const development::PlayfieldTest& game) {
    const rules::MissionSnapshot state=game.mission_snapshot();
    check(state.phase==rules::MissionPhase::Selecting&&state.active==rules::MissionId::None&&
          state.selected==rules::MissionId::None&&state.completed==1,"completed actual mission retained active ownership");
    check(state.rank==1&&state.rank_progress==6,"cadet completion rank progress differs from source facts");
    check(game.snapshot().missions_completed==1&&game.snapshot().mission_id==0&&game.snapshot().rank_progress==6,
          "public MotionSnapshot differs from mission state");
}
void mission_practice(const table::TableModel& model,const reference::TablePhysics& profile,
                      scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    for(unsigned target=0;target<3;++target)contact(game,profile,bank_fixture(profile,target));
    check(game.snapshot().score_multiplier==2&&game.snapshot().score==2500,
          "actual multiplier setup for mission-special integration failed");
    park(game,profile);run_for(game,250000000);
    start_mission(game,profile,2,rules::MissionId::Practice,8,board,input);
    for(unsigned hit=0;hit<8;++hit) {
        const std::uint64_t score=game.snapshot().score;
        const std::uint32_t bumper_hits=game.snapshot().bumper_hits;
        contact(game,profile,attack_bumper);
        check(game.snapshot().bumper_hits==bumper_hits+1,"real practice bumper hit was lost or counted twice");
        const std::uint64_t expected=std::uint64_t(profile.lane_groups[0].bumper_scores[0])*2U+(hit==7?500000U:0U);
        check(game.snapshot().score-score==expected,"practice completion special points or ordinary bumper points differ");
        const std::vector<audio::SoundCue> sounds=take_sounds(game);
        check(count_sound(sounds,model.require_named_group("soundwave9"),audio::SoundRole::Resource)==(hit==7?1U:0U)&&
              count_sound(sounds,model.require_named_group("soundwave10"),audio::SoundRole::Resource)==0,
              "Practice completion omitted source soundwave9 or invented a rank cue");
        if(hit<7)check(game.mission_snapshot().remaining==7-hit,"practice objective did not count precisely one real contact");
        park(game,profile);run_for(game,110000000);
    }
    complete_check(game);game.render(board,input);
    check(!visible(board,229)&&visible(board,205),"completed mission or rank indicator retained wrong state");
    game.input(edge(runtime::Action::NewGame,true));
    check(game.snapshot().score==0&&game.mission_snapshot().phase==rules::MissionPhase::Waiting&&
          game.mission_snapshot().rank==1&&game.mission_snapshot().rank_progress==0&&!game.snapshot().hidden_test,
          "new game retained advanced mission/score/test state");
}
void mission_reentry(const table::TableModel& model,const reference::TablePhysics& profile,
                     scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    start_mission(game,profile,1,rules::MissionId::ReentryTraining,3,board,input);
    for(unsigned lane=0;lane<3;++lane) {
        const std::uint64_t score=game.snapshot().score;
        const std::uint32_t old_hits=game.snapshot().lane_hits;
        contact(game,profile,ground_lanes[lane]);
        check(game.snapshot().lane_hits==old_hits+1,"real reentry pass was lost or counted twice");
        check(game.snapshot().score-score==profile.lane_groups[0].rollover_award+(lane==2?500000U:0U),
              "reentry completion special points or ordinary lane points differ");
        if(lane<2)check(game.mission_snapshot().remaining==2-lane,"reentry objective count differs");
        run_for(game,50000000);park(game,profile);
    }
    complete_check(game);
    check(game.snapshot().ground_bumper_level==1&&game.snapshot().lane_completions==1,
          "mission lane completion bypassed normal bumper progression");
}
void mission_launch(const table::TableModel& model,const reference::TablePhysics& profile,
                    scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    start_mission(game,profile,0,rules::MissionId::LaunchTraining,3,board,input);
    for(unsigned shot=0;shot<3;++shot) {
        const std::uint64_t score=game.snapshot().score;
        contact(game,profile,ramp);
        check(game.snapshot().score-score==ramp_award(profile)+(shot==2?500000U:0U),
              "launch completion special points or ordinary ramp points differ");
        if(shot<2)check(game.mission_snapshot().remaining==2-shot,"launch objective counted accepting ramp or missed real ramp");
        park(game,profile);
    }
    complete_check(game);
}
Fixture point_event_fixture(const reference::TablePhysics& profile,std::size_t collider_index) {
    const motion::StaticCollider& event=profile.static_colliders.at(collider_index);
    const physics::Vec2 axis=physics::subtract(event.second,event.first);
    const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
    const physics::Vec2 center=physics::multiply(physics::add(event.first,event.second),.5f);
    const physics::Vec2 position=physics::add(center,physics::multiply(front,.01f));
    const physics::Vec2 velocity=physics::multiply(front,-5);
    const Fixture result={profile.collider_groups.at(collider_index),position.x,position.y,velocity.x,velocity.y,
        event.collision_mask,event.collision_mask==4?1.360703945f:profile.ball_radius};return result;
}
std::size_t sprite_frame(const scene::ResourceScene& scene,std::size_t group) {
    for(std::size_t i=0;i<scene.instances.size();++i)
        if(scene.instances[i].resource_group==group)return scene.instances[i].state_index;
    throw std::runtime_error("requested original advanced body was not cached");
}
void solo_cooldown(const table::TableModel& model,const reference::TablePhysics& profile,
                   scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    Fixture weak=selectors[0];weak.vx*=.1f;weak.vy*=.1f;
    contact(game,profile,weak);
    check(game.snapshot().score==0&&game.advanced_snapshot().target_enabled[13]&&
          !game.advanced_snapshot().target_fired[13],"weak actual solo contact fired or scored");
    contact(game,profile,selectors[0]);
    check(game.snapshot().score==1000&&!game.advanced_snapshot().target_enabled[13]&&
          game.advanced_snapshot().target_fired[13],"hard actual solo contact did not fire/disable/score once");
    game.render(board,input);check(visible(board,448)&&sprite_frame(board,448)==1,
          "fired solo did not draw original frame1");
    const rules::AdvancedSnapshot before_retry=game.advanced_snapshot();
    const runtime::Tick retry_start=game.snapshot().time;
    const std::uint64_t score_before_retry=game.snapshot().score;
    contact(game,profile,selectors[0]);
    const rules::AdvancedSnapshot after_retry=game.advanced_snapshot();
    check(!after_retry.target_enabled[13]&&after_retry.target_fired[13]&&
          after_retry.target_remaining[13]==before_retry.target_remaining[13]-(game.snapshot().time-retry_start),
          "disabled solo13 owner accepted/reset a retry before its fixed deadline");
    // The ball can pass the disabled body and reach a neighboring live solo
    // under the table field. Keep the owner13 deadline assertion and account
    // only newly activated OTHER owners, rather than suppressing real awards.
    std::uint64_t other_points=0;
    for(const reference::AdvancedTargetProfile& target:profile.advanced.targets)
        if(target.number!=13&&before_retry.target_enabled[target.number]&&!after_retry.target_enabled[target.number])
            other_points+=std::uint64_t(target.score)*game.snapshot().score_multiplier;
    const std::uint64_t retry_score=score_before_retry+other_points;
    check(game.snapshot().score==retry_score,"solo retry ledger contains an unowned/duplicate award");
    park(game,profile);
    const runtime::Tick remaining=game.advanced_target_visual(448).remaining;
    check(remaining>0&&remaining<=100000000,"solo exact cooldown remaining not exposed");
    const runtime::Tick clock=game.snapshot().time;
    game.set_suspended(true);game.advance(1000000000);
    check(game.snapshot().time==clock&&game.advanced_target_visual(448).remaining==remaining,
          "pause consumed solo target cooldown");game.set_suspended(false);
    run_for(game,remaining-1);
    check(!game.advanced_snapshot().target_enabled[13]&&game.advanced_target_visual(448).frame==1,
          "solo body/frame rearmed before100ms deadline");
    run_for(game,1);
    check(game.advanced_snapshot().target_enabled[13]&&!game.advanced_snapshot().target_fired[13]&&
          game.advanced_target_visual(448).remaining==0,"solo did not rearm at100ms deadline");
    game.render(board,input);check(visible(board,448)&&sprite_frame(board,448)==0,
          "rearmed solo did not restore original idle frame");
    contact(game,profile,selectors[0]);check(game.snapshot().score==retry_score+1000,"rearmed solo did not score normally");
}
void booster_rearm(const table::TableModel& model,const reference::TablePhysics& profile,
                   scene::ResourceScene& board,const runtime::InputState& input) {
    const Fixture booster[]={
        {432,-3.837959051f,1.093824267f,-20,0,1,.3f},
        {433,-3.837959051f,1.795166492f,-20,0,1,.3f},
        {434,-3.837959051f,2.493823528f,-20,0,1,.3f}
    };
    development::PlayfieldTest game(model);
    for(unsigned target=0;target<3;++target) {
        validate_fixture(profile,booster[target],false);
        contact(game,profile,booster[target]);
        check(!game.advanced_snapshot().target_enabled[target+1]&&!game.advanced_snapshot().target_visible[target+1],
              "booster popup did not disable/hide its whole owner");
        game.render(board,input);check(!visible(board,booster[target].group),"dropped booster original sprite remained visible");
    }
    check(game.snapshot().score==6000&&game.advanced_snapshot().flags_active&&game.advanced_snapshot().booster_lit,
          "booster completion score or first reward differs");
    game.render(board,input);check(visible(board,137)&&visible(board,138)&&visible(board,167),
          "first booster reward did not light its original three indicators");
    park(game,profile);const runtime::Tick remaining=game.advanced_target_visual(432).remaining;
    check(remaining>0&&remaining<=250000000,"booster rearm deadline not exposed");
    run_for(game,remaining-1);
    for(unsigned number=1;number<=3;++number)check(!game.advanced_snapshot().target_enabled[number],"booster popup rose early");
    run_for(game,1);
    for(unsigned number=1;number<=3;++number)check(game.advanced_snapshot().target_enabled[number]&&
        game.advanced_snapshot().target_visible[number],"booster popup did not rise at250ms completion deadline");
    game.render(board,input);
    for(unsigned target=0;target<3;++target)check(visible(board,booster[target].group)&&sprite_frame(board,booster[target].group)==0,
        "raised booster did not restore original popup frame");
    game.input(edge(runtime::Action::NewGame,true));
    check(game.snapshot().score==0&&!game.advanced_snapshot().flags_active&&!game.advanced_snapshot().booster_lit,
          "new game retained booster rewards");
}
void fuel_cancel_and_refill(const table::TableModel& model,const reference::TablePhysics& profile,
                            scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    check(game.advanced_snapshot().fuel_index==0&&!game.advanced_snapshot().fuel_countdown&&!game.advanced_snapshot().fuel_empty,
          "serve should show full fuel without starting index11 countdown");
    start_mission(game,profile,0,rules::MissionId::LaunchTraining,3,board,input);
    const Fixture gate=point_event_fixture(profile,profile.rules.skill_gate_collider_index);
    contact(game,profile,gate);
    check(game.advanced_snapshot().fuel_countdown&&game.advanced_snapshot().fuel_index==11,
          "real skill gate1 did not start full fuel countdown");
    park(game,profile);const std::uint64_t score=game.snapshot().score;
    runtime::Tick total=0;
    for(std::size_t i=0;i<profile.advanced.fuel.level_durations.size();++i)total+=profile.advanced.fuel.level_durations[i];
    run_for(game,total+frame);
    check(game.advanced_snapshot().fuel_empty&&!game.advanced_snapshot().fuel_countdown&&
          game.mission_snapshot().phase==rules::MissionPhase::Selecting&&game.mission_snapshot().active==rules::MissionId::None&&
          !game.mission_snapshot().fuel_available,"fuel exhaustion did not cancel the actual active mission");
    check(game.snapshot().score==score&&game.snapshot().ball_active&&game.snapshot().balls_remaining==3&&
          game.snapshot().missions_completed==0,"fuel expiry changed score/ball ledger or completed failed mission");
    const Fixture top_fuel={536,6.739692211f,-3.147507429f,5.020306110f,-19.35966301f,1,.3f};
    validate_fixture(profile,top_fuel,true);contact(game,profile,top_fuel);
    check(game.snapshot().score==score+500&&!game.advanced_snapshot().fuel_empty&&
          game.advanced_snapshot().fuel_countdown&&game.advanced_snapshot().fuel_index==11&&game.mission_snapshot().fuel_available,
          "real top fuel rollover failed to refill/re-enable mission selection");
    check(game.mission_snapshot().phase==rules::MissionPhase::Selecting&&game.mission_snapshot().selected==rules::MissionId::None,
          "refill silently restarted a failed mission");
    game.render(board,input);
    for(unsigned i=0;i<6;++i)check(game.advanced_snapshot().fuel_lamps[i]&&visible(board,255+i),
          "refilled bar did not enable all six original fuel lamp states");
}
void hyperspace_holds(const table::TableModel& model,const reference::TablePhysics& profile) {
    const Fixture hyperspace_fixture={512,-3.546120167f,-9.216409683f,14.14213562f,-14.14213562f,1,.3f};
    development::PlayfieldTest game(model);
    game.set_replay_ball(ball(profile,hyperspace_fixture));game.advance(frame);
    check(game.snapshot().captured&&game.snapshot().captured_kickout&&game.snapshot().kickout_captures==1&&
          game.advanced_snapshot().hyperspace_level==1&&game.snapshot().score==10000,
          "real hyperspace capture did not apply first-stage award/state");
    check(game.snapshot().ball.height==0&&physics::length(game.snapshot().ball.velocity)==0,
          "hyperspace held pose must be source Z0 and stationary");
    toggle_test(game);const runtime::Tick paused=game.snapshot().time;
    game.set_suspended(true);game.advance(1000000000);game.advance(1000000000);
    check(game.snapshot().time==paused&&game.snapshot().captured&&game.snapshot().kickout_ejections==0,
          "pause advanced hyperspace sound-derived hold");game.set_suspended(false);
    const runtime::Tick normal=19358LL*runtime::ticks_per_second/11025LL;
    run_for(game,normal-20000000);
    check(game.snapshot().captured&&game.snapshot().kickout_ejections==0,"hyperspace ejected before original stage0 WAV hold");
    run_for(game,20000000);
    check(!game.snapshot().captured&&game.snapshot().kickout_ejections==1&&game.snapshot().score==10000,
          "hyperspace did not eject after original stage0 WAV hold");
    // Test the source cooldown behavior directly. Hidden neutral suppresses
    // external fields, but capture collision remains active when eligible.
    motion::BallState center(profile.advanced.hyperspace.center,physics::Vec2(),profile.ball_radius);
    game.set_replay_ball(center);run_for(game,10000000);
    check(!game.snapshot().captured&&game.snapshot().kickout_captures==1&&game.snapshot().score==10000,
          "hyperspace50ms cooldown allowed immediate center recapture");
    park(game,profile);run_for(game,100000000);
    toggle_test(game);game.set_replay_ball(ball(profile,hyperspace_fixture));game.advance(frame);
    check(game.snapshot().captured&&game.snapshot().kickout_captures==2&&game.advanced_snapshot().hyperspace_level==2&&
          game.snapshot().score==55000,"eligible hyperspace reentry lost jackpot/25000Reflex or counted twice");
    toggle_test(game);
    const runtime::Tick reflex=9054LL*runtime::ticks_per_second/11025LL;
    run_for(game,reflex-20000000);
    check(game.snapshot().captured&&game.snapshot().kickout_ejections==1,"Reflex hyperspace ejected before original sound21 hold");
    run_for(game,30000000);
    check(!game.snapshot().captured&&game.snapshot().kickout_ejections==2&&game.snapshot().score==55000,
          "Reflex hyperspace did not release without another award");
    check(game.snapshot().exhausted_steps==0&&game.snapshot().moving_exhausted==0,"hyperspace integration exhausted work");
}
motion::BallState sink_approach(const reference::TablePhysics& profile,unsigned owner,float gap) {
    const motion::StaticCollider& gate=profile.static_colliders.at(profile.sinks.at(owner).collider_index);
    const physics::Vec2 axis=physics::subtract(gate.second,gate.first);
    const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
    const physics::Vec2 middle=physics::multiply(physics::add(gate.first,gate.second),.5f);
    return motion::BallState(physics::add(middle,physics::multiply(front,profile.ball_radius+gap)),
                            physics::multiply(front,-2),profile.ball_radius);
}
void trigger_tilt(development::PlayfieldTest& game,const reference::TablePhysics& profile) {
    if(game.snapshot().hidden_test)toggle_test(game);
    game.set_replay_ball(motion::BallState(profile.feed_position,physics::Vec2(),profile.ball_radius));
    run_for(game,frame); // An inactive frame clamps a prior feed's -2 to zero.
    game.input(edge(runtime::Action::NudgeLeft,true));run_for(game,200000000);
    check(!game.snapshot().tilted&&game.snapshot().tilt_pressure>.5&&game.snapshot().tilt_pressure<1,
          "normal nudge did not pass warning pressure before tilt threshold");
    run_for(game,100000000);
    check(game.snapshot().tilted&&game.snapshot().tilt_pressure>1&&game.tilt_snapshot().tilt_lock&&
          game.tilt_snapshot().timeout_pending,"normal held nudge did not enter finite-round tilt");
    game.input(edge(runtime::Action::NudgeLeft,false));
}
void tilt_finite_cycle(const table::TableModel& model,const reference::TablePhysics& profile,
                       scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    const Fixture destination={466,-2.644778967f,-2.607370138f,-17.70150566f,9.308959007f,1,.3f};
    contact(game,profile,destination);game.set_replay_ball(sink_approach(profile,0,.08f));
    for(unsigned i=0;i<120&&!game.snapshot().captured;++i)game.advance(frame);
    check(game.snapshot().captured&&game.snapshot().replay_armed,"matching real wormhole did not arm replay before tilt fixture");
    toggle_test(game);
    for(unsigned i=0;i<300&&game.snapshot().captured;++i)game.advance(frame);
    check(!game.snapshot().captured,"replay primer could not leave its two-second wormhole hold");
    park(game,profile);const std::uint64_t score=game.snapshot().score;
    check(game.snapshot().shoot_again&&game.snapshot().replay_armed,"tilt fixture lost serve/replay protection before nudging");
    trigger_tilt(game,profile);
    check(!game.snapshot().shoot_again&&!game.snapshot().replay_armed,"tilt retained save or replay protection");
    park(game,profile);game.render(board,input);const bool first=visible(board,179);
    run_for(game,100000000);game.render(board,input);
    check(visible(board,179)!=first,"original tilt lamp did not alternate its visible phase");
    for(const scene::SpriteInstance& image:board.instances)
        if(image.object_type==1002&&image.resource_group!=179)check(!image.visible,"tilt left an ordinary table lamp visible");
    game.input(edge(runtime::Action::LeftFlipper,true));game.input(edge(runtime::Action::RightFlipper,true));
    run_for(game,50000000);
    check(game.snapshot().left_flipper==0&&game.snapshot().right_flipper==0,"tilt accepted new flipper inputs");
    const std::uint32_t hits=game.snapshot().bumper_hits;
    contact(game,profile,attack_bumper);
    check(game.snapshot().score==score&&game.snapshot().bumper_hits==hits&&physics::length(game.snapshot().ball.velocity)<20,
          "tilted bumper retained scoring or powered response");
    const Fixture hyperspace_fixture={512,-3.546120167f,-9.216409683f,14.14213562f,-14.14213562f,1,.3f};
    game.set_replay_ball(ball(profile,hyperspace_fixture));game.advance(frame);
    check(game.snapshot().captured_kickout&&game.snapshot().score==score&&game.advanced_snapshot().hyperspace_level==0,
          "tilted active kickout awarded points or progressed its controller");
    toggle_test(game);run_for(game,80000000);
    check(game.snapshot().captured_kickout,"tilted kickout released before its100ms hold");
    run_for(game,30000000);
    check(!game.snapshot().captured&&game.snapshot().score==score,"tilted kickout did not release at100ms without award");
    if(game.snapshot().hidden_test)toggle_test(game);
    // Incoming initial overlap intentionally records the sink/drain event at
    // this exact clock, making the subsequent feed boundary observable.
    game.set_replay_ball(sink_approach(profile,1,-.001f));
    const runtime::Tick drained_at=game.snapshot().time;game.advance(1);
    check(game.snapshot().drains==1&&game.snapshot().saved_drains==0&&!game.snapshot().ball_active&&
          game.snapshot().serve_pending&&game.snapshot().balls_remaining==2&&game.snapshot().score==score,
          "tilted sink was not an unsaved ordinary loss with a frozen score");
    run_for(game,profile.rules.drain_delay+profile.rules.feed_delay-1);
    check(game.snapshot().time==drained_at+profile.rules.drain_delay+profile.rules.feed_delay&&
          game.snapshot().ball_active&&game.snapshot().balls_remaining==2&&game.snapshot().ball_number==2&&
          !game.snapshot().tilted&&!game.tilt_snapshot().tilt_lock&&game.snapshot().tilt_pressure==-2,
          "tilt drain/feed boundary did not clear lock and reset pressure to-2");
    game.advance(1);check(game.snapshot().tilt_pressure==0,"first inactive postfeed frame did not clamp tilt pressure to0");
}
void tilt_timeout_and_kickback_exception(const table::TableModel& model,const reference::TablePhysics& profile) {
    development::PlayfieldTest game(model);trigger_tilt(game,profile);
    const Fixture right_kickback={507,-5.224841118f,11.44925976f,0,20,1,.3f};
    contact(game,profile,right_kickback);
    check(game.snapshot().tilted&&game.snapshot().score==0&&physics::length(game.snapshot().ball.velocity)>40,
          "tilt incorrectly removed the source kickback55 boost exception");
    park(game,profile);const runtime::Tick remaining=game.tilt_snapshot().remaining;
    check(remaining>0&&remaining<=30000000000LL,"tilt timeout remaining outside source30s");
    run_for(game,remaining-1);
    check(game.snapshot().ball_active&&game.snapshot().drains==0&&game.snapshot().tilted,
          "hidden neutral tilt timeout drained before30s");
    run_for(game,1);
    check(!game.snapshot().ball_active&&game.snapshot().serve_pending&&game.snapshot().drains==1&&
          game.snapshot().saved_drains==0&&game.snapshot().balls_remaining==2&&game.snapshot().score==0,
          "tilt30s timeout failed to force an unsaved drain while hovering");
}
void kickback_cycle(const table::TableModel& model,const reference::TablePhysics& profile,
                     scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    const Fixture right={507,-5.224841118f,11.44925976f,0,20,1,.3f};
    check(game.advanced_snapshot().gates_open[1],"serve did not open right kickback gate");
    contact(game,profile,right);
    check(game.kickback_snapshot(507).kick_active&&!game.kickback_snapshot(507).armed&&game.snapshot().score==0,
          "first real kickback contact did not start passive arm delay");
    park(game,profile);runtime::Tick remaining=game.kickback_snapshot(507).remaining;
    check(remaining>0,"kickback arm callback was not scheduled");run_for(game,remaining-1);
    check(!game.kickback_snapshot(507).armed,"kickback armed before its DAT delay");run_for(game,1);
    check(game.kickback_snapshot(507).armed&&game.kickback_snapshot(507).frame==1,"kickback failed to arm/show fired frame at deadline");
    game.render(board,input);check(visible(board,507)&&sprite_frame(board,507)==1,"armed kickback original image was not displayed");
    contact(game,profile,right);
    check(!game.kickback_snapshot(507).kick_active&&!game.kickback_snapshot(507).armed&&
          physics::length(game.snapshot().ball.velocity)>40&&game.snapshot().score==0,
          "armed real kickback did not return ball with source boost55");
    park(game,profile);remaining=game.kickback_snapshot(507).remaining;
    check(remaining>0&&game.advanced_snapshot().gates_open[1],"kickback consumed pulse/gate callback too early");
    run_for(game,remaining-1);check(game.advanced_snapshot().gates_open[1],"kickback closed gate before queued callback");run_for(game,1);
    check(!game.advanced_snapshot().gates_open[1]&&game.kickback_snapshot(507).frame==0,
          "spent kickback callback did not restore idle art/close gate");
    game.render(board,input);check(visible(board,502)&&visible(board,507)&&sprite_frame(board,507)==0,
          "right kickback gate/idle images not restored after callback");
}
void escape_return(const table::TableModel& model,const reference::TablePhysics& profile) {
    development::PlayfieldTest game(model);
    const Fixture escape={344,-5.857958794f,3.419572115f,-.04942625761f,-19.99993896f,1,.3f};
    game.set_replay_ball(ball(profile,escape));game.advance(frame);
    check(game.snapshot().captured&&!game.snapshot().captured_kickout&&game.snapshot().score==0&&
          game.snapshot().balls_remaining==3&&game.snapshot().drains==0,"real escape344 did not capture without score/loss");
    toggle_test(game);run_for(game,1980000000);
    check(game.snapshot().captured&&game.snapshot().sink_ejections==0,"escape344 returned before its two-second delay");
    run_for(game,20000000);
    check(!game.snapshot().captured&&game.snapshot().sink_ejections==1&&game.snapshot().score==0&&
          game.snapshot().ball_active&&game.snapshot().balls_remaining==3&&game.snapshot().drains==0,
          "escape344 did not return same ball at two seconds without score");
    game.advance(frame);game.advance(frame);
    check(!game.snapshot().captured&&game.snapshot().sink_captures==1&&game.snapshot().sink_ejections==1,
          "escape344 immediately recaptured its outgoing return");
}
void cheat_commands_and_rank(const table::TableModel& model,const reference::TablePhysics& profile,
                             scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);game.set_suspended(true);
    const runtime::Tick clock=game.snapshot().time;
    check(game.activate_cheat(rules::CheatAction::ExtraBall)&&game.snapshot().extra_balls_pending==1&&
          game.snapshot().balls_remaining==3&&game.snapshot().score==0,"paused extra-ball cheat did not grant exactly one credit");
    check(game.activate_cheat(rules::CheatAction::HiddenTest)&&game.snapshot().hidden_test,
          "paused backend Hidden Test action failed");
    check(game.activate_cheat(rules::CheatAction::HiddenTest)&&game.snapshot().hidden_test,
          "repeated backend Hidden Test action toggled the mode off");
    check(game.activate_cheat(rules::CheatAction::GravityWell)&&game.snapshot().gravity_well_active,
          "paused backend gravity-well action failed");
    check(game.activate_cheat(rules::CheatAction::RankUp)&&game.snapshot().rank==2&&game.snapshot().rank_progress==0,
          "paused rank action failed or granted unrelated progress");
    const std::vector<audio::SoundCue> announced=take_sounds(game);
    check(count_sound(announced,model.require_named_group("soundwave10"),audio::SoundRole::Resource)==1&&
          count_sound(announced,model.require_named_group("soundwave28"),audio::SoundRole::Resource)==1&&
          count_sound(announced,model.require_named_group("soundwave9"),audio::SoundRole::Resource)==0,
          "paused rank/extra credit actions omitted actual DAT-named sounds or added a mission sound");
    check(game.snapshot().time==clock&&game.snapshot().suspended&&game.snapshot().cheats_used&&game.snapshot().score==0,
          "backend paused cheat action advanced simulation or score");
    game.render(board,input);check(visible(board,205)&&!visible(board,206),"new rank lamp must begin its two-second flash OFF");
    game.set_suspended(false);park(game,profile);run_for(game,100000000);game.render(board,input);
    check(visible(board,205)&&visible(board,206),"new original rank lamp did not enter ON flash phase");
    run_for(game,100000000);game.render(board,input);check(!visible(board,206),"rank flash did not return to OFF phase");
    run_for(game,1800000000);game.render(board,input);check(visible(board,206),"rank lamp did not settle ON at two seconds");
    take_sounds(game);
    for(unsigned promotion=0;promotion<12;++promotion)check(game.activate_cheat(rules::CheatAction::RankUp),"rank action was unexpectedly rejected");
    check(count_sound(take_sounds(game),model.require_named_group("soundwave10"),audio::SoundRole::Resource)==7,
          "rank cap replayed promotion audio or omitted one of seven real gains");
    check(game.snapshot().rank==9&&game.snapshot().rank_progress==0&&game.snapshot().score==0,
          "rank cheat exceeded rank9 or changed points/progress");
    run_for(game,2000000000);game.render(board,input);
    for(std::size_t group=205;group<=213;++group)check(visible(board,group),"capped rank9 omitted an original rank lamp");
    game.input(edge(runtime::Action::NewGame,true));
    check(!game.snapshot().cheats_used&&!game.snapshot().hidden_test&&!game.snapshot().unlimited_balls&&
          !game.snapshot().gravity_well_active&&game.snapshot().extra_balls_pending==0&&game.snapshot().rank==1&&
          game.snapshot().score==0&&!game.snapshot().suspended,"Y new game did not clear gameplay cheat state");
}
void cheat_gravity_well(const table::TableModel& model,const reference::TablePhysics& profile) {
    development::PlayfieldTest game(model);game.set_suspended(true);
    check(game.activate_cheat(rules::CheatAction::GravityWell)&&game.snapshot().gravity_well_active,
          "gravity-well backend did not enable the real profile capture");
    game.set_suspended(false);
    const Fixture gravity={510,.1800000072f,6,-20,0,1,.3f};
    game.set_replay_ball(ball(profile,gravity));game.advance(frame);
    check(game.snapshot().captured_kickout&&game.snapshot().kickout_captures==1&&game.snapshot().score==50000&&
          !game.snapshot().gravity_well_active&&game.snapshot().cheats_used,
          "real gravity well did not award50000 and consume its enabled state");
    check(game.activate_cheat(rules::CheatAction::HiddenTest),"hidden action during owned gravity hold failed");
    const runtime::Tick hold=26302LL*runtime::ticks_per_second/11025LL;
    run_for(game,hold-20000000);check(game.snapshot().captured_kickout,"gravity well released before original sound7 hold");
    run_for(game,20000000);
    check(!game.snapshot().captured&&game.snapshot().kickout_ejections==1&&game.snapshot().score==50000,
          "gravity-well release repeated score or missed original sound7 hold");
    park(game,profile);run_for(game,100000000);contact(game,profile,gravity);
    check(!game.snapshot().captured&&game.snapshot().kickout_captures==1&&game.snapshot().score==50000&&
          !game.snapshot().gravity_well_active,"consumed gravity well reactivated after cooldown without another action");
}
void cheat_unlimited_return(const table::TableModel& model,const reference::TablePhysics& profile,
                            scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    for(unsigned target=0;target<3;++target)contact(game,profile,bank_fixture(profile,target));
    park(game,profile);run_for(game,250000000);
    start_mission(game,profile,2,rules::MissionId::Practice,8,board,input);
    game.set_suspended(true);check(game.activate_cheat(rules::CheatAction::UnlimitedBalls),"paused unlimited action failed");
    game.set_suspended(false);if(game.snapshot().hidden_test)toggle_test(game);
    const development::MotionSnapshot before=game.snapshot();
    const rules::MissionSnapshot mission=game.mission_snapshot();
    const motion::StaticCollider& drain=profile.static_colliders.at(profile.rules.drain_collider_index);
    const physics::Vec2 axis=physics::subtract(drain.second,drain.first);
    const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
    const physics::Vec2 center=physics::multiply(physics::add(drain.first,drain.second),.5f);
    game.set_replay_ball(motion::BallState(physics::add(center,physics::multiply(front,profile.ball_radius-.001f)),
                                         physics::multiply(front,-1),profile.ball_radius));
    const runtime::Tick drained_at=game.snapshot().time;game.advance(1);
    const development::MotionSnapshot held=game.snapshot();
    check(held.unlimited_balls&&held.captured&&!held.captured_kickout&&held.ball_active&&!held.serve_pending&&
          held.drains==before.drains&&held.saved_drains==before.saved_drains&&held.returns==before.returns&&
          held.balls_remaining==before.balls_remaining&&held.ball_number==before.ball_number&&
          held.serves==before.serves&&held.score==before.score&&held.total_score==before.total_score&&
          held.multiplier_tier==before.multiplier_tier&&game.mission_snapshot().active==mission.active&&
          game.mission_snapshot().remaining==mission.remaining,"unlimited actual drain touched GameRules, bonus, life or table state");
    const runtime::Tick delay=profile.sinks[2].delay;
    run_for(game,delay-2);check(game.snapshot().captured,"unlimited return ejected before source two-second timer");
    run_for(game,1);const development::MotionSnapshot returned=game.snapshot();
    check(returned.time==drained_at+delay&&!returned.captured&&returned.sink_ejections==before.sink_ejections+1&&
          returned.ball.position.x==profile.sinks[2].position.x&&returned.ball.position.y==profile.sinks[2].position.y,
          "unlimited drain did not return through original sink3 at the exact two-second boundary");
    check(returned.unlimited_balls&&returned.drains==before.drains&&returned.serves==before.serves&&
          returned.balls_remaining==before.balls_remaining&&returned.score==before.score&&
          returned.multiplier_tier==before.multiplier_tier&&game.mission_snapshot().active==mission.active,
          "unlimited return reset table state or changed the finite-round ledger");
    trigger_tilt(game,profile);check(!game.snapshot().unlimited_balls,"tilt did not clear Unlimited Balls");
    game.set_replay_ball(sink_approach(profile,1,-.001f));game.advance(1);
    check(game.snapshot().drains==before.drains+1&&!game.snapshot().ball_active&&game.snapshot().balls_remaining==2,
          "post-unlimited tilt sink did not take an ordinary lost ball");
    check(!game.activate_cheat(rules::CheatAction::ExtraBall),"backend granted an extra-ball command without an active ball");
    run_for(game,profile.rules.drain_delay+profile.rules.feed_delay-1);
    check(game.snapshot().ball_active&&!game.snapshot().unlimited_balls&&game.snapshot().cheats_used,
          "normal feed retained Unlimited Balls or erased the round's CheatsUsed flag");
    game.input(edge(runtime::Action::NewGame,true));
    check(!game.snapshot().unlimited_balls&&!game.snapshot().cheats_used&&!game.snapshot().gravity_well_active&&
          !game.snapshot().hidden_test&&game.snapshot().rank==1&&game.snapshot().extra_balls_pending==0,
          "Y reset did not clear all backend cheat effects");
}
void frontend_ball_visibility(const table::TableModel& model,const reference::TablePhysics& profile,
                              scene::ResourceScene& board,const runtime::InputState& input) {
    development::PlayfieldTest game(model);
    game.set_replay_ball(motion::BallState(physics::Vec2(),physics::Vec2(),profile.ball_radius));
    const development::MotionSnapshot before=game.snapshot();
    game.render(board,input,false);const scene::Surface hidden=board.surface;
    // Scene instances already have the same runtime lamp/body states. A raw
    // scene rebuild omits only the separately drawn ball and numeric sidebar.
    board.render();const scene::Surface raw=board.surface;
    const unsigned table_width=board.background.width<365?board.background.width:365;
    for(unsigned y=0;y<board.surface.height;++y)for(unsigned x=0;x<table_width;++x) {
        const std::size_t sample=static_cast<std::size_t>(y)*board.surface.width+x;
        check(hidden.depth[sample]==raw.depth[sample]&&
              std::equal(hidden.pixels.begin()+sample*4U,hidden.pixels.begin()+sample*4U+4U,raw.pixels.begin()+sample*4U),
              "frontend ball-visible=false changed the physical table or left a ball sample");
    }
    game.render(board,input,true);const scene::Surface visible_ball=board.surface;
    std::size_t changed=0;
    for(unsigned y=0;y<board.surface.height;++y)for(unsigned x=0;x<board.surface.width;++x) {
        const std::size_t sample=static_cast<std::size_t>(y)*board.surface.width+x;
        const bool different=!std::equal(hidden.pixels.begin()+sample*4U,hidden.pixels.begin()+sample*4U+4U,
                                        visible_ball.pixels.begin()+sample*4U);
        if(x<table_width&&different)++changed;
        if(x>=386)check(!different&&hidden.depth[sample]==visible_ball.depth[sample],
                      "ball visibility changed original sidebar/HUD pixels");
    }
    check(changed>0,"visible frontend ball produced no original sprite pixels on the playfield");
    game.render(board,input,false);
    check(board.surface.pixels==hidden.pixels&&board.surface.depth==hidden.depth,
          "rehiding frontend ball did not restore the exact frame");
    const development::MotionSnapshot after=game.snapshot();
    check(after.time==before.time&&after.score==before.score&&after.serves==before.serves&&
          after.ball.position.x==before.ball.position.x&&after.ball.position.y==before.ball.position.y,
          "frontend ball rendering mutated the post-start simulation");
}
void wormhole_reentry_diagnostics(const table::TableModel& model) {
    const reference::TablePhysics profile(model);const visual::Camera camera=visual::camera_from_table(model);
    for(unsigned owner=0;owner<3;++owner)for(unsigned hidden=0;hidden<2;++hidden) {
        const reference::SinkProfile& sink=profile.sinks[owner];
        const motion::StaticCollider& sensor=profile.static_colliders[sink.collider_index];
        const physics::Vec2 axis=physics::subtract(sensor.second,sensor.first);
        const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
        const physics::Vec2 center=physics::multiply(physics::add(sensor.first,sensor.second),.5f);
        const motion::BallState incoming(physics::add(center,physics::multiply(front,profile.ball_radius+.08f)),physics::multiply(front,-2),profile.ball_radius);
        development::PlayfieldTest game(model,incoming);
        for(unsigned step=0;step<120&&!game.snapshot().captured;++step)game.advance(frame);
        check(game.snapshot().captured&&game.snapshot().sink_captures==1,"wormhole diagnostic initial physical capture failed");
        if(hidden)toggle_test(game);
        for(unsigned step=0;step<300&&!game.snapshot().sink_ejections;++step)game.advance(frame);
        check(game.snapshot().sink_ejections==1&&!game.snapshot().captured,"wormhole diagnostic outgoing self-captured");
        game.advance(frame);game.advance(frame);
        for(unsigned step=0;step<1200;++step)game.advance(frame);
        for(unsigned step=0;step<600&&game.snapshot().captured;++step)game.advance(frame);
        check(!game.snapshot().captured,"wormhole diagnostic cannot place genuine re-entry");
        const development::MotionSnapshot before=game.snapshot();
        const rules::AdvancedSnapshot feature=game.advanced_snapshot();
        std::cout<<"wormcase group="<<sink.group<<" hidden="<<hidden<<" before_score="<<before.score
            <<" captures="<<before.sink_captures<<" ejections="<<before.sink_ejections
            <<" multiplier="<<before.score_multiplier<<" destination="<<feature.wormhole_destination
            <<" flags="<<feature.flags_active<<" targets="<<before.target_hits
            <<" bumpers="<<before.bumper_hits<<" rebounds="<<before.rebound_hits<<'\n';
        game.set_replay_ball(incoming);
        if(hidden) {
            const physics::Vec2 target=physics::subtract(center,physics::multiply(front,.5f));
            const visual::ProjectedPoint a=visual::project(camera,incoming.position.x,incoming.position.y,incoming.height);
            const visual::ProjectedPoint b=visual::project(camera,target.x,target.y,incoming.height);
            const double dx=b.x-a.x,dy=b.y-a.y,length=std::sqrt(dx*dx+dy*dy);
            game.set_test_stick(static_cast<float>(dx/length),static_cast<float>(-dy/length));
        }
        std::uint64_t previous=before.score;unsigned destination=feature.wormhole_destination;
        for(unsigned step=0;step<120;++step) {
            game.advance(frame);const development::MotionSnapshot now=game.snapshot();
            const rules::AdvancedSnapshot state=game.advanced_snapshot();
            if(now.score!=previous||state.wormhole_destination!=destination) {
                std::cout<<" reentry frame="<<step<<" score="<<now.score<<" delta="<<now.score-previous
                    <<" captures="<<now.sink_captures<<" captured="<<now.captured
                    <<" destination="<<state.wormhole_destination<<" replay="<<now.replay_armed
                    <<" contacts="<<now.contacts<<" p="<<now.ball.position.x<<','<<now.ball.position.y<<'\n';
                previous=now.score;destination=state.wormhole_destination;
            }
            if(now.sink_captures>before.sink_captures)break;
        }
        const development::MotionSnapshot after=game.snapshot();
        std::cout<<" result captures_delta="<<after.sink_captures-before.sink_captures<<" score_delta="<<after.score-before.score
            <<" captured="<<after.captured<<" mask="<<after.ball.collision_mask
            <<" p="<<after.ball.position.x<<','<<after.ball.position.y<<" v="<<after.ball.velocity.x<<','<<after.ball.velocity.y
            <<" exhausted="<<after.exhausted_steps<<'/'<<after.moving_exhausted<<'\n';
        check(after.sink_captures==before.sink_captures+1&&after.captured,"wormhole diagnostic genuine physical re-entry failed");
        game.set_test_stick(0,0);run_for(game,500000000);
        std::cout<<" held score_delta="<<game.snapshot().score-before.score<<" captures_delta="<<game.snapshot().sink_captures-before.sink_captures<<'\n';
    }
}
// Event-level regression coverage for the reported silent control paths.
// These checks validate cue ownership/timing, independently of PCM availability
// or whether a muted/native device actually produces audible output.
motion::BallState cue_event_start(const reference::TablePhysics& profile,std::size_t index,bool solid) {
    const motion::StaticCollider& shape=profile.static_colliders.at(index);
    const physics::Vec2 axis=physics::subtract(shape.second,shape.first);
    const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
    const physics::Vec2 middle=physics::multiply(physics::add(shape.first,shape.second),.5f);
    const float gap=solid?profile.ball_radius-.001f:.01f;
    motion::BallState result(physics::add(middle,physics::multiply(front,gap)),
        physics::multiply(front,solid?-1.0f:-5.0f),profile.ball_radius);
    result.collision_mask=shape.collision_mask;return result;
}
void sound_cue_lifecycle(const table::TableModel& model,const reference::TablePhysics& profile) {
    const std::size_t table_group=model.require_named_group("table");
    const std::size_t skill_sound=model.require_named_group("soundwave14");
    const std::size_t drain_sound=model.require_named_group("soundwave27");
    const std::size_t plain_ramp=model.require_named_group("soundwave30");
    const std::size_t reflex_ramp=model.require_named_group("soundwave21");
    development::PlayfieldTest game(model);
    check(count_sound(take_sounds(game),table_group,audio::SoundRole::TableStart)==1,
          "finite constructor omitted or duplicated source table startup cue");
    game.reset();
    check(count_sound(take_sounds(game),table_group,audio::SoundRole::TableStart)==1,
          "new round retained an old startup cue or omitted the new one");
    development::PlayfieldTest physics_only(model,development::SessionMode::PhysicsTest);
    check(count_sound(take_sounds(physics_only),table_group,audio::SoundRole::TableStart)==0,
          "physics-only replay emitted a finite-round startup announcement");

    // Plain and Reflex ramp controls differ even though their generic geometry
    // has no soft sound slot. These are independent real profile crossings.
    contact(game,profile,ramp);
    check(count_sound(take_sounds(game),plain_ramp,audio::SoundRole::Resource)==1,
          "plain ramp event omitted its source soundwave30 control cue");
    game.set_replay_ball(cue_event_start(profile,profile.rules.skill_gate_collider_index,false));
    game.advance(frame);
    check(count_sound(take_sounds(game),skill_sound,audio::SoundRole::Resource)==1,
          "armed skill gate omitted its soundwave14 control tone");
    contact(game,profile,ramp);
    std::vector<audio::SoundCue> sounds=take_sounds(game);
    check(count_sound(sounds,reflex_ramp,audio::SoundRole::Resource)==1&&
          count_sound(sounds,plain_ramp,audio::SoundRole::Resource)==0,
          "Reflex ramp omitted its source control cue or also played the plain cue");
    game.set_replay_ball(cue_event_start(profile,profile.rules.skill_clear_collider_index,false));
    game.advance(frame);take_sounds(game);
    check(!game.advanced_snapshot().skill_armed,"skill-clear fixture did not disarm the source flag");
    game.set_replay_ball(cue_event_start(profile,profile.rules.skill_gate_collider_index,false));
    game.advance(frame);
    check(count_sound(take_sounds(game),skill_sound,audio::SoundRole::Resource)==0,
          "disarmed skill gate invented a soundwave14 tone");

    // Use a fresh round for finite loss timing; deliberately cross the source
    // skill gate before each drain, so this does not conceal a normal-route
    // or Hidden Test bypass report.
    game.reset();take_sounds(game);
    for(unsigned lost=0;lost<3;++lost) {
        game.set_replay_ball(cue_event_start(profile,profile.rules.skill_gate_collider_index,false));
        game.advance(frame);take_sounds(game);
        park(game,profile);run_for(game,10000000001LL);toggle_test(game);
        check(!game.snapshot().shoot_again,"cue round did not expire the gate-started save");
        take_sounds(game);
        game.set_replay_ball(cue_event_start(profile,profile.rules.drain_collider_index,true));
        game.advance(1);sounds=take_sounds(game);
        check(game.snapshot().drains==lost+1&&game.snapshot().saved_drains==0,
              "cue round fixture did not record an ordinary charged drain");
        check(count_sound(sounds,drain_sound,audio::SoundRole::Resource)==1&&
              count_sound(sounds,table_group,audio::SoundRole::TableGameOver)==0,
              "charged drain omitted soundwave27 or announced GameOver before settlement");
        run_for(game,profile.rules.drain_delay-1);sounds=take_sounds(game);
        check(count_sound(sounds,table_group,audio::SoundRole::TableGameOver)==(lost==2?1U:0U),
              "table GameOver cue missing, duplicated or emitted before the last ball");
        if(lost<2) {
            run_for(game,profile.rules.feed_delay);
            check(game.snapshot().ball_active,"cue round failed to serve its next ball");
            check(count_sound(take_sounds(game),table_group,audio::SoundRole::TableStart)==0,
                  "ordinary next-ball feed replayed the whole-game startup announcement");
        }
    }
    check(game.snapshot().game_over,"cue round did not reach actual GameOver");
    run_for(game,1000000000);
    check(count_sound(take_sounds(game),table_group,audio::SoundRole::TableGameOver)==0,
          "settled GameOver repeated its one-shot announcement");

    development::PlayfieldTest tilt(model);
    tilt.set_replay_ball(motion::BallState(profile.feed_position,physics::Vec2(),profile.ball_radius));
    tilt.advance(frame);take_sounds(tilt);
    tilt.input(edge(runtime::Action::NudgeLeft,true));run_for(tilt,200000000);
    check(!tilt.snapshot().tilted&&tilt.snapshot().tilt_pressure>.5,
          "audio warning fixture skipped the source warning interval");
    check(count_sound(take_sounds(tilt),table_group,audio::SoundRole::TableTilt)==0,
          "Careful warning invented an early tilt sound");
    run_for(tilt,100000000);
    check(tilt.snapshot().tilted&&count_sound(take_sounds(tilt),table_group,audio::SoundRole::TableTilt)==1,
          "actual tilt entry omitted or duplicated table sound108 cue");
    run_for(tilt,100000000);tilt.input(edge(runtime::Action::NudgeLeft,false));
    check(count_sound(take_sounds(tilt),table_group,audio::SoundRole::TableTilt)==0,
          "locked tilt repeated its one-shot entry sound");
}
void actual(const std::string& dat_path) {
    const table::TableModel model=table::TableModel::load(dat_path);
    const reference::TablePhysics profile(model);
    check(profile.advanced.deployment_gates.size()==2&&profile.advanced.targets.size()==16,
          "advanced integration requires the approved deployment/target profile");
    validate_fixture(profile,deployment,true);validate_fixture(profile,ramp,true);
    for(unsigned i=0;i<3;++i){validate_fixture(profile,selectors[i],false);validate_fixture(profile,ground_lanes[i],true);}
    validate_fixture(profile,attack_bumper,false);
    scene::ResourceScene board=scene::static_resource_scene(model);runtime::InputState input;
    for(std::size_t i=0;i<profile.advanced.lights.size();++i)
        check(!visible(board,profile.advanced.lights[i].group),"advanced profile light must be cached and initially hidden");
    for(std::size_t i=0;i<profile.advanced.hyperspace_lamps.size();++i)
        check(!visible(board,profile.advanced.hyperspace_lamps[i].group),"hyperspace profile lamp must be cached and initially hidden");
    frontend_ball_visibility(model,profile,board,input);
    mission_practice(model,profile,board,input);mission_reentry(model,profile,board,input);mission_launch(model,profile,board,input);
    solo_cooldown(model,profile,board,input);booster_rearm(model,profile,board,input);
    fuel_cancel_and_refill(model,profile,board,input);hyperspace_holds(model,profile);
    kickback_cycle(model,profile,board,input);escape_return(model,profile);
    tilt_finite_cycle(model,profile,board,input);tilt_timeout_and_kickback_exception(model,profile);
    cheat_commands_and_rank(model,profile,board,input);cheat_gravity_well(model,profile);
    cheat_unlimited_return(model,profile,board,input);
    sound_cue_lifecycle(model,profile);
    std::cout<<"Actual advanced Session PASS: real deployment/three selectors/ramp acceptance, Practice/Reentry/Launch completion, solo/booster timing, fuel cancellation/refill, hyperspace WAV holds/cooldown/Reflex, kickback/escape, finiteTilt/feed/timeout, fiveCheats/finiteLedger, points/rank/render/pause/reset\n";
}
}
int main(int argc,char** argv) {
#if defined(_WIN32) && !defined(__XSDK__)
    // Scope error-dialog suppression to this owned test process only. Never
    // change machine-wide reporting policy or another application.
    ::SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);
    try {
#if defined(__XSDK__)
        actual(argc>1?argv[1]:"game:\\PINBALL.DAT");
#else
        if(argc<2)throw std::invalid_argument("advanced_playfield_tests requires actual PINBALL.DAT path");
        if(argc>2&&std::string(argv[2])=="--diagnose-wormholes")wormhole_reentry_diagnostics(table::TableModel::load(argv[1]));
        else actual(argv[1]);
#endif
        return 0;
    }catch(const std::exception& e){std::cerr<<"Advanced Session tests FAIL: "<<e.what()<<'\n';return 1;}
}
