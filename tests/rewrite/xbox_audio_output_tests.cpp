#include "xbox_audio_output.hpp"
#include <iostream>
#include <stdexcept>
#ifdef __XSDK__
#include <xsdk/thread.h>
#endif
namespace {
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
}
int main(){try{
    pinball::audio::XboxAudioOutput output;
    check(!output.stats().running,"audio backend started without explicit request");
    check(!output.start(17,5)&&output.stats().last_status==0xC000000DU,"invalid queue depth not rejected nonfatally");
    check(!output.start(8,6),"invalid hardware thread accepted");
#ifndef __XSDK__
    check(!output.start()&&output.stats().last_status==0xC00000BBU,"host backend should report unsupported");
#endif
#ifdef __XSDK__
    output.set_master_gain(0); // Muted device smoke: no unexpected test tone.
    check(output.start(8,5),"native audio output could not start");
    const auto clip=pinball::audio::StereoClip::create(std::vector<float>(4096,.125f));
    check(output.play(clip)!=0,"native audio voice was rejected");
    for(unsigned i=0;i<200;++i){
        const auto current=output.stats();if(current.submitted>=24&&current.played>0)break;
        xsdk_thread_sleep(5);
    }
    const auto driver=output.stats();
    check(driver.running&&driver.submitted>=24&&driver.played>0&&driver.mixer.started==1,"native audio queue did not play submitted frames");
    std::cout<<"native audio driver submitted="<<driver.submitted<<" played="<<driver.played<<" status="<<driver.last_status<<'\n';
    output.stop();
#endif
    check(output.play(std::shared_ptr<const pinball::audio::StereoClip>())==0,"inactive/missing output blocked");
    output.set_paused(true);output.set_master_gain(.5f);output.stop_all();output.stop();output.stop();
    check(!output.stats().running,"idempotent output stop");
    std::cout<<"xbox_audio_output_tests PASS:optional lifecycle/validation; actual driver playback is separately verified\n";return 0;
}catch(const std::exception& e){std::cerr<<"xbox_audio_output_tests FAIL: "<<e.what()<<'\n';return 1;}}
