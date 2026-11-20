#ifndef PINBALL_REWRITE_VOICE_MIXER_HPP
#define PINBALL_REWRITE_VOICE_MIXER_HPP
#include "wave_audio.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
namespace pinball { namespace audio {
class StereoClip {
public:
    static std::shared_ptr<const StereoClip> from_wave(WaveClip clip,std::uint32_t rate=48000,
                                                       std::size_t max_frames=2880000);
    static std::shared_ptr<const StereoClip> load_optional(const std::string& path,std::uint32_t rate=48000,
                                                          std::size_t max_frames=2880000);
    static std::shared_ptr<const StereoClip> create(std::vector<float> stereo,std::uint32_t rate=48000);
    const std::vector<float>& samples() const;
    std::size_t frames() const;
    std::uint32_t sample_rate() const;
private:
    StereoClip(std::vector<float> stereo,std::uint32_t rate);
    std::vector<float> stereo_;
    std::uint32_t rate_;
};
typedef std::uint64_t VoiceId;
struct MixerStats {
    std::size_t active_voices;
    std::uint64_t started,dropped,ignored,clipped_samples,rendered_frames;
};
// Fresh bounded PCM mixer, not a DirectSound behavior emulation. Source clips
// are immutable and converted before playback. No file access or allocation
// occurs in render. Full voice slots drop a new request without stealing.
// Gain/pan are explicit caller policy; stereo balance is linear. Final samples
// are hard-clamped to[-1,1]. Null/missing/empty clips are silent requests.
// The mixer is single-owner; an output adapter supplies synchronization.
class VoiceMixer {
public:
    explicit VoiceMixer(std::uint32_t rate=48000,std::size_t voices=16,std::size_t maximum_block=4096);
    VoiceId play(const std::shared_ptr<const StereoClip>& clip,float gain=1,float pan=0,bool loop=false);
    bool stop(VoiceId voice);
    void stop_all();
    void set_paused(bool paused);
    void set_master_gain(float gain);
    void render(float* interleaved_stereo,std::size_t frames);
    MixerStats stats() const;
    std::uint32_t sample_rate() const;
private:
    struct Voice {
        VoiceId id;
        std::shared_ptr<const StereoClip> clip;
        std::size_t cursor;
        float left,right;
        bool loop;
        Voice():id(0),clip(),cursor(0),left(0),right(0),loop(false) {}
    };
    std::uint32_t rate_;
    std::size_t maximum_block_;
    std::vector<Voice> voices_;
    VoiceId sequence_;
    bool paused_;
    float master_;
    MixerStats stats_;
};
// SDK contract conversion: six contiguous planes, first L then R, four silent.
// Caller supplies capacity in floats; source has2*frames interleaved values.
// Source and destination must not overlap.
void pack_stereo_planar6(const float* stereo,std::size_t frames,float* planar,std::size_t float_capacity);
} }
#endif

