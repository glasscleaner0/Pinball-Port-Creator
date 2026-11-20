#include "wave_audio.hpp"

#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace pinball {
namespace audio {
namespace {

std::string error_text(std::size_t offset, const std::string& message) {
    std::ostringstream text;
    text << message << " at byte " << offset;
    return text.str();
}

void require_bytes(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t count) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        throw WaveError(offset, "truncated RIFF/WAVE data");
    }
}

std::uint16_t u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    require_bytes(bytes, offset, 2);
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

std::uint32_t u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    require_bytes(bytes, offset, 4);
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::string fourcc(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    require_bytes(bytes, offset, 4);
    return std::string(bytes.begin() + offset, bytes.begin() + offset + 4);
}

void stereo_extent(std::size_t frames) {
    if (frames > std::numeric_limits<std::size_t>::max() / (2 * sizeof(float))) {
        throw std::overflow_error("stereo output byte extent overflows size_t");
    }
}

} // namespace

WaveError::WaveError(std::size_t offset, const std::string& message)
    : std::runtime_error(error_text(offset, message)), offset_(offset) {}
std::size_t WaveError::offset() const { return offset_; }

WaveClip::WaveClip() : format_(), frames_(0) {}

WaveClip WaveClip::parse(std::vector<std::uint8_t> bytes) {
    require_bytes(bytes, 0, 12);
    if (fourcc(bytes, 0) != "RIFF") throw WaveError(0, "little-endian RIFF required");
    if (fourcc(bytes, 8) != "WAVE") throw WaveError(8, "WAVE form required");
    const std::uint64_t declared_size = static_cast<std::uint64_t>(u32(bytes, 4)) + 8;
    if (declared_size != bytes.size()) throw WaveError(4, "RIFF size disagrees with input length");
    WaveClip result;
    result.bytes_ = std::move(bytes);
    const std::vector<std::uint8_t>& input = result.bytes_;
    bool have_format = false;
    bool have_data = false;
    std::size_t format_chunk = 0;
    std::size_t data_chunk = 0;
    std::size_t position = 12;
    while (position < input.size()) {
        require_bytes(input, position, 8);
        WaveChunk chunk;
        chunk.header_offset = position;
        chunk.id = fourcc(input, position);
        chunk.payload_size = u32(input, position + 4);
        chunk.payload_offset = position + 8;
        require_bytes(input, chunk.payload_offset, chunk.payload_size);
        if (chunk.id == "fmt ") {
            if (have_format) throw WaveError(position, "multiple fmt chunks are unsupported");
            have_format = true;
            format_chunk = result.chunks_.size();
        } else if (chunk.id == "data") {
            if (have_data) throw WaveError(position, "multiple data chunks are unsupported");
            have_data = true;
            data_chunk = result.chunks_.size();
        }
        result.chunks_.push_back(chunk);
        position = chunk.payload_offset + chunk.payload_size;
        if ((chunk.payload_size & 1u) != 0) {
            require_bytes(input, position, 1);
            ++position; // Pad value is not interpreted; its byte stays owned.
        }
    }
    if (!have_format) throw WaveError(12, "required fmt chunk missing");
    if (!have_data) throw WaveError(12, "required data chunk missing");
    const WaveChunk& format = result.chunks_[format_chunk];
    if (format.payload_size < 16) throw WaveError(format.payload_offset, "PCM fmt header is truncated");
    WaveFormat& decoded = result.format_;
    decoded.format_tag = u16(input, format.payload_offset);
    decoded.channels = u16(input, format.payload_offset + 2);
    decoded.sample_rate = u32(input, format.payload_offset + 4);
    decoded.byte_rate = u32(input, format.payload_offset + 8);
    decoded.block_align = u16(input, format.payload_offset + 12);
    decoded.bits_per_sample = u16(input, format.payload_offset + 14);
    if (decoded.format_tag != 1) throw WaveError(format.payload_offset, "only integer PCM format is supported");
    if (decoded.channels != 1 && decoded.channels != 2) {
        throw WaveError(format.payload_offset + 2, "only mono and stereo are supported");
    }
    if (decoded.sample_rate == 0) throw WaveError(format.payload_offset + 4, "sample rate must be positive");
    if (decoded.bits_per_sample != 8 && decoded.bits_per_sample != 16) {
        throw WaveError(format.payload_offset + 14, "only PCM8 and PCM16 samples are supported");
    }
    const std::uint16_t expected_align = static_cast<std::uint16_t>(
        decoded.channels * (decoded.bits_per_sample / 8));
    if (decoded.block_align != expected_align) {
        throw WaveError(format.payload_offset + 12, "PCM block alignment is inconsistent");
    }
    const std::uint64_t expected_rate = static_cast<std::uint64_t>(decoded.sample_rate) * expected_align;
    if (expected_rate > std::numeric_limits<std::uint32_t>::max() || decoded.byte_rate != expected_rate) {
        throw WaveError(format.payload_offset + 8, "PCM byte rate is inconsistent or unrepresentable");
    }
    const WaveChunk& data = result.chunks_[data_chunk];
    if (data.payload_size % decoded.block_align != 0) {
        throw WaveError(data.payload_offset, "PCM data ends inside a frame");
    }
    result.frames_ = data.payload_size / decoded.block_align;
    if (result.frames_ > result.samples_.max_size() / decoded.channels) {
        throw WaveError(data.payload_offset, "decoded PCM sample count exceeds host limits");
    }
    const std::size_t sample_count = static_cast<std::size_t>(result.frames_) * decoded.channels;
    result.samples_.reserve(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i) {
        if (decoded.bits_per_sample == 8) {
            const int value = static_cast<int>(input[data.payload_offset + i]) - 128;
            result.samples_.push_back(static_cast<float>(value) / 128.0f);
        } else {
            const std::uint16_t bits = u16(input, data.payload_offset + i * 2);
            const std::int32_t value = bits < 32768u ? bits : static_cast<std::int32_t>(bits) - 65536;
            result.samples_.push_back(static_cast<float>(value) / 32768.0f);
        }
    }
    return result;
}

WaveClip WaveClip::load(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open WAV file: " + path);
    const std::streamoff length = input.tellg();
    const std::uintmax_t maximum_riff = static_cast<std::uintmax_t>(std::numeric_limits<std::uint32_t>::max()) + 8;
    if (length < 0 || static_cast<std::uintmax_t>(length) > maximum_riff ||
        static_cast<std::uintmax_t>(length) > std::numeric_limits<std::size_t>::max() ||
        static_cast<std::uintmax_t>(length) > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("WAV file size exceeds supported limits: " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
        if (!input) throw std::runtime_error("cannot read complete WAV file: " + path);
    }
    return parse(std::move(bytes));
}

const WaveFormat& WaveClip::format() const { return format_; }
const std::vector<WaveChunk>& WaveClip::chunks() const { return chunks_; }
const std::vector<std::uint8_t>& WaveClip::bytes() const { return bytes_; }
const std::vector<float>& WaveClip::samples() const { return samples_; }
std::uint64_t WaveClip::frame_count() const { return frames_; }
std::uint64_t WaveClip::duration_numerator() const { return frames_; }
std::uint32_t WaveClip::duration_denominator() const { return format_.sample_rate; }
float WaveClip::sample(std::uint64_t frame, std::uint16_t channel) const {
    if (frame >= frames_ || channel >= format_.channels) throw std::out_of_range("PCM frame or channel index");
    return samples_.at(static_cast<std::size_t>(frame) * format_.channels + channel);
}

Resampler::Resampler(WaveClip clip, std::uint32_t output_rate, bool loop)
    : clip_(std::move(clip)), output_rate_(output_rate), loop_(loop), stopped_(false),
      source_frame_(0), phase_(0) {
    if (output_rate == 0) throw std::invalid_argument("output sample rate must be positive");
    if (clip_.format().sample_rate == 0 || clip_.format().channels == 0 ||
        clip_.frame_count() > clip_.samples().max_size() / clip_.format().channels ||
        clip_.frame_count() * clip_.format().channels != clip_.samples().size()) {
        throw std::invalid_argument("resampler requires an intact owned PCM clip");
    }
}

void Resampler::advance() {
    const std::uint64_t numerator = static_cast<std::uint64_t>(phase_) + clip_.format().sample_rate;
    std::uint64_t step = numerator / output_rate_;
    phase_ = static_cast<std::uint32_t>(numerator % output_rate_);
    const std::uint64_t frames = clip_.frame_count();
    if (loop_) {
        step %= frames; // Only called after finished() ruled out an empty clip.
        const std::uint64_t remaining = frames - source_frame_;
        if (step >= remaining) source_frame_ = step - remaining;
        else source_frame_ += step;
    } else if (step >= frames - source_frame_) {
        source_frame_ = frames;
        phase_ = 0;
    } else source_frame_ += step;
}

std::size_t Resampler::read(float* stereo, std::size_t frame_capacity) {
    stereo_extent(frame_capacity);
    if (!stereo && frame_capacity != 0) throw std::invalid_argument("nonempty audio output requires a destination");
    std::size_t produced = 0;
    while (produced < frame_capacity && !finished()) {
        std::uint64_t next = source_frame_ + 1;
        if (next == clip_.frame_count()) next = loop_ ? 0 : source_frame_;
        const double fraction = static_cast<double>(phase_) / output_rate_;
        for (std::uint16_t channel = 0; channel < 2; ++channel) {
            const std::uint16_t source_channel = clip_.format().channels == 1 ? 0 : channel;
            const double first = clip_.sample(source_frame_, source_channel);
            const double second = clip_.sample(next, source_channel);
            stereo[produced * 2 + channel] = static_cast<float>(first + (second - first) * fraction);
        }
        ++produced;
        advance();
    }
    return produced;
}

std::vector<float> Resampler::render(std::size_t frame_capacity) {
    stereo_extent(frame_capacity);
    std::vector<float> output;
    if (frame_capacity > output.max_size() / 2) throw std::length_error("audio output exceeds vector limits");
    output.resize(frame_capacity * 2);
    const std::size_t produced = read(output.empty() ? NULL : &output[0], frame_capacity);
    output.resize(produced * 2);
    return output;
}

void Resampler::stop() { stopped_ = true; }
void Resampler::reset() { stopped_ = false; source_frame_ = 0; phase_ = 0; }
bool Resampler::finished() const {
    return stopped_ || clip_.frame_count() == 0 || (!loop_ && source_frame_ >= clip_.frame_count());
}
bool Resampler::looping() const { return loop_; }
std::uint32_t Resampler::output_rate() const { return output_rate_; }
std::uint64_t Resampler::source_frame() const { return source_frame_; }
std::uint32_t Resampler::phase_numerator() const { return phase_; }
const WaveClip& Resampler::clip() const { return clip_; }

} // namespace audio
} // namespace pinball
