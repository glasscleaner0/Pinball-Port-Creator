#include "wave_audio.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <locale>
#include <string>

namespace {
void quoted(std::ostream& out,const std::string& value) {
    const char hex[]="0123456789abcdef";
    out << '"';
    for(std::size_t i=0;i<value.size();++i) {
        const unsigned char c=static_cast<unsigned char>(value[i]);
        if(c=='"'||c=='\\') out << '\\' << static_cast<char>(c);
        else if(c<32||c>=127) out << "\\u00" << hex[c>>4] << hex[c&15];
        else out << static_cast<char>(c);
    }
    out << '"';
}
}

int main(int argc,char** argv) {
    if(argc!=2) { std::cerr << "Usage: pinball_audio_inspect <local-clip.wav>\n"; return 2; }
    try {
        const pinball::audio::WaveClip clip=pinball::audio::WaveClip::load(argv[1]);
        const pinball::audio::WaveFormat& format=clip.format();
        std::cout.imbue(std::locale::classic());
        std::cout << "{\n  \"format\":\"pinball-wave-inventory-v1\",\n  \"source\":";
        quoted(std::cout,argv[1]);
        std::cout << ",\n  \"pcm\":{\"tag\":" << format.format_tag << ",\"channels\":" << format.channels
            << ",\"sample_rate\":" << format.sample_rate << ",\"bits_per_sample\":" << format.bits_per_sample
            << ",\"byte_rate\":" << format.byte_rate << ",\"block_align\":" << format.block_align << "},\n"
            << "  \"frames\":" << clip.frame_count() << ",\n  \"duration_seconds\":{\"numerator\":"
            << clip.duration_numerator() << ",\"denominator\":" << clip.duration_denominator() << "},\n"
            << "  \"metadata_policy\":\"all original chunks and pad bytes retained; non-audio metadata uninterpreted\",\n"
            << "  \"resampling_policy\":\"new linear interpolation, mono duplicated to stereo; last sample held until source duration ends; optional loop interpolates to first sample\",\n"
            << "  \"game_event_timing\":\"unspecified; PCM duration is not a reference event timer\",\n"
            << "  \"chunks\":[";
        for(std::size_t i=0;i<clip.chunks().size();++i) {
            if(i) std::cout << ',';
            const pinball::audio::WaveChunk& chunk=clip.chunks()[i];
            std::cout << "{\"id\":"; quoted(std::cout,chunk.id);
            std::cout << ",\"header_offset\":" << chunk.header_offset << ",\"payload_offset\":"
                << chunk.payload_offset << ",\"payload_size\":" << chunk.payload_size
                << ",\"interpreted\":" << ((chunk.id=="fmt "||chunk.id=="data")?"true":"false") << '}';
        }
        std::cout << "]\n}\n";
        if(!std::cout) throw std::runtime_error("audio inventory output failed");
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "Audio inventory failed: " << error.what() << '\n';
        return 1;
    }
}
