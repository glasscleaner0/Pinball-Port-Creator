#include "startup_sequence.hpp"
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pinball::startup;
using pinball::runtime::Tick;
const Tick second=1000000000LL;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void rejects(const std::function<void()>& operation){
    try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error("expected invalid startup input rejection");
}
void quiet(const Events& e,const char* message){check(!e.play_feed&&!e.became_playing&&!e.flush_audio,message);}
void phase(const Sequence& sequence,Phase expected,const char* message){
    check(sequence.snapshot().phase==expected,message);
    check(sequence.blocks_gameplay()==(expected!=Phase::Playing),"startup gameplay gate disagrees with phase");
    check(sequence.ball_visible()==(expected==Phase::Playing),"startup ball visibility disagrees with feed-completion gate");
}
void metadata_boundaries(){
    const SequenceConfig c;
    check(c.intro_duration==5020408164LL&&c.feed_delay==960000000&&c.stall_timeout==2*second,
          "original intro metadata, source feed delay or bounded fallback defaults changed");
    Sequence sequence(c);phase(sequence,Phase::Playing,"unstarted sequencer must be an explicit post-start model");
    quiet(sequence.advance(0,false),"initial Playing emitted a phantom startup event");
    sequence.restart(0,false);phase(sequence,Phase::Intro,"restart did not begin Intro");
    check(sequence.snapshot().generation==1&&sequence.snapshot().elapsed==0&&
          sequence.snapshot().remaining==c.intro_duration&&!sequence.snapshot().audio_tracked,
          "fresh disabled-audio startup metadata state");
    quiet(sequence.advance(c.intro_duration-1,false),"metadata intro ended one nanosecond early");
    phase(sequence,Phase::Intro,"ball/gameplay exposed before intro metadata boundary");
    const Events feed=sequence.advance(c.intro_duration,false);
    check(feed.play_feed&&!feed.became_playing&&!feed.flush_audio,"disabled audio boundary must emit one feed without a flush");
    phase(sequence,Phase::FeedDelay,"intro did not start source feed wait");
    check(sequence.snapshot().elapsed==0&&sequence.snapshot().remaining==c.feed_delay,"feed wait inherited intro elapsed time");
    quiet(sequence.advance(c.intro_duration,false),"same-time poll repeated feed event");
    quiet(sequence.advance(c.intro_duration+c.feed_delay-1,false),"feed delay ended one nanosecond early");
    phase(sequence,Phase::FeedDelay,"ball exposed before complete source feed delay");
    const Events play=sequence.advance(c.intro_duration+c.feed_delay,false);
    check(!play.play_feed&&play.became_playing&&!play.flush_audio,"exact feed boundary must start gameplay exactly once");
    phase(sequence,Phase::Playing,"source feed wait did not reveal ball/start game");
    quiet(sequence.advance(c.intro_duration+c.feed_delay,false),"same-time poll repeated Playing event");
    quiet(sequence.advance(c.intro_duration+c.feed_delay+100*second,false),"completed startup replayed feed/Playing events");
}
void external_pause(){
    const SequenceConfig c;const Tick epoch=0x100000000LL+0x7fffffffLL;
    Sequence sequence(c,epoch);sequence.restart(epoch,false);
    Tick now=epoch+c.intro_duration/2;quiet(sequence.advance(now,false),"half intro emitted feed");
    const Snapshot before=sequence.snapshot();now+=30*second;
    quiet(sequence.advance(now,true,AudioState(true,0,100,101)),"pause completed Intro from wall time or audio state");
    check(sequence.snapshot().elapsed==before.elapsed&&sequence.snapshot().remaining==before.remaining,
          "pause aged intro duration");
    now+=before.remaining-1;quiet(sequence.advance(now,false),"resume included paused elapsed time");
    const Events feed=sequence.advance(++now,false);check(feed.play_feed&&!feed.became_playing,"resumed intro missed exact remaining boundary");
    const Snapshot feeding=sequence.snapshot();now+=90*second;
    quiet(sequence.advance(now,true),"pause completed source feed wait");
    check(sequence.snapshot().elapsed==feeding.elapsed&&sequence.snapshot().remaining==c.feed_delay,"pause aged feed delay");
    now+=c.feed_delay-1;quiet(sequence.advance(now,false),"paused feed delay completed early");
    check(sequence.advance(++now,false).became_playing,"resumed feed did not finish at its exact remaining boundary");
}
void accepted_audio_tail(){
    const SequenceConfig c;Sequence sequence(c);
    sequence.restart(0,true,AudioState(true,1,10,9));
    quiet(sequence.advance(second,false,AudioState(true,1,20,18)),"active accepted intro voice completed early");
    quiet(sequence.advance(second+100000000,false,AudioState(true,0,30,29)),"zero mixer voices skipped the queued hardware tail");
    check(sequence.snapshot().audio_tracked&&sequence.snapshot().tail_pending,"finished voice did not snapshot submitted tail");
    quiet(sequence.advance(second+200000000,false,AudioState(true,0,40,30)),"equal played/tail count exposed ball before one full driver frame");
    const Events feed=sequence.advance(second+300000000,false,AudioState(true,0,45,31));
    check(feed.play_feed&&!feed.flush_audio&&!feed.became_playing,"accepted drained audio did not start feed without fallback");
    phase(sequence,Phase::FeedDelay,"accepted tail did not enter feed phase");
    check(sequence.snapshot().elapsed==0&&sequence.snapshot().remaining==c.feed_delay,
          "hardware completion carried elapsed into feed wait or chased newer silent submissions");
    quiet(sequence.advance(second+300000000+c.feed_delay-1,false),"audio-driven feed ended early");
    check(sequence.advance(second+300000000+c.feed_delay,false).became_playing,"audio-driven feed failed to complete");

    Sequence paused(c);paused.restart(0,true,AudioState(true,1,1,0));
    quiet(paused.advance(1000000,false,AudioState(true,0,10,9)),"tail fixture completed without driver proof");
    quiet(paused.advance(100*second,true,AudioState(true,0,20,11)),"paused tail proof emitted feed");
    phase(paused,Phase::Intro,"paused hardware completion exposed ball");
    check(paused.advance(100*second+1,false,AudioState(true,0,21,11)).play_feed,
          "resume lost already-drained accepted tail proof");
}
void audio_counter_wrap(){
    Sequence sequence;
    sequence.restart(0,true,AudioState(true,1,0xfffffffeU,0xfffffffdU));
    quiet(sequence.advance(1000000,false,AudioState(true,0,0xffffffffU,0xfffffffeU)),"wrapping tail fixture completed early");
    quiet(sequence.advance(2000000,false,AudioState(true,0,0,0xffffffffU)),"equal UINT32_MAX tail count counted as passed");
    const Events feed=sequence.advance(3000000,false,AudioState(true,0,1,0));
    check(feed.play_feed&&!feed.flush_audio,"played counter wrap did not complete accepted hardware tail");

    Sequence ambiguous;ambiguous.restart(0,true,AudioState(true,1,0,0));
    quiet(ambiguous.advance(1,false,AudioState(true,0,0,0x80000000U)),"half-range counter distance was accepted as forward tail proof");
    check(ambiguous.advance(2,false,AudioState(true,0,0,1)).play_feed,"unambiguous forward hardware count did not pass tail");
}
void failed_disabled_and_stalled_audio(){
    const SequenceConfig c;
    Sequence disabled(c);disabled.restart(0,false,AudioState(true,20,5,0));
    quiet(disabled.advance(c.intro_duration-1,false,AudioState(true,0,50,60)),"unaccepted audio bypassed metadata duration");
    const Events disabled_feed=disabled.advance(c.intro_duration,false);
    check(disabled_feed.play_feed&&!disabled_feed.flush_audio,"unaccepted/disabled audio failed metadata feed or requested an unrelated flush");

    Sequence failed(c);failed.restart(0,true,AudioState(true,1,5,4));
    quiet(failed.advance(1000000,false,AudioState(false,1,5,4)),"failed accepted output skipped metadata fallback duration");
    check(!failed.snapshot().audio_tracked,"failed device remained tracked");
    const Events fallback=failed.advance(c.intro_duration,false);
    check(fallback.play_feed&&fallback.flush_audio&&!fallback.became_playing,"failed accepted intro must flush before feed");
    quiet(failed.advance(c.intro_duration,false),"accepted failure emitted a repeated flush/feed");

    Sequence already_failed(c);already_failed.restart(0,true,AudioState(false,1,0,0));
    const Events immediate_fallback=already_failed.advance(c.intro_duration,false);
    check(immediate_fallback.play_feed&&immediate_fallback.flush_audio,"accepted but unavailable output missed fallback flush");

    Sequence stalled(c);stalled.restart(0,true,AudioState(true,1,9,8));
    quiet(stalled.advance(c.stall_timeout-1,false,AudioState(true,1,9,8)),"stall fallback fired before two-second bound");
    check(stalled.snapshot().audio_tracked,"tracking disabled before stall threshold");
    quiet(stalled.advance(c.stall_timeout,false,AudioState(true,1,9,8)),"stall fallback fed before intro metadata duration");
    check(!stalled.snapshot().audio_tracked,"no-progress hardware tracking did not stop at stall threshold");
    const Events stalled_feed=stalled.advance(c.intro_duration,false);
    check(stalled_feed.play_feed&&stalled_feed.flush_audio,"stalled accepted voice did not flush/feed at metadata duration");

    Sequence permanently_active(c);permanently_active.restart(0,true,AudioState(true,1,1,0));
    for(unsigned seconds=1;seconds<=6;++seconds)
        quiet(permanently_active.advance(Tick(seconds)*second,false,AudioState(true,1,seconds+1,seconds)),
              "progressing active voice escaped before hard intro bound");
    const Tick hard=c.intro_duration+c.stall_timeout;
    quiet(permanently_active.advance(hard-1,false,AudioState(true,1,20,19)),"hard startup bound fired one nanosecond early");
    const Events hard_feed=permanently_active.advance(hard,false,AudioState(true,1,21,20));
    check(hard_feed.play_feed&&hard_feed.flush_audio,"permanently active voice did not hit bounded fallback");
}
void late_dispatch_restart_and_bypass(){
    const SequenceConfig c;Sequence sequence(c);sequence.restart(0,false);
    const Tick late=c.intro_duration+100*c.feed_delay;
    const Events feed=sequence.advance(late,false);
    check(feed.play_feed&&!feed.became_playing&&sequence.snapshot().elapsed==0,
          "late intro dispatch skipped the audible feed phase");
    quiet(sequence.advance(late+c.feed_delay-1,false),"late intro carried old elapsed into source feed delay");
    check(sequence.advance(late+c.feed_delay,false).became_playing,"late dispatched feed did not complete");

    Sequence restarted(c);restarted.restart(0,true,AudioState(true,1,5,4));
    quiet(restarted.advance(1000000,false,AudioState(true,0,10,9)),"restart fixture did not retain pending tail");
    const std::uint32_t first=restarted.snapshot().generation;
    restarted.restart(2000000,true,AudioState(true,1,100,99));
    check(restarted.snapshot().generation==first+1&&restarted.snapshot().elapsed==0&&!restarted.snapshot().tail_pending,
          "Y restart retained old intro progress/tail generation");
    quiet(restarted.advance(3000000,false,AudioState(true,1,101,100)),"old tail completion started a newly restarted intro");
    quiet(restarted.advance(4000000,false,AudioState(true,0,102,101)),"new intro voice skipped its own submitted tail");
    check(restarted.advance(5000000,false,AudioState(true,0,103,103)).play_feed,"new intro generation did not feed once drained");
    restarted.restart(6000000,false);
    quiet(restarted.advance(6000000+c.feed_delay,false),"old feed callback completed a restarted intro");
    phase(restarted,Phase::Intro,"restart during feed did not restore Intro");
    check(restarted.advance(6000000+c.intro_duration,false).play_feed,"restarted metadata intro did not emit its own feed");
    const std::uint32_t generation=restarted.snapshot().generation;
    restarted.bypass(6000000+c.intro_duration);phase(restarted,Phase::Playing,"explicit smoke bypass did not enter post-start state");
    check(restarted.snapshot().generation==generation&&!restarted.snapshot().audio_tracked&&!restarted.snapshot().tail_pending,
          "bypass changed generation or retained hardware tail state");
    quiet(restarted.advance(6000000+c.intro_duration+100*second,false),"bypass leaked a stale intro/feed event");
}
void validation_and_clock_extremes(){
    rejects([]{SequenceConfig c(0,1,1);});rejects([]{SequenceConfig c(1,0,1);});rejects([]{SequenceConfig c(1,1,0);});
    rejects([]{SequenceConfig c(-1,1,1);});rejects([]{SequenceConfig c(1,-1,1);});
    rejects([]{SequenceConfig c((std::numeric_limits<Tick>::max)(),1,1);});
    rejects([]{SequenceConfig c;c.feed_delay=0;Sequence s(c);});
    rejects([]{Sequence s(SequenceConfig(),-1);});
    Sequence sequence;sequence.restart(10,false);const Snapshot before=sequence.snapshot();
    rejects([&]{sequence.advance(9,false);});rejects([&]{sequence.restart(9,false);});rejects([&]{sequence.bypass(9);});
    rejects([&]{sequence.advance(-1,false);});
    check(sequence.snapshot().generation==before.generation&&sequence.snapshot().elapsed==before.elapsed&&
          sequence.snapshot().phase==before.phase,"invalid clocks partially mutated startup state");

    Sequence word_boundaries(SequenceConfig(0x100000000LL,0x80000000LL,1));word_boundaries.restart(0,false);
    quiet(word_boundaries.advance(0xffffffffLL,false),"exact2^32 intro ended on a truncated word");
    check(word_boundaries.advance(0x100000000LL,false).play_feed,"exact2^32 duration was treated as zero");
    quiet(word_boundaries.advance(0x17fffffffLL,false),"exact2^31 feed duration ended early");
    check(word_boundaries.advance(0x180000000LL,false).became_playing,"exact2^31 feed failed across high-word boundary");

    const Tick maximum=(std::numeric_limits<Tick>::max)();const Tick epoch=maximum-15;
    Sequence near_limit(SequenceConfig(10,5,2),epoch);near_limit.restart(epoch,false);
    quiet(near_limit.advance(epoch+9,false),"near-max intro ended early");
    check(near_limit.advance(epoch+10,false).play_feed,"near-max intro deadline overflowed");
    quiet(near_limit.advance(maximum-1,false),"near-max source feed ended early");
    check(near_limit.advance(maximum,false).became_playing,"near-max source feed could not complete");

    Sequence huge(SequenceConfig(maximum-1,1,1));huge.restart(0,false);
    quiet(huge.advance(maximum-2,false),"huge positive intro used a truncated32-bit clock");
    check(huge.advance(maximum-1,false).play_feed&&huge.advance(maximum,false).became_playing,
          "huge positive intro/feed elapsed arithmetic overflowed");
}
}
int main(){
    try{
        metadata_boundaries();external_pause();accepted_audio_tail();audio_counter_wrap();
        failed_disabled_and_stalled_audio();late_dispatch_restart_and_bypass();validation_and_clock_extremes();
        std::cout<<"startup_sequence tests PASS: intro/audio-tail/fallback/feed ordering, pause/restart/stale cleanup and clock bounds\n";return 0;
    }catch(const std::exception& e){std::cerr<<"startup_sequence tests FAIL: "<<e.what()<<'\n';return 1;}
}
