#include "voice_mixer.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace pinball::audio;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void near(float a,float b,const char* m){check(std::fabs(a-b)<.000001f,m);}
template<class E,class F>void throws(F f,const char* m){try{f();}catch(const E&){return;}throw std::runtime_error(m);}
std::shared_ptr<const StereoClip> clip(){return StereoClip::create({.25f,-.5f,.5f,.75f,-.75f,1});}
void voice_lifetime_pan_pause(){
    VoiceMixer m(48000,2,16);const VoiceId id=m.play(clip());float out[8];
    m.render(out,2);near(out[0],.25f,"first L");near(out[1],-.5f,"first R");near(out[2],.5f,"second L");
    check(m.stats().active_voices==1,"voice ended early");
    m.set_paused(true);m.render(out,2);for(unsigned i=0;i<4;++i)near(out[i],0,"pause not silent");
    check(m.stats().active_voices==1,"pause advanced voice");
    m.set_paused(false);m.render(out,2);near(out[0],-.75f,"resume cursor");near(out[1],1,"resume R");
    near(out[2],0,"finished tail not zero");near(out[3],0,"finished tail R not zero");
    check(m.stats().active_voices==0&&!m.stop(id),"finished voice still active");
    m.play(clip(),1,-1);m.render(out,1);near(out[0],.25f,"left pan");near(out[1],0,"left pan leaked R");
    m.stop_all();m.set_master_gain(.5f);m.play(clip(),1,1);m.render(out,1);
    near(out[0],0,"right pan leaked L");near(out[1],-.25f,"master gain/right pan");
    m.stop_all();m.set_master_gain(0);m.play(clip());m.render(out,4);
    check(m.stats().active_voices==0,"mute froze voice instead of advancing");
}
void bounded_voices_clipping_and_ids(){
    const std::shared_ptr<const StereoClip> loud=StereoClip::create({.75f,.75f,.75f,.75f});
    VoiceMixer m(48000,2,8);const VoiceId first=m.play(loud),second=m.play(loud);
    check(first&&second&&m.play(loud)==0&&m.stats().dropped==1,"voice budget did not drop new request");
    float out[8];m.render(out,4);
    for(unsigned i=0;i<4;++i)near(out[i],1,"sum not hard clipped");
    for(unsigned i=4;i<8;++i)near(out[i],0,"unproduced mix tail not silent");
    check(m.stats().clipped_samples==4&&m.stats().active_voices==0,"clipping/voice counters");
    check(m.play(std::shared_ptr<const StereoClip>())==0&&m.play(StereoClip::create({}))==0&&
          m.stats().ignored==2,"missing/empty sound not silently ignored");
    const VoiceId third=m.play(loud);check(third>second&&!m.stop(first)&&m.stop(third),"stale ID affected new voice");
    const VoiceId fourth=m.play(loud);m.stop_all();
    const VoiceId fifth=m.play(loud);check(fifth>fourth&&!m.stop(fourth),"stop_all reused stale ID");
}
void loop_subdivision_and_planar_contract(){
    const std::shared_ptr<const StereoClip> source=StereoClip::create({.1f,.2f,.3f,.4f});
    VoiceMixer all(48000,1,16),split(48000,1,16);all.play(source,1,0,true);split.play(source,1,0,true);
    float whole[16],parts[16];all.render(whole,8);split.render(parts,3);split.render(parts+6,5);
    for(unsigned i=0;i<16;++i)near(whole[i],parts[i],"loop changed with render subdivision");
    check(all.stats().active_voices==1,"loop finished");
    const float stereo[6]={1,2,3,4,5,6};float planar[20];
    for(unsigned i=0;i<20;++i)planar[i]=9;
    pack_stereo_planar6(stereo,3,planar,20);
    near(planar[0],1,"planar L0");near(planar[1],3,"planar L1");near(planar[2],5,"planar L2");
    near(planar[3],2,"planar R0");near(planar[4],4,"planar R1");near(planar[5],6,"planar R2");
    for(unsigned i=6;i<18;++i)near(planar[i],0,"surround plane not cleared");
    near(planar[18],9,"planar wrote beyond six planes");
}
void u16(std::vector<std::uint8_t>& b,unsigned value){b.push_back(static_cast<std::uint8_t>(value));b.push_back(static_cast<std::uint8_t>(value>>8));}
void u32(std::vector<std::uint8_t>& b,std::uint32_t value){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(value>>(8*i)));}
void tag(std::vector<std::uint8_t>& b,const char* s){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(s[i]));}
WaveClip pcm(){
    std::vector<std::uint8_t> b;tag(b,"RIFF");u32(b,40);tag(b,"WAVE");tag(b,"fmt ");u32(b,16);u16(b,1);u16(b,1);
    u32(b,24000);u32(b,24000);u16(b,1);u16(b,8);tag(b,"data");u32(b,4);
    b.push_back(0);b.push_back(128);b.push_back(255);b.push_back(128);return WaveClip::parse(b);
}
void conversion_missing_and_guards(){
    const std::shared_ptr<const StereoClip> converted=StereoClip::from_wave(pcm());
    check(converted->frames()==8&&converted->sample_rate()==48000,"preconversion frame count");
    near(converted->samples()[0],-1,"PCM conversion first sample");near(converted->samples()[2],-.5f,"resampling interpolation");
    near(converted->samples()[8],127.0f/128.0f,"PCM conversion positive sample");
    check(!StereoClip::load_optional("__pinball_missing_optional_sound__.wav"),"missing sound blocked optional load");
    throws<std::length_error>([]{StereoClip::from_wave(pcm(),48000,7);},"clip frame budget ignored");
    throws<std::invalid_argument>([]{StereoClip::create({0,0,0});},"odd stereo data accepted");
    throws<std::invalid_argument>([]{StereoClip::create({0,(std::numeric_limits<float>::quiet_NaN)()});},"nonfinite PCM accepted");
    throws<std::invalid_argument>([]{VoiceMixer m(48000,0);},"zero voice budget accepted");
    VoiceMixer m(48000,1,4);m.play(clip());float out[10]={};
    throws<std::length_error>([&]{m.render(out,5);},"oversized render accepted");
    m.render(out,1);near(out[0],.25f,"invalid render partially advanced voice");
    throws<std::invalid_argument>([&]{m.play(clip(),2);},"invalid gain accepted");
    throws<std::invalid_argument>([&]{m.set_master_gain(-1);},"invalid master gain accepted");
    throws<std::invalid_argument>([&]{m.play(StereoClip::create({0,0},44100));},"wrong-rate clip accepted");
    throws<std::invalid_argument>([&]{m.render(0,1);},"null render output accepted");
    const float input[2]={0,0};float planar[6];
    throws<std::length_error>([&]{pack_stereo_planar6(input,1,planar,5);},"short planar output accepted");
    throws<std::overflow_error>([&]{pack_stereo_planar6(input,(std::numeric_limits<std::size_t>::max)()/(6*sizeof(float))+1,planar,6);},"planar byte extent overflow accepted");
}
}
int main(){try{voice_lifetime_pan_pause();bounded_voices_clipping_and_ids();loop_subdivision_and_planar_contract();conversion_missing_and_guards();
    std::cout<<"voice_mixer_tests PASS:bounded voices,mix/clip/pan,pause/loop,PCM conversion,planar6,missing silence\n";return 0;
}catch(const std::exception& e){std::cerr<<"voice_mixer_tests FAIL: "<<e.what()<<'\n';return 1;}}
