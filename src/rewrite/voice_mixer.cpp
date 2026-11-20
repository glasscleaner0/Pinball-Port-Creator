#include "voice_mixer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace pinball { namespace audio {
namespace {
void add(std::uint64_t& counter,std::uint64_t amount=1){
    const std::uint64_t limit=(std::numeric_limits<std::uint64_t>::max)();
    counter=amount>limit-counter?limit:counter+amount;
}
void extent(std::size_t frames){
    if(frames>(std::numeric_limits<std::size_t>::max)()/(2*sizeof(float)))
        throw std::overflow_error("stereo extent overflow");
}
void unit_gain(float gain){
    if(!std::isfinite(gain)||gain<0||gain>1)throw std::invalid_argument("audio gain must be within0..1");
}
}
StereoClip::StereoClip(std::vector<float> stereo,std::uint32_t rate):stereo_(std::move(stereo)),rate_(rate){}
std::shared_ptr<const StereoClip> StereoClip::create(std::vector<float> stereo,std::uint32_t rate){
    if(rate==0||(stereo.size()&1U)!=0)throw std::invalid_argument("invalid stereo clip shape/rate");
    for(std::size_t i=0;i<stereo.size();++i)
        if(!std::isfinite(stereo[i])||stereo[i]<-1||stereo[i]>1)throw std::invalid_argument("stereo clip must contain finite normalized PCM");
    return std::shared_ptr<const StereoClip>(new StereoClip(std::move(stereo),rate));
}
std::shared_ptr<const StereoClip> StereoClip::from_wave(WaveClip clip,std::uint32_t rate,std::size_t max_frames){
    if(rate==0||clip.format().sample_rate==0)throw std::invalid_argument("invalid conversion rate");
    const std::uint64_t frames=clip.frame_count(),limit=(std::numeric_limits<std::uint64_t>::max)();
    if(frames>limit/rate)throw std::overflow_error("resampled frame product overflow");
    const std::uint64_t product=frames*rate,denominator=clip.format().sample_rate;
    const std::uint64_t output=product/denominator+(product%denominator!=0?1U:0U);
    if(output>max_frames||output>(std::numeric_limits<std::size_t>::max)()/2)
        throw std::length_error("resampled clip exceeds frame budget");
    extent(static_cast<std::size_t>(output));
    Resampler converter(std::move(clip),rate,false);
    return create(converter.render(static_cast<std::size_t>(output)),rate);
}
std::shared_ptr<const StereoClip> StereoClip::load_optional(const std::string& path,std::uint32_t rate,std::size_t max_frames){
    try{return from_wave(WaveClip::load(path),rate,max_frames);}
    catch(const std::exception&){return std::shared_ptr<const StereoClip>();}
}
const std::vector<float>& StereoClip::samples() const{return stereo_;}
std::size_t StereoClip::frames() const{return stereo_.size()/2;}
std::uint32_t StereoClip::sample_rate() const{return rate_;}
VoiceMixer::VoiceMixer(std::uint32_t rate,std::size_t voices,std::size_t maximum_block)
    :rate_(rate),maximum_block_(maximum_block),voices_(),sequence_(0),paused_(false),master_(1),stats_(){
    if(rate==0||voices==0||voices>64||maximum_block==0)throw std::invalid_argument("invalid bounded voice mixer configuration");
    extent(maximum_block);voices_.resize(voices);stats_.active_voices=0;
    stats_.started=stats_.dropped=stats_.ignored=stats_.clipped_samples=stats_.rendered_frames=0;
}
VoiceId VoiceMixer::play(const std::shared_ptr<const StereoClip>& clip,float gain,float pan,bool loop){
    if(!clip||clip->frames()==0){add(stats_.ignored);return 0;}
    unit_gain(gain);
    if(!std::isfinite(pan)||pan<-1||pan>1)throw std::invalid_argument("audio pan must be within-1..1");
    if(clip->sample_rate()!=rate_)throw std::invalid_argument("voice clip rate differs from mixer");
    std::size_t slot=voices_.size();
    for(std::size_t i=0;i<voices_.size();++i)if(voices_[i].id==0){slot=i;break;}
    if(slot==voices_.size()){add(stats_.dropped);return 0;}
    if(sequence_==(std::numeric_limits<VoiceId>::max)())throw std::overflow_error("voice ID sequence exhausted");
    Voice& voice=voices_[slot];voice.id=++sequence_;voice.clip=clip;voice.cursor=0;voice.loop=loop;
    voice.left=gain*(pan>0?1-pan:1);voice.right=gain*(pan<0?1+pan:1);add(stats_.started);return voice.id;
}
bool VoiceMixer::stop(VoiceId id){
    if(id==0)return false;
    for(std::size_t i=0;i<voices_.size();++i)if(voices_[i].id==id){
        voices_[i].id=0;voices_[i].clip.reset();return true;
    }
    return false;
}
void VoiceMixer::stop_all(){
    for(std::size_t i=0;i<voices_.size();++i){voices_[i].id=0;voices_[i].clip.reset();}
}
void VoiceMixer::set_paused(bool paused){paused_=paused;}
void VoiceMixer::set_master_gain(float gain){unit_gain(gain);master_=gain;}
void VoiceMixer::render(float* stereo,std::size_t frames){
    extent(frames);
    if(frames>maximum_block_)throw std::length_error("audio render block exceeds budget");
    if(!stereo&&frames)throw std::invalid_argument("audio render requires output");
    if(frames==0)return;
    std::fill(stereo,stereo+2*frames,0.0f);add(stats_.rendered_frames,frames);
    if(paused_)return;
    for(std::size_t slot=0;slot<voices_.size();++slot){
        Voice& voice=voices_[slot];if(voice.id==0)continue;
        const std::vector<float>& source=voice.clip->samples();const std::size_t count=voice.clip->frames();
        for(std::size_t frame=0;frame<frames;++frame){
            if(voice.cursor==count){
                if(voice.loop)voice.cursor=0;
                else{voice.id=0;break;}
            }
            stereo[2*frame]+=source[2*voice.cursor]*voice.left;
            stereo[2*frame+1]+=source[2*voice.cursor+1]*voice.right;++voice.cursor;
        }
        if(!voice.loop&&voice.cursor==count)voice.id=0;
        // The inactive slot retains its clip until main-thread reuse/stop,
        // so render never destroys the last reference to a large PCM buffer.
    }
    for(std::size_t i=0;i<2*frames;++i){
        const float value=stereo[i]*master_;
        if(value>1){stereo[i]=1;add(stats_.clipped_samples);}
        else if(value< -1){stereo[i]=-1;add(stats_.clipped_samples);}
        else stereo[i]=value;
    }
}
MixerStats VoiceMixer::stats() const{
    MixerStats result=stats_;result.active_voices=0;
    for(std::size_t i=0;i<voices_.size();++i)if(voices_[i].id!=0)++result.active_voices;
    return result;
}
std::uint32_t VoiceMixer::sample_rate() const{return rate_;}
void pack_stereo_planar6(const float* stereo,std::size_t frames,float* planar,std::size_t capacity){
    extent(frames);
    if(frames>(std::numeric_limits<std::size_t>::max)()/(6*sizeof(float)))throw std::overflow_error("planar audio extent overflow");
    if(capacity<6*frames)throw std::length_error("six-channel audio output too small");
    if(frames&&(!stereo||!planar||stereo==planar))throw std::invalid_argument("planar audio requires separate buffers");
    for(std::size_t i=0;i<2*frames;++i)if(!std::isfinite(stereo[i]))throw std::invalid_argument("nonfinite audio sample");
    if(frames==0)return;
    std::fill(planar,planar+6*frames,0.0f);
    for(std::size_t i=0;i<frames;++i){planar[i]=stereo[2*i];planar[frames+i]=stereo[2*i+1];}
}
} }


