#include "game_rules.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pinball::rules;
using pinball::runtime::Tick;
const Tick second=1000000000LL;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Exception,class Operation>
void throws(Operation op,const char* message){try{op();}catch(const Exception&){return;}throw std::runtime_error(message);}
GameRulesConfig config(){return GameRulesConfig(3,1820000000,960000000,5*second,5*second,10000,25000);}
void tilt_scoring_and_drain(){
    GameRules game(config());game.award(500);game.award_replay();game.award_extra_ball();game.set_score_multiplier(10);
    game.tilt(0);check(game.snapshot().tilted&&!game.snapshot().shoot_again&&!game.snapshot().replay_armed,"tilt retained protection");
    check(!game.award(1000)&&!game.award_special(50000)&&!game.award_replay()&&!game.award_extra_ball(),"tilt accepted points or new rewards");
    game.skill_gate(0);game.hole_release(0);check(!game.snapshot().shoot_again&&!game.snapshot().reflex_active,"tilt gate rearmed protections");
    const DrainResult loss=game.drain(0);
    check(loss.accepted&&!loss.saved&&loss.extra_ball_used&&game.snapshot().score==500&&game.snapshot().balls_remaining==3,
          "tilt must omit bonus, consume earned credit and charge an ordinary loss");
    game.clear_tilt(0);game.advance(config().drain_delay+config().feed_delay);
    check(!game.snapshot().tilted&&game.snapshot().ball_active&&game.snapshot().shoot_again,"tilt cleanup did not restore normal serve");
    game.reset();game.set_score_multiplier(10);check(game.set_bonus(12345),"active bonus setter rejected");
    game.skill_gate(0);game.advance(10*second);game.drain(10*second);
    check(game.snapshot().score==12345,"configured loss bonus was multiplied or replaced");
    game.reset();check(!game.snapshot().tilted,"new game retained tilt lock");
}
void score_and_boundaries(){
    GameRules game(config());
    check(game.snapshot().score==0&&game.snapshot().total_score==0,"new score");
    check(game.award(500)&&game.snapshot().score==500,"basic award");
    check(game.award(999999500)&&game.snapshot().score==1000000000ULL,"exact billion wrapped early");
    check(game.award(500)&&game.snapshot().score==500&&game.snapshot().total_score==1000000500ULL,
          "display counter carry/total score");
    throws<std::invalid_argument>([&]{game.award(1000000001U);},"unsupported huge award accepted");
    check(game.snapshot().score==500,"invalid award partially changed score");
    game.reset();check(game.snapshot().score==0&&game.snapshot().total_score==0,"reset retained score carry");
}
void shoot_again_timing(){
    GameRules game(config());
    check(game.snapshot().shoot_again&&game.snapshot().save_phase==SavePhase::Armed,"initial indefinite save");
    const Tick start=4294967296LL;
    game.advance(start);
    check(game.snapshot().shoot_again&&game.snapshot().save_phase==SavePhase::Armed,
          "initial save incorrectly timed from serve");
    game.skill_gate(start);
    check(game.until_boundary()==5*second,"gate steady deadline");
    game.advance(start+5*second-1);check(game.snapshot().save_phase==SavePhase::Steady,"steady ended early");
    game.advance(start+5*second);check(game.snapshot().save_phase==SavePhase::Flashing&&game.snapshot().shoot_again,
          "flashing phase lost save");
    game.advance(start+10*second-1);check(game.snapshot().shoot_again,"flash protection ended early");
    game.advance(start+10*second);check(!game.snapshot().shoot_again,"save did not expire after5+5s");

    GameRules saved(config());
    saved.skill_gate(0);
    const DrainResult at_deadline=saved.drain(10*second);
    check(at_deadline.accepted&&at_deadline.saved,"equal-time drain lost collision-before-expiry priority");
    check(saved.snapshot().balls_remaining==3&&saved.snapshot().score==0&&saved.snapshot().saved_drains==1,
          "saved drain charged ball/bonus");
    const Tick serve=10*second+2780000000LL;
    check(saved.advance(serve).serve_ball&&saved.snapshot().ball_number==1&&saved.snapshot().shoot_again,
          "saved drain did not re-serve the same protected ball");
    saved.advance(serve+100*second);
    check(saved.snapshot().save_phase==SavePhase::Armed,"saved serve protection expired without gate");
    saved.skill_gate(serve+100*second);
    saved.advance(serve+106*second);
    const DrainResult in_flash=saved.drain(serve+106*second);
    check(in_flash.saved&&saved.snapshot().balls_remaining==3&&saved.snapshot().score==0,
          "flash drain was not saved");
}
void finite_round(){
    GameRules game(config());
    check(game.snapshot().player==1&&game.snapshot().ball_number==1&&game.snapshot().balls_remaining==3&&
          game.snapshot().ball_active&&!game.snapshot().game_over,"default one-player round");
    Tick now=0;
    const std::uint64_t expected_scores[]={10000,35000,60000};
    for(unsigned ball=0;ball<3;++ball){
        game.skill_gate(now);now+=10*second+1;game.advance(now);
        const DrainResult drain=game.drain(now);
        check(drain.accepted&&!drain.saved&&!game.snapshot().ball_active,"ordinary drain not accepted");
        check(game.snapshot().score==expected_scores[ball]&&game.snapshot().balls_remaining==2-ball,
              "ordinary drain bonus or ball charge");
        check(!game.drain(now).accepted,"duplicate drain charged twice");
        check(!game.award(500),"inactive ball could still score");
        game.advance(now+1819999999);
        check(!game.snapshot().game_over&&!game.snapshot().ball_active,"drain wait ended early");
        const RulesEvents after_drain=game.advance(now+1820000000);
        if(ball==2){
            check(after_drain.game_over&&!after_drain.serve_ball&&game.snapshot().game_over&&
                  game.snapshot().ball_number==0&&!game.snapshot().serve_pending,"third drain did not end game");
            game.advance(now+100*second);
            check(game.snapshot().game_over&&!game.snapshot().ball_active&&game.snapshot().serves==3,
                  "game over re-served a stray ball");
        }else{
            check(!after_drain.serve_ball&&game.snapshot().serve_pending&&
                  game.snapshot().phase==RoundPhase::ServeWait,"drain/feed delays conflated");
            game.advance(now+2779999999LL);check(!game.snapshot().ball_active,"feed delay ended early");
            now+=2780000000LL;
            check(game.advance(now).serve_ball&&game.snapshot().ball_active&&
                  game.snapshot().ball_number==ball+2,"next ball did not serve after1.82+.96s");
        }
    }
    game.reset();
    check(game.snapshot().score==0&&game.snapshot().ball_number==1&&game.snapshot().balls_remaining==3&&
          !game.snapshot().game_over&&game.snapshot().drains==0&&game.snapshot().serves==1,
          "new game failed to reset finite round");
}
void reflex_and_skill_clear(){
    GameRules game(config());
    check(!game.snapshot().reflex_active&&game.snapshot().skill_shot_armed,"initial reflex/skill state");
    check(game.award_ramp(5000,0)==5000,"unlit ramp base award");
    game.skill_gate(1);game.skill_clear(2);
    check(!game.snapshot().skill_shot_armed&&game.snapshot().reflex_active&&game.snapshot().shoot_again,
          "skill clear canceled existing reflex/save");
    check(game.award_ramp(5000,2)==25000,"active reflex award");
    game.advance(5*second+1);check(!game.snapshot().reflex_active,"reflex did not expire after5s");
    game.skill_gate(5*second+2);
    check(!game.snapshot().reflex_active&&game.snapshot().shoot_again,"disarmed gate reopened reflex or lost save");
    check(game.award_ramp(5000,5*second+2)==5000,"disarmed ramp did not use base score");
    game.hole_release(5*second+3);
    check(game.award_ramp(5000,5*second+3)==25000,"hole release did not refresh reflex");
    check(game.drain(5*second+4).saved,"protected reflex fixture did not save");
    game.advance(5*second+4+2780000000LL);
    check(!game.snapshot().skill_shot_armed,"saved serve incorrectly reset cleared skill state");
    const Tick later=20*second;
    game.advance(later);game.skill_gate(later);
    check(!game.snapshot().reflex_active,"saved cleared skill state did not persist");
    game.advance(later+10*second+1);
    check(!game.drain(later+10*second+1).saved,"ordinary reset fixture still saved");
    game.advance(later+10*second+1+2780000000LL);
    check(game.snapshot().skill_shot_armed,"ordinary serve did not rearm skill state");
    game.skill_gate(game.time());check(game.snapshot().reflex_active,"fresh ordinary skill gate did not open reflex");
}
void multiplier_awards(){
    GameRules game(config());game.set_score_multiplier(3);
    check(game.snapshot().score_multiplier==3&&game.award(500)&&game.snapshot().score==1500,
          "ordinary award did not use numeric multiplier");
    check(game.award_special(500)&&game.snapshot().score==2000,"special award multiplied");
    check(game.award_ramp(5000,0)==15000&&game.snapshot().score==17000,"base ramp did not multiply");
    game.skill_gate(1);
    check(game.award_ramp(5000,1)==25000&&game.snapshot().score==42000,"reflex award multiplied");
    throws<std::invalid_argument>([&]{game.set_score_multiplier(4);},"non-source multiplier accepted");
    check(game.snapshot().score_multiplier==3,"invalid multiplier mutated coefficient");
    game.set_score_multiplier(10);
    throws<std::invalid_argument>([&]{game.award(100000001);},"oversized multiplied award accepted");
    check(game.snapshot().score==42000,"invalid multiplied award partially scored");
    check(game.drain(2).saved&&game.snapshot().score_multiplier==10,"saved drain reset multiplier");
    game.advance(2780000002LL);game.skill_gate(game.time());
    game.advance(game.time()+10*second+1);
    check(!game.drain(game.time()).saved&&game.snapshot().score==52000&&game.snapshot().score_multiplier==1,
          "ordinary drain multiplied bonus or retained coefficient");
    game.reset();check(game.snapshot().score_multiplier==1,"new game retained multiplier");
}
void earned_lives_and_replay(){
    GameRules game(config());game.set_score_multiplier(5);
    check(game.award_extra_ball()&&game.award_extra_ball()&&game.award_replay()&&!game.award_replay(),
          "extra-ball credit or one-shot replay grant");
    check(game.snapshot().extra_balls_pending==2&&game.snapshot().replay_armed,"credit snapshot");
    check(game.drain(0).saved&&game.snapshot().replay_armed&&game.snapshot().extra_balls_pending==2,
          "initial Shoot Again consumed replay/extra credit");
    game.advance(2780000000LL);game.skill_gate(game.time());game.advance(game.time()+10*second+1);
    const DrainResult replay=game.drain(game.time());
    check(replay.saved&&replay.replay_used&&!replay.extra_ball_used&&!game.snapshot().replay_armed&&
          game.snapshot().balls_remaining==3&&game.snapshot().extra_balls_pending==2&&
          game.snapshot().score==0&&game.snapshot().score_multiplier==5,"replay priority/cleanup");
    game.advance(game.time()+2780000000LL);
    for(unsigned used=0;used<2;++used){
        game.skill_gate(game.time());game.advance(game.time()+10*second+1);
        const DrainResult extra=game.drain(game.time());
        check(!extra.saved&&extra.extra_ball_used&&!extra.replay_used&&game.snapshot().balls_remaining==3&&
              game.snapshot().ball_number==1&&game.snapshot().extra_balls_pending==1-used,
              "ordinary loss did not consume credit instead of numbered ball");
        check(game.snapshot().score==(used==0?10000:35000)&&game.snapshot().score_multiplier==1,
              "extra ball bypassed ordinary bonus/reset or multiplied bonus");
        game.advance(game.time()+2780000000LL);
    }
    game.skill_gate(game.time());game.advance(game.time()+10*second+1);game.drain(game.time());
    check(game.snapshot().balls_remaining==2&&game.snapshot().extra_balls_used==2&&
          game.snapshot().replays_used==1&&game.snapshot().extra_balls_awarded==2,
          "earned lives did not eventually charge ordinary numbered ball");
    game.reset();
    check(game.snapshot().extra_balls_pending==0&&!game.snapshot().replay_armed&&
          game.snapshot().extra_balls_awarded==0&&game.snapshot().replays_used==0,"reset retained earned lives");
}
void wide_clocks_and_validation(){
    const Tick high_only=4294967296LL,maximum=(std::numeric_limits<Tick>::max)();
    GameRules wide(GameRulesConfig(3,high_only,high_only,high_only,high_only,10000,25000));
    wide.skill_gate(0);wide.advance(high_only);
    check(wide.snapshot().save_phase==SavePhase::Flashing,"zero-low-word duration treated as zero");
    wide.advance(high_only*2);check(!wide.snapshot().shoot_again,"wide save timer did not expire");
    GameRules sign(config(),2147483648LL);
    sign.skill_gate(2147483648LL);sign.advance(2147483648LL+5*second);
    check(sign.snapshot().save_phase==SavePhase::Flashing,"positive bit31 time rejected or miscompared");
    GameRules end(config(),maximum-100);
    end.skill_gate(maximum-99);end.advance(maximum);
    check(end.snapshot().shoot_again&&end.until_boundary()==5*second-99,"near-max save deadline overflow");
    check(end.drain(maximum).saved&&end.until_boundary()==1820000000,"near-max drain deadline overflow");
    end.advance(maximum);check(!end.snapshot().ball_active,"near-max clock spuriously served");
    throws<std::invalid_argument>([&]{end.advance(maximum-1);},"backward rules clock accepted");
    throws<std::invalid_argument>([&]{end.advance(-1);},"negative rules clock accepted");
    throws<std::invalid_argument>([]{GameRulesConfig(0,1,1,1,1,0,0);},"zero-ball round accepted");
    throws<std::invalid_argument>([]{GameRulesConfig(3,0,1,1,1,0,0);},"zero drain delay accepted");
    throws<std::invalid_argument>([]{GameRulesConfig(3,1,1,-1,1,0,0);},"negative save delay accepted");
    throws<std::invalid_argument>([]{GameRulesConfig(3,1,1,1,1,0,0,0);},"zero score wrap accepted");
    GameRulesConfig changed=config();changed.feed_delay=0;
    throws<std::invalid_argument>([&]{GameRules invalid(changed);},"mutated invalid config accepted");
}
}
int main(){
    try{tilt_scoring_and_drain();score_and_boundaries();shoot_again_timing();finite_round();reflex_and_skill_clear();multiplier_awards();earned_lives_and_replay();wide_clocks_and_validation();
        std::cout<<"game_rules_tests PASS; default one-player rules, startup immediate policy\n";return 0;
    }catch(const std::exception& error){std::cerr<<"game_rules_tests FAIL: "<<error.what()<<'\n';return 1;}
}

