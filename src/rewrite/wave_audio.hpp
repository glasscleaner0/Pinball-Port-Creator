#ifndef PINBALL_REWRITE_WAVE_AUDIO_HPP
#define PINBALL_REWRITE_WAVE_AUDIO_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace pinball {
namespace audio {

class WaveError : public std::runtime_error {
public:
    WaveError(std::size_t offset, const std::string& message);
    std::size_t offset() const;
private:
    std::size_t offset_;
};

struct WaveFormat {
    std::uint16_t format_tag;
    std::uint16_t channels;
    std::uint32_t sample_rate;
    std::uint32_t byte_rate;
    std::uint16_t block_align;
    std::uint16_t bits_per_sample;
};

struct WaveChunk {
    std::string id; // Exact four bytes, including spaces.
    std::size_t header_offset;
    std::size_t payload_offset;
    std::uint32_t payload_size; // Excludes optional RIFF pad byte.
};

// Integer PCM8/PCM16, mono/stereo only. Original chunks and bytes are owned;
// metadata and extended PCM fmt bytes are retained but never drive playback.
// Exactly one fmt and data chunk are accepted, in either order. Other RIFF
// types, compressed/float/extensible formats and multiple data chunks reject.
class WaveClip {
public:
    static WaveClip parse(std::vector<std::uint8_t> bytes);
    static WaveClip load(const std::string& path);
    const WaveFormat& format() const;
    const std::vector<WaveChunk>& chunks() const;
    const std::vector<std::uint8_t>& bytes() const;
    const std::vector<float>& samples() const; // Interleaved original channels.
    std::uint64_t frame_count() const;
    std::uint64_t duration_numerator() const; // Exact frames/sample_rate seconds.
    std::uint32_t duration_denominator() const;
    float sample(std::uint64_t frame, std::uint16_t channel) const;
private:
    WaveClip();
    WaveFormat format_;
    std::vector<WaveChunk> chunks_;
    std::vector<std::uint8_t> bytes_;
    std::vector<float> samples_;
    std::uint64_t frames_;
};

// An owned, streaming stereo converter; no audio hardware or game event policy.
// Linear interpolation uses a rational phase with denominator output_rate.
// Mono is duplicated to L/R. Nonlooping interpolation holds the last source
// sample until source position reaches frame_count, yielding
// ceil(frame_count*output_rate/source_rate) output frames. Looping interpolates
// the final sample into the first. This policy is new, not reference behavior.
class Resampler {
public:
    explicit Resampler(WaveClip clip, std::uint32_t output_rate = 48000, bool loop = false);
    // Returns produced frames. Writes only 2*returned interleaved float samples;
    // the unproduced tail is untouched. Null is valid only with zero capacity.
    std::size_t read(float* stereo, std::size_t frame_capacity);
    std::vector<float> render(std::size_t frame_capacity);
    void stop(); // No more output until reset().
    void reset();
    bool finished() const;
    bool looping() const;
    std::uint32_t output_rate() const;
    std::uint64_t source_frame() const;
    std::uint32_t phase_numerator() const;
    const WaveClip& clip() const;
private:
    WaveClip clip_;
    std::uint32_t output_rate_;
    bool loop_;
    bool stopped_;
    std::uint64_t source_frame_;
    std::uint32_t phase_;
    void advance();
};

} // namespace audio
} // namespace pinball

#endif
