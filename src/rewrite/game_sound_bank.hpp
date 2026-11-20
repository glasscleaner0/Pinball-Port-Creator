#ifndef PINBALL_REWRITE_GAME_SOUND_BANK_HPP
#define PINBALL_REWRITE_GAME_SOUND_BANK_HPP
#include "sound_cue.hpp"
#include "table_model.hpp"
#include "voice_mixer.hpp"
#include <map>
#include <string>
namespace pinball { namespace audio {
struct SoundBankStats {
    std::size_t resources,loaded,missing,over_budget,mapping_errors,cached_bytes;
};
struct SoundResourceInfo {
    std::size_t group;
    std::string filename; // Original DAT spelling; files are never renamed.
    bool loaded;
    std::size_t frames;
};
// Data-only visual slot resolver plus bounded optional immutable PCM cache.
// Constructor preloads only marker202 WAV records, once, from the given
// directory (normally SOUNDS beside the XEX). Playback lookups never do IO.
// Absent/unusable/over-budget sounds stay null and never set game timing.
// State0 visual records are used; this table's continuation frames are art.
class GameSoundBank {
public:
    explicit GameSoundBank(const table::TableModel& model,const std::string& sound_directory,
                           std::size_t maximum_bytes=32U*1024U*1024U,std::size_t maximum_clips=128,
                           std::uint32_t output_rate=48000);
    std::shared_ptr<const StereoClip> clip(std::size_t group,SoundRole role) const;
    std::shared_ptr<const StereoClip> clip(const SoundCue& cue) const;
    std::shared_ptr<const StereoClip> clip_resource(std::size_t sound_resource_group) const;
    std::shared_ptr<const StereoClip> clip_named(const std::string& filename) const;
    std::size_t resource(std::size_t group,SoundRole role) const; // max(size_t) when absent.
    const SoundResourceInfo* info(std::size_t sound_resource_group) const;
    SoundBankStats stats() const;
private:
    struct Slots {std::size_t soft,hard,aux4,aux3;};
    struct Entry {SoundResourceInfo info;std::shared_ptr<const StereoClip> pcm;unsigned status;};
    std::map<std::size_t,Entry> resources_;
    std::map<std::string,std::size_t> names_;
    std::map<std::size_t,Slots> slots_;
    std::map<std::size_t,std::size_t> wrappers_;
    SoundBankStats stats_;
};
} }
#endif

