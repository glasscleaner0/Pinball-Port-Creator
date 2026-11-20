#include "wave_audio.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using pinball::audio::WaveClip;
using pinball::audio::Resampler;
typedef std::vector<std::uint8_t> Bytes;
struct Chunk {
    std::string id;
    Bytes data;
    Chunk(const std::string& name, Bytes value) : id(name), data(std::move(value)) {}
};

void check(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); }
void close(float actual, float expected, const char* label) {
    check(std::isfinite(actual) && std::fabs(actual - expected) < 0.000001f, label);
}
void put16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}
void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
Bytes fmt(std::uint16_t channels = 1, std::uint16_t bits = 8, std::uint32_t rate = 11025) {
    Bytes bytes(16, 0);
    put16(bytes, 0, 1); put16(bytes, 2, channels); put32(bytes, 4, rate);
    put32(bytes, 8, rate * channels * (bits / 8));
    put16(bytes, 12, static_cast<std::uint16_t>(channels * (bits / 8))); put16(bytes, 14, bits);
    return bytes;
}
Bytes riff(const std::vector<Chunk>& chunks) {
    Bytes bytes{'R','I','F','F',0,0,0,0,'W','A','V','E'};
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        check(chunks[i].id.size() == 4, "fixture FOURCC length");
        bytes.insert(bytes.end(), chunks[i].id.begin(), chunks[i].id.end());
        const std::size_t at = bytes.size(); bytes.resize(at + 4);
        put32(bytes, at, static_cast<std::uint32_t>(chunks[i].data.size()));
        bytes.insert(bytes.end(), chunks[i].data.begin(), chunks[i].data.end());
        if (chunks[i].data.size() & 1) bytes.push_back(0xa5);
    }
    put32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8));
    return bytes;
}
WaveClip clip8(const Bytes& data, std::uint32_t rate = 11025) {
    return WaveClip::parse(riff({Chunk("fmt ", fmt(1, 8, rate)), Chunk("data", data)}));
}
void rejects(const std::function<void()>& action, const char* label) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(label);
}

void decoding_and_ownership() {
    Bytes format = fmt(); format.push_back(0x77); // PCM extension retained, uninterpreted.
    const Bytes bytes = riff({Chunk("JUNK", Bytes{9,8,7}), Chunk("data", Bytes{0,128,255}),
        Chunk("fmt ", format), Chunk("LIST", Bytes{1,2})});
    const WaveClip clip = WaveClip::parse(bytes);
    check(clip.format().format_tag == 1 && clip.format().channels == 1 &&
        clip.format().sample_rate == 11025 && clip.format().bits_per_sample == 8,
        "PCM header decoding");
    check(clip.frame_count() == 3 && clip.duration_numerator() == 3 &&
        clip.duration_denominator() == 11025, "exact rational duration");
    close(clip.sample(0,0), -1, "unsigned8 minimum");
    close(clip.sample(1,0), 0, "unsigned8 zero");
    close(clip.sample(2,0), 127.0f/128, "unsigned8 maximum");
    check(clip.bytes() == bytes && clip.chunks().size() == 4 &&
        clip.chunks()[0].id == "JUNK" && clip.chunks()[0].payload_offset == 20 &&
        clip.chunks()[0].payload_size == 3 && clip.chunks()[1].header_offset == 24,
        "unknown chunk offsets and odd padding retained");
    WaveClip copied = clip; WaveClip moved = std::move(copied);
    check(moved.bytes() == bytes && moved.samples() == clip.samples(), "owned clip copy and move");
    rejects([&clip]() { clip.sample(3,0); }, "out-of-range frame accepted");
    rejects([&clip]() { clip.sample(0,1); }, "out-of-range channel accepted");
    const WaveClip stereo = WaveClip::parse(riff({Chunk("fmt ",fmt(2,16,48000)),
        Chunk("data",Bytes{0,128,255,127,0,0,0,64})}));
    check(stereo.frame_count() == 2 && stereo.samples().size() == 4, "stereo PCM frame count");
    close(stereo.sample(0,0),-1,"signed16 minimum");
    close(stereo.sample(0,1),32767.0f/32768,"signed16 maximum");
    close(stereo.sample(1,0),0,"signed16 zero");close(stereo.sample(1,1),0.5f,"little-endian signed16");
    Resampler same(stereo,48000);
    check(same.render(20) == stereo.samples(), "stereo resampling retains channel order at equal rate");
}

void malformed_chunks_and_formats() {
    const Bytes valid = riff({Chunk("fmt ",fmt()),Chunk("data",Bytes{0,1,2})});
    for (std::size_t n = 0; n < valid.size(); ++n) {
        Bytes prefix(valid.begin(), valid.begin() + n);
        if (n >= 12) put32(prefix,4,static_cast<std::uint32_t>(n - 8));
        rejects([&prefix]() { WaveClip::parse(prefix); }, "truncated WAV prefix accepted");
    }
    Bytes bad = valid; bad[0] = 'X';
    rejects([&bad]() { WaveClip::parse(bad); }, "non-RIFF form accepted");
    bad = valid; bad[8] = 'X';
    rejects([&bad]() { WaveClip::parse(bad); }, "non-WAVE RIFF accepted");
    bad = valid; put32(bad,4,0xffffffffu);
    rejects([&bad]() { WaveClip::parse(bad); }, "RIFF length mismatch accepted");
    bad = valid; put32(bad,16,0xffffffffu);
    rejects([&bad]() { WaveClip::parse(bad); }, "oversized chunk accepted");
    rejects([]() { WaveClip::parse(riff({Chunk("fmt ",fmt())})); }, "missing data accepted");
    rejects([]() { WaveClip::parse(riff({Chunk("data",Bytes())})); }, "missing fmt accepted");
    rejects([]() { WaveClip::parse(riff({Chunk("fmt ",fmt()),Chunk("fmt ",fmt()),
        Chunk("data",Bytes())})); }, "multiple fmt chunks accepted");
    rejects([]() { WaveClip::parse(riff({Chunk("fmt ",fmt()),Chunk("data",Bytes()),
        Chunk("data",Bytes())})); }, "multiple data chunks accepted");
    rejects([]() { WaveClip::parse(riff({Chunk("fmt ",Bytes(15)),Chunk("data",Bytes())})); },
        "short fmt header accepted");
    for (int field = 0; field < 6; ++field) {
        Bytes format = fmt();
        if (field == 0) put16(format,0,3); // IEEE float deliberately unsupported.
        if (field == 1) put16(format,2,3);
        if (field == 2) put32(format,4,0);
        if (field == 3) put32(format,8,1);
        if (field == 4) put16(format,12,2);
        if (field == 5) put16(format,14,24);
        const Bytes input = riff({Chunk("fmt ",format),Chunk("data",Bytes())});
        rejects([&input]() { WaveClip::parse(input); }, "unsupported or inconsistent PCM format accepted");
    }
    rejects([]() { WaveClip::parse(riff({Chunk("fmt ",fmt(2,16,48000)),
        Chunk("data",Bytes{1,2,3})})); }, "partial stereo PCM frame accepted");
    rejects([]() { WaveClip::parse(riff({Chunk("fmt ",fmt(2,16,0xffffffffu)),
        Chunk("data",Bytes())})); }, "overflowing PCM byte rate accepted");
    check(clip8(Bytes()).frame_count() == 0, "empty PCM data is valid");
}

std::vector<float> chunked(Resampler& source, std::size_t total) {
    std::vector<float> result;
    const std::size_t blocks[] = {1,7,2,19,5};
    std::size_t emitted = 0;
    std::size_t index = 0;
    while (emitted < total && !source.finished()) {
        std::size_t count = blocks[index++ % 5];
        if (count > total - emitted) count = total - emitted;
        const std::vector<float> next = source.render(count);
        emitted += next.size() / 2;
        result.insert(result.end(),next.begin(),next.end());
    }
    return result;
}

void streaming_resampler() {
    const WaveClip clip = clip8(Bytes{0,128,255,64},2);
    Resampler up(clip,4);
    const std::vector<float> output = up.render(100);
    const float expected[] = {-1,-0.5f,0,0.49609375f,0.9921875f,0.24609375f,-0.5f,-0.5f};
    check(output.size() == 16 && up.finished(), "nonlooping output frame extent");
    for (std::size_t i = 0; i < 8; ++i) {
        close(output[i*2],expected[i],"linear interpolation or final-sample hold");
        close(output[i*2+1],expected[i],"mono duplication to stereo");
    }
    check(up.render(1).empty(), "finished source emitted more samples");
    up.reset(); check(!up.finished() && up.source_frame() == 0 && up.phase_numerator() == 0,
        "reset restores initial rational phase");
    up.stop(); check(up.finished() && up.render(5).empty(), "stop does not emit samples");
    up.reset(); check(up.render(100) == output, "reset reproduces identical output");

    const WaveClip irregular = clip8(Bytes{0,8,93,128,207,255,64},11025);
    Resampler whole(irregular,48000), blocks(irregular,48000);
    const std::vector<float> one_shot = whole.render(1000);
    check(chunked(blocks,1000) == one_shot && blocks.finished(),
        "chunked upsampling differs from one-shot samples");
    check(one_shot.size()/2 == (7ULL*48000+11024)/11025,
        "rational source duration produces expected ceiling output frames");
    Resampler down(clip8(Bytes{0,10,20,30,40,50,60,70,80,90,100},5),3);
    Resampler down_blocks(down);
    check(down.render(100) == chunked(down_blocks,100), "chunked downsampling differs from one-shot");

    Resampler looping(clip8(Bytes{0,255},2),4,true);
    const std::vector<float> loop = looping.render(12);
    const float cycle[] = {-1,-0.00390625f,0.9921875f,-0.00390625f};
    for (std::size_t i = 0; i < 12; ++i) close(loop[i*2],cycle[i%4],"loop boundary interpolation");
    check(!looping.finished(), "nonempty looping clip finished");
    Resampler loop_whole(irregular,48000,true), loop_blocks(irregular,48000,true);
    check(loop_whole.render(257) == chunked(loop_blocks,257), "loop chunk boundary lost rational phase");
    check(loop_whole.source_frame() == loop_blocks.source_frame() &&
        loop_whole.phase_numerator() == loop_blocks.phase_numerator(), "loop phases diverged");

    Resampler skipped(clip8(Bytes{0,128,255},100),1,true);
    const std::vector<float> skip = skipped.render(6);
    const float skipped_expected[] = {-1,0,127.0f/128};
    for (std::size_t i = 0; i < 6; ++i) close(skip[i*2],skipped_expected[i%3],"multi-loop source advance");
    Resampler extreme(clip8(Bytes{0,128,255,64,32},0xffffffffu),0xfffffffeu,true);
    extreme.render(2);
    check(extreme.source_frame() == 2 && extreme.phase_numerator() == 2,
        "rational phase numerator overflowed 32-bit addition");
    Resampler empty(clip8(Bytes()),48000,true);
    check(empty.finished() && empty.render(10).empty(), "empty loop did not terminate safely");
    Resampler singleton(clip8(Bytes{255},1),4,true);
    const std::vector<float> single = singleton.render(10);
    for (std::size_t i = 0; i < single.size(); ++i) close(single[i],127.0f/128,"single-frame loop");

    Resampler tail(clip8(Bytes{0,128,255},3),3);
    float destination[16]; for (std::size_t i = 0; i < 16; ++i) destination[i] = 9;
    check(tail.read(destination,8) == 3,"read returns produced frame count");
    for (std::size_t i = 6; i < 16; ++i) check(destination[i] == 9,"read overwrote unproduced output tail");
    check(tail.read(NULL,0) == 0,"zero-sized null output accepted without advancing");
    rejects([&tail]() { tail.read(NULL,1); },"nonempty null output accepted");
    rejects([&tail,&destination]() { tail.read(destination,std::numeric_limits<std::size_t>::max()); },
        "stereo sample-count overflow accepted");
    rejects([&clip]() { Resampler invalid(clip,0); },"zero output sample rate accepted");
}

void real_assets(const std::string& folder) {
    struct Expected { const char* name; std::uint64_t frames; };
    // Inventory facts read from the permitted local binary assets, not package code.
    const Expected expected[] = {
        {"SOUND1.WAV",55350},{"SOUND104.WAV",1086},{"SOUND105.WAV",1828},{"SOUND108.WAV",7498},
        {"SOUND111.WAV",705},{"SOUND112.WAV",640},{"SOUND12.WAV",4156},{"SOUND13.WAV",7850},
        {"SOUND131.WAV",1150},{"SOUND136.WAV",19098},{"SOUND14.WAV",2862},{"SOUND16.WAV",906},
        {"SOUND17.WAV",1950},{"SOUND18.WAV",3845},{"SOUND181.WAV",27262},{"SOUND19.WAV",5089},
        {"SOUND20.WAV",8509},{"SOUND21.WAV",9054},{"SOUND22.WAV",7235},{"SOUND24.WAV",11966},
        {"SOUND240.WAV",14415},{"SOUND243.WAV",20528},{"SOUND25.WAV",25564},{"SOUND26.WAV",7166},
        {"SOUND27.WAV",20102},{"SOUND28.WAV",8510},{"SOUND29.WAV",10224},{"SOUND3.WAV",22718},
        {"SOUND30.WAV",22430},{"SOUND34.WAV",1380},{"SOUND35.WAV",19358},{"SOUND36.WAV",33707},
        {"SOUND38.WAV",12884},{"SOUND39.WAV",28141},{"SOUND4.WAV",16370},{"SOUND42.WAV",28999},
        {"SOUND43.WAV",22656},{"SOUND45.WAV",9630},{"SOUND49.WAV",1736},{"SOUND49D.WAV",3189},
        {"SOUND5.WAV",2924},{"SOUND50.WAV",11934},{"SOUND528.WAV",8748},{"SOUND53.WAV",8838},
        {"SOUND54.WAV",18110},{"SOUND55.WAV",21749},{"SOUND560.WAV",28806},{"SOUND563.WAV",23993},
        {"SOUND57.WAV",30361},{"SOUND58.WAV",3194},{"SOUND6.WAV",4191},{"SOUND65.WAV",17491},
        {"SOUND68.WAV",32238},{"SOUND7.WAV",26302},{"SOUND713.WAV",14394},{"SOUND735.WAV",27074},
        {"SOUND8.WAV",1961},{"SOUND827.WAV",47046},{"SOUND9.WAV",19958},{"SOUND999.WAV",6653}
    };
    const std::string separator = folder.empty() || folder[folder.size()-1] == '/' ||
        folder[folder.size()-1] == '\\' ? "" : "/";
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < sizeof(expected)/sizeof(expected[0]); ++i) {
        const WaveClip clip = WaveClip::load(folder+separator+expected[i].name);
        check(clip.frame_count() == expected[i].frames,"original clip frame count changed");
        check(clip.format().format_tag == 1 && clip.format().channels == 1 &&
            clip.format().sample_rate == 11025 && clip.format().bits_per_sample == 8,
            "original clip PCM format changed");
        total += clip.frame_count();
        Resampler whole(clip,48000), blocks(clip,48000);
        const std::size_t count = static_cast<std::size_t>((clip.frame_count()*48000+11024)/11025);
        check(whole.render(count+1) == chunked(blocks,count+1),"original audio block sizes changed resampled output");
    }
    check(sizeof(expected)/sizeof(expected[0]) == 60 && total == 863711,
        "original WAV inventory totals changed");
    const char* missing[] = {"SOUND2.WAV","SOUND37.WAV","SOUND44.WAV","SOUND52.WAV","SOUND59.WAV","SOUND62.WAV"};
    for (std::size_t i = 0; i < 6; ++i) {
        rejects([&folder,&separator,&missing,i]() { WaveClip::load(folder+separator+missing[i]); },
            "expected missing DAT reference unexpectedly loaded");
    }
    std::cout << "Original WAV inventory: 60 PCM8 mono 11025-Hz clips, 863711 frames.\n"
        << "Missing DAT references remain missing, without substitution: SOUND2.WAV, SOUND37.WAV, "
        << "SOUND44.WAV, SOUND52.WAV, SOUND59.WAV, SOUND62.WAV.\n";
}
}

int main(int argc,char** argv) {
    try {
        if (argc == 1) {
            decoding_and_ownership(); malformed_chunks_and_formats(); streaming_resampler();
            std::cout << "WAV parsing and streaming resampler tests passed\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--real") { real_assets(argv[2]); return 0; }
        std::cerr << "Usage: pinball_wave_tests [--real <local-asset-folder>]\n";
        return 2;
    } catch(const std::exception& error) {
        std::cerr << "WAV test failed: " << error.what() << '\n';
        return 1;
    }
}
