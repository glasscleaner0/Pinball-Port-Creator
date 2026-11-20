// Fresh title-owned adapter. Public contracts consulted at pinned xsdk revision
// ade2c81d914634834d77394c5d9f8c4575f395bc: include/xsdk/audio.h,
// include/xsdk/thread.h and docs/GUIDE.md ("Audio", "Threads").
// No SDK implementation/sample or supplied game code is reused here.
#include "xbox_audio_output.hpp"
#include <atomic>
#include <mutex>
#ifdef __XSDK__
#include <xsdk/audio.h>
#include <xsdk/thread.h>
#endif
namespace pinball { namespace audio {
namespace {
const std::uint32_t invalid_parameter=0xC000000DU;
#ifdef __XSDK__
const std::uint32_t failed=0xC0000001U;
#else
const std::uint32_t unsupported=0xC00000BBU;
#endif
}
struct XboxAudioOutput::Impl {
    VoiceMixer mixer;
    mutable std::mutex mutex;
    std::atomic<std::uint32_t> stop_requested,running,last_status,submitted;
    bool opened;
#ifdef __XSDK__
    HANDLE thread;
#endif
    explicit Impl(std::size_t voices):mixer(48000,voices,256),mutex(),stop_requested(1),running(0),
        last_status(0),submitted(0),opened(false)
#ifdef __XSDK__
        ,thread(0)
#endif
    {}
#ifdef __XSDK__
    static std::uint32_t feed(void* context){
        Impl& self=*static_cast<Impl*>(context);
        try{
            float stereo[2*XSDK_AUDIO_FRAME_SAMPLES];
            while(!self.stop_requested.load(std::memory_order_acquire)){
                float* frame=0;
                const NTSTATUS status=xsdk_audio_next_frame(50,&frame);
                if(status==STATUS_IO_TIMEOUT)continue;
                if(!NT_SUCCESS(status)){
                    if(!self.stop_requested.load(std::memory_order_acquire))
                        self.last_status.store(static_cast<std::uint32_t>(status),std::memory_order_relaxed);
                    break;
                }
                // Closing uses this same short lock. It cannot unregister
                // between a last stop check and this frame's submission.
                std::lock_guard<std::mutex> lock(self.mutex);
                if(self.stop_requested.load(std::memory_order_acquire))break;
                if(!frame){self.last_status.store(failed,std::memory_order_relaxed);break;}
                self.mixer.render(stereo,XSDK_AUDIO_FRAME_SAMPLES);
                pack_stereo_planar6(stereo,XSDK_AUDIO_FRAME_SAMPLES,frame,XSDK_AUDIO_FRAME_FLOATS);
                xsdk_audio_queue_frame();self.submitted.fetch_add(1,std::memory_order_relaxed);
            }
        }catch(...){self.last_status.store(failed,std::memory_order_relaxed);}
        self.running.store(0,std::memory_order_release);return 0;
    }
#endif
};
XboxAudioOutput::XboxAudioOutput(std::size_t voices):impl_(new Impl(voices)){}
XboxAudioOutput::~XboxAudioOutput(){stop();}
bool XboxAudioOutput::start(std::uint32_t frames_ahead,std::uint32_t hardware_thread){
    if(frames_ahead>16||hardware_thread>=6){
        impl_->last_status.store(invalid_parameter,std::memory_order_relaxed);return false;
    }
#ifdef __XSDK__
    if(impl_->running.load(std::memory_order_acquire))return true;
    stop();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    NTSTATUS status=xsdk_audio_open(frames_ahead);
    impl_->last_status.store(static_cast<std::uint32_t>(status),std::memory_order_relaxed);
    if(!NT_SUCCESS(status))return false;
    impl_->opened=true;impl_->submitted.store(0,std::memory_order_relaxed);
    impl_->stop_requested.store(0,std::memory_order_release);impl_->running.store(1,std::memory_order_release);
    status=xsdk_thread_create_on(&impl_->thread,0,Impl::feed,impl_.get(),hardware_thread);
    if(!NT_SUCCESS(status)){
        impl_->last_status.store(static_cast<std::uint32_t>(status),std::memory_order_relaxed);
        impl_->stop_requested.store(1,std::memory_order_release);impl_->running.store(0,std::memory_order_release);
        xsdk_audio_close();impl_->opened=false;impl_->thread=0;return false;
    }
    return true;
#else
    (void)frames_ahead;(void)hardware_thread;
    impl_->last_status.store(unsupported,std::memory_order_relaxed);return false;
#endif
}
void XboxAudioOutput::stop(){
#ifdef __XSDK__
    HANDLE thread=0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stop_requested.store(1,std::memory_order_release);
        if(impl_->opened){
            const NTSTATUS status=xsdk_audio_close();
            if(!NT_SUCCESS(status))impl_->last_status.store(static_cast<std::uint32_t>(status),std::memory_order_relaxed);
            impl_->opened=false;
        }
        thread=impl_->thread;impl_->thread=0;
    }
    if(thread){NtWaitForSingleObjectEx(thread,KERNEL_MODE,0,0);NtClose(thread);}
#else
    impl_->stop_requested.store(1,std::memory_order_release);
#endif
    impl_->running.store(0,std::memory_order_release);
    std::lock_guard<std::mutex> lock(impl_->mutex);impl_->mixer.stop_all();
}
VoiceId XboxAudioOutput::play(const std::shared_ptr<const StereoClip>& clip,float gain,float pan,bool loop){
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(!impl_->running.load(std::memory_order_acquire))return 0;
    return impl_->mixer.play(clip,gain,pan,loop);
}
bool XboxAudioOutput::stop_voice(VoiceId id){
    std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->mixer.stop(id);
}
void XboxAudioOutput::stop_all(){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->mixer.stop_all();}
void XboxAudioOutput::set_paused(bool paused){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->mixer.set_paused(paused);}
void XboxAudioOutput::set_master_gain(float gain){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->mixer.set_master_gain(gain);}
AudioOutputStats XboxAudioOutput::stats() const{
    std::lock_guard<std::mutex> lock(impl_->mutex);AudioOutputStats result={};
    result.running=impl_->running.load(std::memory_order_acquire)!=0;
    result.last_status=impl_->last_status.load(std::memory_order_relaxed);
    result.submitted=impl_->submitted.load(std::memory_order_relaxed);result.mixer=impl_->mixer.stats();
#ifdef __XSDK__
    if(impl_->opened){
        XsdkAudioStats native={};xsdk_audio_stats(&native);result.played=native.played;result.silent=native.silent;
        result.fewest_queued=native.fewest_queued;result.longest_wait_us=native.longest_wait_us;
    }
#endif
    return result;
}
} }

