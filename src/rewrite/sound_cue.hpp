#ifndef PINBALL_REWRITE_SOUND_CUE_HPP
#define PINBALL_REWRITE_SOUND_CUE_HPP
#include <cstddef>
#include <cstdint>
namespace pinball { namespace audio {
// Role identifies an original visual-record sound slot; no mixer, file,
// device or asset ownership is part of the physics cue.
enum class SoundRole {
    SoftImpact,HardImpact,FlipperExtend,FlipperRetract,
    PlungerPull,PlungerRelease,PlungerFeed,SinkCapture,SinkRelease,
    KickoutCapture,KickoutRelease,HoleCapture,HoleLanding,Resource,
    TableStart,TableGameOver,TableTilt
};
struct SoundCue {
    std::size_t group; // Component group, or sound resource/wrapper for Resource.
    SoundRole role;
    std::int64_t time; // Simulation event timestamp; never changes game timers.
    float gain,pan;
    SoundCue(std::size_t owner=0,SoundRole kind=SoundRole::SoftImpact,
             std::int64_t at=0,float volume=1,float balance=0)
        :group(owner),role(kind),time(at),gain(volume),pan(balance) {}
};
} }
#endif
