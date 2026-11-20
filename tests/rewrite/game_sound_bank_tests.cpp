#include "game_sound_bank.hpp"
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
namespace {
using namespace pinball;using audio::GameSoundBank;using audio::SoundRole;
typedef std::vector<std::uint8_t> Bytes;
struct Field {std::uint8_t type;Bytes bytes;Field(std::uint8_t t,Bytes b):type(t),bytes(std::move(b)){}};
typedef std::vector<Field> Group;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void p16(Bytes& b,std::size_t at,std::uint16_t n){b.at(at)=static_cast<std::uint8_t>(n);b.at(at+1)=static_cast<std::uint8_t>(n>>8);}
void p32(Bytes& b,std::size_t at,std::uint32_t n){for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::uint8_t>(n>>(8*i));}
Field shorts(std::uint8_t type,std::initializer_list<std::int16_t> values){
    Bytes b(values.size()*2);std::size_t at=0;for(auto i=values.begin();i!=values.end();++i,at+=2)p16(b,at,static_cast<std::uint16_t>(*i));
    return Field(type,b);
}
Field text(std::uint8_t type,const std::string& s){Bytes b(s.begin(),s.end());b.push_back(0);return Field(type,b);}
Field floats(std::initializer_list<float> values){
    Bytes b(values.size()*4);std::size_t at=0;
    for(auto i=values.begin();i!=values.end();++i,at+=4){std::uint32_t bits=0;std::memcpy(&bits,&*i,4);p32(b,at,bits);}
    return Field(11,b);
}
table::TableModel fixture(){
    std::vector<Group> g(16);
    g[0]={text(3,"table_size"),shorts(10,{600,416})};
    g[1]={text(3,"table_objects"),shorts(10,{1025,1000,2,1031,8})};
    g[2]={shorts(0,{200}),shorts(10,{304,5,300,3,400,4,406,7,1100,5,1101,6})};
    g[3]={shorts(0,{300}),floats({301,.8f,304,6})};
    // The404 vector contains sound-like numbers; they are not attribute tags.
    g[4]={shorts(0,{400}),floats({404,406,5,6,406,6})};
    g[5]={shorts(0,{202}),text(9,"first.wav")};g[6]={shorts(0,{202}),text(9,"second.wav")};
    g[7]={shorts(0,{202}),text(9,"third.wav")};g[8]={shorts(0,{200}),shorts(10,{1100,7})};
    g[9]={shorts(0,{300}),floats({301,.8f})};
    g[10]={shorts(0,{200}),shorts(10,{300,3,400,4})};
    g[11]={shorts(0,{200})};g[12]={shorts(0,{200}),shorts(10,{304,5,300,9})};
    g[13]={shorts(0,{202}),text(9,"../outside.wav")};g[14]={shorts(0,{200}),shorts(10,{406,99})};
    g[15]={shorts(0,{200}),shorts(10,{1027,5,6,7})};
    Bytes b(183,0);const char signature[]="PARTOUT(4.0)RESOURCE";
    for(std::size_t i=0;i<sizeof(signature);++i)b[i]=static_cast<std::uint8_t>(signature[i]);
    p16(b,175,static_cast<std::uint16_t>(g.size()));
    for(std::size_t i=0;i<g.size();++i){
        b.push_back(static_cast<std::uint8_t>(g[i].size()));
        for(std::size_t f=0;f<g[i].size();++f){
            b.push_back(g[i][f].type);
            if(g[i][f].type!=0&&g[i][f].type!=2&&g[i][f].type!=13){const std::size_t at=b.size();b.resize(at+4);p32(b,at,static_cast<std::uint32_t>(g[i][f].bytes.size()));}
            b.insert(b.end(),g[i][f].bytes.begin(),g[i][f].bytes.end());
        }
    }
    p32(b,171,static_cast<std::uint32_t>(b.size()));p32(b,177,static_cast<std::uint32_t>(b.size()-183));
    return table::TableModel::from_dat(data::DatFile::parse(std::move(b)));
}
void synthetic(){
    const table::TableModel model=fixture();const GameSoundBank bank(model,"__missing_sound_directory__",0);
    const std::size_t absent=(std::numeric_limits<std::size_t>::max)();
    check(bank.resource(2,SoundRole::SoftImpact)==6,"material inheritance/order");
    check(bank.resource(2,SoundRole::HardImpact)==7,"direct hard override");
    check(bank.resource(10,SoundRole::HardImpact)==6,"kicker404 vector was misread as sound406");
    check(bank.resource(12,SoundRole::SoftImpact)==5,"soundless material erased previous soft sound");
    check(bank.resource(2,SoundRole::FlipperExtend)==5&&bank.resource(2,SoundRole::FlipperRetract)==6,"auxiliary role slots");
    check(bank.resource(2,SoundRole::TableStart)==5&&bank.resource(2,SoundRole::TableGameOver)==6&&
          bank.resource(2,SoundRole::TableTilt)==7,"table lifecycle role slots");
    static_assert(static_cast<int>(SoundRole::Resource)==13,"existing sound role ordinals changed");
    check(bank.resource(8,SoundRole::Resource)==7&&bank.info(8)&&bank.info(8)->filename=="third.wav","sound wrapper resolution");
    check(bank.resource(11,SoundRole::HoleCapture)==absent&&!bank.clip(11,SoundRole::HoleCapture),"absent role invented sound");
    check(!bank.clip_resource(13)&&!bank.clip_named("../outside.wav")&&!bank.clip_resource(99),"invalid optional sound resolved");
    const audio::SoundBankStats stats=bank.stats();
    check(stats.resources==4&&stats.loaded==0&&stats.over_budget==3&&stats.missing==1&&stats.mapping_errors==2,
          "bounded/malformed audio classification");
    const audio::SoundCue cue(2,SoundRole::PlungerFeed,4294967296LL);
    check(!bank.clip(cue)&&cue.time==4294967296LL,"portable cue changed timing");
    static_assert(sizeof(audio::SoundCue)<=48,"physics cue unexpectedly large");
}
void maps(const GameSoundBank& bank,std::size_t owner,SoundRole role,std::size_t resource,const char* filename,bool loaded){
    check(bank.resource(owner,role)==resource,"actual DAT role/resource mismatch");
    const audio::SoundResourceInfo* info=bank.info(resource);
    check(info&&info->filename==filename&&info->loaded==loaded,"actual DAT filename/availability mismatch");
    check(static_cast<bool>(bank.clip(owner,role))==loaded,"actual clip availability mismatch");
}
void real(const std::string& dat,const std::string& directory){
    const table::TableModel model=table::TableModel::load(dat);const GameSoundBank bank(model,directory);
    const std::size_t plunger=model.require_named_group("plunger");
    const std::size_t table_group=model.require_named_group("table");
    maps(bank,table_group,SoundRole::TableStart,18,"sound1.wav",true);
    maps(bank,table_group,SoundRole::TableGameOver,19,"sound2.wav",false);
    maps(bank,table_group,SoundRole::TableTilt,62,"sound108.wav",true);
    maps(bank,model.require_named_group("a_flip1"),SoundRole::FlipperExtend,60,"sound104.wav",true);
    maps(bank,model.require_named_group("a_flip2"),SoundRole::FlipperRetract,61,"sound105.wav",true);
    maps(bank,plunger,SoundRole::PlungerPull,59,"sound62.wav",false);
    maps(bank,plunger,SoundRole::PlungerRelease,34,"sound22.wav",true);
    maps(bank,plunger,SoundRole::PlungerFeed,21,"sound4.wav",true);
    maps(bank,model.require_named_group("v_sink1"),SoundRole::SinkCapture,48,"sound43.wav",true);
    maps(bank,model.require_named_group("v_sink2"),SoundRole::SinkRelease,48,"sound43.wav",true);
    maps(bank,model.require_named_group("a_bump1"),SoundRole::HardImpact,26,"sound12.wav",true);
    maps(bank,model.require_named_group("a_kout3"),SoundRole::KickoutCapture,57,"sound57.wav",true);
    maps(bank,model.require_named_group("a_kout2"),SoundRole::KickoutRelease,57,"sound57.wav",true);
    check(!bank.clip(model.require_named_group("ramp_hole"),SoundRole::HoleCapture)&&
          !bank.clip(model.require_named_group("ramp_hole"),SoundRole::HoleLanding),"silent hole slot invented fallback");
    check(bank.clip_named("SOUND104.WAV")==bank.clip_resource(60),"case-insensitive filename lookup");
    check(bank.clip_resource(model.require_named_group("soundwave35"))==bank.clip_resource(43),"actual sound wrapper");
    const std::size_t missing[6]={19,45,49,54,58,59};
    for(unsigned i=0;i<6;++i)check(!bank.clip_resource(missing[i]),"missing original WAV was not silent");
    const audio::SoundBankStats stats=bank.stats();
    std::cout<<"Sound bank real DAT resources="<<stats.resources<<" loaded="<<stats.loaded<<" missing="<<stats.missing
             <<" budget="<<stats.over_budget<<" mapping_errors="<<stats.mapping_errors<<" cached_bytes="<<stats.cached_bytes<<'\n';
    check(stats.resources==47&&stats.loaded==41&&stats.missing==6&&stats.over_budget==0&&stats.mapping_errors==0&&
          stats.cached_bytes>0&&stats.cached_bytes<=32U*1024U*1024U,"actual bounded sound bank inventory");
    const GameSoundBank disabled(model,directory,0);
    check(disabled.stats().loaded==0&&disabled.stats().over_budget==47&&disabled.stats().cached_bytes==0,"disabled cache performed loads");
}
}
int main(int argc,char** argv){try{
    synthetic();
#ifdef __XSDK__
    real(argc>1?argv[1]:"game:\\PINBALL.DAT",argc>2?argv[2]:"game:\\SOUNDS");
#else
    if(argc==3)real(argv[1],argv[2]);
    else if(argc!=1){std::cerr<<"Usage: game_sound_bank_tests [PINBALL.DAT SOUNDS]\n";return 2;}
#endif
    std::cout<<"game_sound_bank_tests PASS:visual slot data mapping,bounded optional assets,no timing dependency\n";return 0;
}catch(const std::exception& e){std::cerr<<"game_sound_bank_tests FAIL: "<<e.what()<<'\n';return 1;}}
