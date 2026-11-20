#ifndef PINBALL_REWRITE_STARTUP_SEQUENCE_HPP
#define PINBALL_REWRITE_STARTUP_SEQUENCE_HPP
#include "runtime.hpp"
#include <cstddef>
#include <cstdint>
namespace pinball { namespace startup {
enum class Phase { Intro,FeedDelay,Playing };
struct AudioState {
    bool running;
    std::size_t active_voices;
    std::uint32_t submitted,played;
    AudioState(bool ready=false,std::size_t voices=0,std::uint32_t queued=0,std::uint32_t consumed=0)
        :running(ready),active_voices(voices),submitted(queued),played(consumed) {}
};
struct SequenceConfig {
    runtime::Tick intro_duration,feed_delay,stall_timeout;
    SequenceConfig(runtime::Tick intro=5020408164LL,runtime::Tick feed=960000000LL,
                   runtime::Tick stalled=2000000000LL);
};
struct Events {
    bool play_feed,became_playing,flush_audio;
    Events():play_feed(false),became_playing(false),flush_audio(false) {}
};
struct Snapshot {
    Phase phase;
    runtime::Tick elapsed,remaining;
    std::uint32_t generation;
    bool audio_tracked,tail_pending;
};
// Frontend-only startup ordering; the simulation clock does not advance here.
// restart follows a canceled/flushed old audio generation, and the supplied
// accepted flag means this generation's exclusive intro voice was accepted.
// Normal completion waits for zero mixer voices, snapshots submitted tail,
// then waits until played passes that tail (one whole driver frame later).
// Kernel consumption is observed; audible speaker latency remains unknown.
// Disabled/failed audio uses the metadata duration. A2s lack of progress
// disables hardware tracking; an intro_duration+2s hard limit also bounds
// permanently active voices. Fallback after an accepted voice requests an
// audio flush before Feed. No already elapsed time carries into FeedDelay:
// its timer begins when play_feed is delivered to the frontend.
// External pause freezes every transition/timer; startup itself must NOT
// pause audio. Initial state is Playing until restart is explicitly called.
class Sequence {
public:
    explicit Sequence(const SequenceConfig& config=SequenceConfig(),runtime::Tick now=0);
    void restart(runtime::Tick now,bool intro_accepted,const AudioState& audio=AudioState());
    void bypass(runtime::Tick now);
    Events advance(runtime::Tick now,bool externally_paused,const AudioState& audio=AudioState());
    bool blocks_gameplay() const;
    bool ball_visible() const;
    Snapshot snapshot() const;
private:
    SequenceConfig config_;
    runtime::Tick now_,elapsed_,progress_at_;
    std::uint32_t generation_,submitted_,played_,tail_;
    Phase phase_;
    bool accepted_,tracking_,tail_pending_;
    void validate_now(runtime::Tick now) const;
};
} }
#endif
