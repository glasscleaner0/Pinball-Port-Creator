#ifndef PINBALL_REWRITE_XBOX_AUDIO_OUTPUT_HPP
#define PINBALL_REWRITE_XBOX_AUDIO_OUTPUT_HPP
#include "voice_mixer.hpp"
#include <memory>
namespace pinball { namespace audio {
struct AudioOutputStats {
    bool running;
    std::uint32_t last_status,submitted,played,silent,fewest_queued,longest_wait_us;
    MixerStats mixer;
};
// Title-owned feeder over the pinned xsdk public audio/thread APIs. No SDK
// implementation or sample code is reused. Hardware calls exist only in
// __XSDK__ builds; other builds report unsupported and can still test mixing.
// Output is best effort: failed open/thread/device disables sound, not gameplay.
// Missing clips remain silent. Original WAV-derived game timers are independent.
class XboxAudioOutput {
public:
    explicit XboxAudioOutput(std::size_t voices=16);
    ~XboxAudioOutput();
    XboxAudioOutput(const XboxAudioOutput&)=delete;
    XboxAudioOutput& operator=(const XboxAudioOutput&)=delete;
    bool start(std::uint32_t frames_ahead=8,std::uint32_t hardware_thread=5);
    void stop();
    VoiceId play(const std::shared_ptr<const StereoClip>& clip,float gain=1,float pan=0,bool loop=false);
    bool stop_voice(VoiceId voice);
    void stop_all();
    void set_paused(bool paused);
    void set_master_gain(float gain);
    AudioOutputStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} }
#endif
