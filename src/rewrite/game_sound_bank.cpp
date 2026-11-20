#include "game_sound_bank.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace pinball { namespace audio {
namespace {
const std::size_t none=(std::numeric_limits<std::size_t>::max)();
struct Mapping {std::size_t soft,hard,aux4,aux3;Mapping():soft(none),hard(none),aux4(none),aux3(none){}};
std::string ascii_case(std::string text,bool upper){
    for(std::size_t i=0;i<text.size();++i){
        const unsigned char c=static_cast<unsigned char>(text[i]);
        if(upper&&c>='a'&&c<='z')text[i]=static_cast<char>(c-'a'+'A');
        if(!upper&&c>='A'&&c<='Z')text[i]=static_cast<char>(c-'A'+'a');
    }
    return text;
}
bool safe_filename(const std::string& name){
    if(name.size()<5||ascii_case(name.substr(name.size()-4),false)!=".wav")return false;
    for(std::size_t i=0;i<name.size();++i){
        const unsigned char c=static_cast<unsigned char>(name[i]);
        if(c<32||c==127||c=='/'||c=='\\'||c==':')return false;
    }
    return name!="."&&name!="..";
}
std::string path_join(const std::string& directory,const std::string& filename){
    if(directory.empty())return filename;
    const char last=directory[directory.size()-1];
    if(last=='/'||last=='\\')return directory+filename;
    const char slash=directory.find('\\')!=std::string::npos||directory.find(':')!=std::string::npos?'\\':'/';
    return directory+slash+filename;
}
std::size_t sound_reference(const table::TableModel& model,int index,std::size_t& errors){
    if(index>=0&&static_cast<std::size_t>(index)<model.groups().size()){
        const table::GroupInventory& g=model.group(static_cast<std::size_t>(index));
        if(g.has_record_marker&&g.record_marker==202)return static_cast<std::size_t>(index);
    }
    if(index!=0)++errors;
    return none;
}
bool inherited(const table::TableModel& model,int link,int marker,int key,std::size_t& sound,std::size_t& errors){
    if(link<0||static_cast<std::size_t>(link)>=model.groups().size()){++errors;return false;}
    const table::GroupInventory& g=model.group(static_cast<std::size_t>(link));
    if(!g.has_record_marker||g.record_marker!=marker){++errors;return false;}
    bool found=false;
    for(std::size_t r=0;r<g.float_records.size();++r){
        const std::vector<float>& values=g.float_records[r].values;
        for(std::size_t i=0;i<values.size();){
            const float tag=values[i];
            if(!std::isfinite(tag)||std::floor(tag)!=tag){++errors;break;}
            const std::size_t width=marker==400&&tag==404?4:2;
            if(values.size()-i<width){++errors;break;}
            if(tag==key){
                const float value=values[i+1];found=true;sound=none;
                if(!std::isfinite(value)||value<0||value>32767||std::floor(value)!=value)++errors;
                else sound=sound_reference(model,static_cast<int>(value),errors);
            }
            i+=width;
        }
    }
    return found;
}
Mapping visual_mapping(const table::TableModel& model,const table::GroupInventory& group,std::size_t& errors){
    Mapping result;
    for(std::size_t r=0;r<group.short_records.size();++r){
        const table::ShortRecord& record=group.short_records[r];
        if(record.field_type!=10)continue;
        const std::vector<std::int16_t>& values=record.values;
        for(std::size_t i=0;i<values.size();){
            const int key=values[i];
            // This inventory also contains nonvisual membership arrays. Their
            // keys are not audio slot records and are left uninterpreted.
            if(key!=100&&key!=300&&key!=304&&key!=400&&key!=406&&key!=602&&key!=1100&&key!=1101)break;
            const std::size_t width=2;
            if(values.size()-i<width){++errors;break;}
            const int value=values[i+1];std::size_t sound=none;
            if(key==300){if(inherited(model,value,300,304,sound,errors))result.soft=sound;}
            else if(key==400){if(inherited(model,value,400,406,sound,errors))result.hard=sound;}
            else if(key==304)result.soft=sound_reference(model,value,errors);
            else if(key==406)result.hard=sound_reference(model,value,errors);
            else if(key==1100)result.aux4=sound_reference(model,value,errors);
            else if(key==1101)result.aux3=sound_reference(model,value,errors);
            i+=width;
        }
    }
    return result;
}
}
GameSoundBank::GameSoundBank(const table::TableModel& model,const std::string& directory,
    std::size_t maximum_bytes,std::size_t maximum_clips,std::uint32_t rate)
    :resources_(),names_(),slots_(),wrappers_(),stats_(){
    if(rate==0)throw std::invalid_argument("sound bank output rate must be positive");
    stats_.resources=stats_.loaded=stats_.missing=stats_.over_budget=stats_.mapping_errors=stats_.cached_bytes=0;
    for(std::size_t g=0;g<model.groups().size();++g){
        const table::GroupInventory& group=model.group(g);
        if(group.has_record_marker&&group.record_marker==202){
            Entry e;e.info.group=g;e.info.filename.clear();e.info.loaded=false;e.info.frames=0;e.status=0;
            unsigned names=0;
            const std::vector<data::Field>& fields=model.dat().groups()[g].fields;
            for(std::size_t f=0;f<fields.size();++f)if(fields[f].type==9){e.info.filename=model.dat().string_value(fields[f]);++names;}
            const bool wav=e.info.filename.size()>=4&&ascii_case(e.info.filename.substr(e.info.filename.size()-4),false)==".wav";
            // DAT group4 is a literal marker202 "..." placeholder. Other
            // non-WAV resources likewise have no PCM dependency here.
            if(names!=1||wav){
                if(names!=1||!safe_filename(e.info.filename)){++stats_.mapping_errors;e.status=2;}
                resources_.insert(std::make_pair(g,e));++stats_.resources;
                if(e.status==0)names_.insert(std::make_pair(ascii_case(e.info.filename,false),g));
            }
        }
        const Mapping m=visual_mapping(model,group,stats_.mapping_errors);
        const Slots slots={m.soft,m.hard,m.aux4,m.aux3};slots_.insert(std::make_pair(g,slots));
    }
    for(std::map<std::size_t,Slots>::iterator it=slots_.begin();it!=slots_.end();++it){
        Slots& s=it->second;
        if(resources_.find(s.soft)==resources_.end())s.soft=none;
        if(resources_.find(s.hard)==resources_.end())s.hard=none;
        if(resources_.find(s.aux4)==resources_.end())s.aux4=none;
        if(resources_.find(s.aux3)==resources_.end())s.aux3=none;
    }
    for(std::size_t i=0;i<model.objects().size();++i)if(model.objects()[i].type==1031){
        const std::size_t group=model.objects()[i].group_index;
        const std::map<std::size_t,Slots>::const_iterator slots=slots_.find(group);
        if(slots!=slots_.end()&&slots->second.aux4!=none)wrappers_.insert(std::make_pair(group,slots->second.aux4));
    }
    std::size_t allocations=0;
    for(std::map<std::size_t,Entry>::iterator it=resources_.begin();it!=resources_.end();++it){
        Entry& entry=it->second;
        if(entry.status==2){++stats_.missing;continue;}
        const std::size_t canonical=names_.find(ascii_case(entry.info.filename,false))->second;
        if(canonical!=it->first){
            const Entry& first=resources_.find(canonical)->second;entry.pcm=first.pcm;entry.status=first.status;
            if(entry.pcm){entry.info.loaded=true;entry.info.frames=entry.pcm->frames();++stats_.loaded;}
            else if(entry.status==3)++stats_.over_budget;else ++stats_.missing;
            continue;
        }
        const std::size_t remaining=maximum_bytes-stats_.cached_bytes;
        if(allocations>=maximum_clips||remaining<2*sizeof(float)){entry.status=3;++stats_.over_budget;continue;}
        const std::string candidates[3]={entry.info.filename,ascii_case(entry.info.filename,true),ascii_case(entry.info.filename,false)};
        bool over_budget=false;
        for(unsigned attempt=0;attempt<3&&!entry.pcm;++attempt){
            if(attempt!=0&&candidates[attempt]==candidates[0])continue;
            try{
                WaveClip wave=WaveClip::load(path_join(directory,candidates[attempt]));
                entry.pcm=StereoClip::from_wave(std::move(wave),rate,remaining/(2*sizeof(float)));
            }catch(const std::length_error&){over_budget=true;break;}
            catch(const std::exception&){}
        }
        if(entry.pcm){
            entry.status=1;entry.info.loaded=true;entry.info.frames=entry.pcm->frames();
            stats_.cached_bytes+=entry.pcm->samples().size()*sizeof(float);++stats_.loaded;++allocations;
        }else if(over_budget){entry.status=3;++stats_.over_budget;}
        else{entry.status=2;++stats_.missing;}
    }
}
std::size_t GameSoundBank::resource(std::size_t group,SoundRole role) const{
    if(role==SoundRole::Resource){
        if(resources_.find(group)!=resources_.end())return group;
        const std::map<std::size_t,std::size_t>::const_iterator wrapper=wrappers_.find(group);
        return wrapper==wrappers_.end()?none:wrapper->second;
    }
    const std::map<std::size_t,Slots>::const_iterator it=slots_.find(group);if(it==slots_.end())return none;
    const Slots& s=it->second;
    switch(role){
    case SoundRole::SoftImpact:case SoundRole::KickoutCapture:case SoundRole::HoleLanding:return s.soft;
    case SoundRole::HardImpact:case SoundRole::PlungerPull:case SoundRole::KickoutRelease:case SoundRole::HoleCapture:case SoundRole::TableTilt:return s.hard;
    case SoundRole::FlipperExtend:case SoundRole::PlungerFeed:case SoundRole::SinkCapture:case SoundRole::TableStart:return s.aux4;
    case SoundRole::FlipperRetract:case SoundRole::PlungerRelease:case SoundRole::SinkRelease:case SoundRole::TableGameOver:return s.aux3;
    default:return none;
    }
}
std::shared_ptr<const StereoClip> GameSoundBank::clip(std::size_t group,SoundRole role) const{return clip_resource(resource(group,role));}
std::shared_ptr<const StereoClip> GameSoundBank::clip(const SoundCue& cue) const{return clip(cue.group,cue.role);}
std::shared_ptr<const StereoClip> GameSoundBank::clip_resource(std::size_t group) const{
    const std::size_t resolved=resource(group,SoundRole::Resource);
    const std::map<std::size_t,Entry>::const_iterator it=resources_.find(resolved);
    return it==resources_.end()?std::shared_ptr<const StereoClip>():it->second.pcm;
}
std::shared_ptr<const StereoClip> GameSoundBank::clip_named(const std::string& filename) const{
    const std::map<std::string,std::size_t>::const_iterator it=names_.find(ascii_case(filename,false));
    return it==names_.end()?std::shared_ptr<const StereoClip>():clip_resource(it->second);
}
const SoundResourceInfo* GameSoundBank::info(std::size_t group) const{
    const std::size_t resolved=resource(group,SoundRole::Resource);
    const std::map<std::size_t,Entry>::const_iterator it=resources_.find(resolved);
    return it==resources_.end()?0:&it->second.info;
}
SoundBankStats GameSoundBank::stats() const{return stats_;}
} }


