#include "score_display.hpp"
#include "table_model.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace pinball;
typedef std::vector<std::uint8_t> Bytes;
struct Entry {std::uint8_t type;Bytes bytes;Entry(unsigned t,Bytes b):type(static_cast<std::uint8_t>(t)),bytes(b){}};
typedef std::vector<Entry> Group;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void error(const std::function<void()>& run){try{run();}catch(const std::exception&){return;}throw std::runtime_error("expected HUD validation failure");}
void put16(Bytes& b,std::size_t o,unsigned v){b.at(o)=static_cast<std::uint8_t>(v);b.at(o+1)=static_cast<std::uint8_t>(v>>8);}
void put32(Bytes& b,std::size_t o,std::size_t v){for(unsigned i=0;i<4;++i)b.at(o+i)=static_cast<std::uint8_t>(v>>(i*8));}
Bytes shorts(std::initializer_list<int> values){Bytes b(values.size()*2);std::size_t i=0;for(int v:values){put16(b,i,static_cast<unsigned>(v));i+=2;}return b;}
Entry name(const char* n){Bytes b;while(*n)b.push_back(static_cast<std::uint8_t>(*n++));b.push_back(0);return Entry(3,b);}
Entry image(int w,int h,int x,int y,unsigned index,bool glyph){
    const std::size_t stride=(static_cast<std::size_t>(w)+3U)&~std::size_t(3);
    Bytes b(14+stride*h,0);put16(b,1,w);put16(b,3,h);put16(b,5,x);put16(b,7,y);
    put32(b,9,stride*h);b[13]=1;
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)
        b[14+static_cast<std::size_t>(h-row-1)*stride+col]=static_cast<std::uint8_t>(!glyph||row==col?index:0);
    return Entry(1,b);
}
table::TableModel fixture(bool invalid_layout=false,bool invalid_font=false,bool missing_digit=false){
    std::vector<Group> groups;
    groups.push_back(Group{name("table_size"),Entry(10,shorts({60,10}))});
    groups.push_back(Group{name("table_objects"),Entry(10,shorts({1025}))});
    groups.push_back(Group{name("background"),image(40,10,20,0,33,false)});
    Bytes palette(1024,0);for(unsigned i=0;i<256;++i){palette[i*4]=static_cast<std::uint8_t>(i*3);palette[i*4+1]=11;palette[i*4+2]=static_cast<std::uint8_t>(i);}
    groups.push_back(Group{Entry(5,palette)});
    groups.push_back(Group{name("score1"),Entry(10,shorts({8,invalid_layout?0:37,4,18,2}))});
    groups.push_back(Group{name("ballcount1"),Entry(10,shorts({invalid_font?9:8,53,0,2,2}))});
    groups.push_back(Group{name("player_number1"),Entry(10,shorts({8,25,4,2,2}))});
    groups.push_back(Group{name("info_text_box"),Entry(10,shorts({1500,25,7,30,2}))});
    for(unsigned i=0;i<(missing_digit?9U:10U);++i)
        groups.push_back(Group{image(2,2,-static_cast<int>(i),static_cast<int>(i),i+1,true)});
    Bytes encoded(183,0);const char signature[]="PARTOUT(4.0)RESOURCE";
    std::copy(signature,signature+sizeof(signature),encoded.begin());put16(encoded,175,static_cast<unsigned>(groups.size()));
    for(const Group& group:groups){encoded.push_back(static_cast<std::uint8_t>(group.size()));for(const Entry& entry:group){
        encoded.push_back(entry.type);const std::size_t at=encoded.size();encoded.resize(at+4);put32(encoded,at,entry.bytes.size());
        encoded.insert(encoded.end(),entry.bytes.begin(),entry.bytes.end());}}
    put32(encoded,171,encoded.size());put32(encoded,177,encoded.size()-183);
    return table::TableModel::from_dat(data::DatFile::parse(encoded));
}
scene::Surface base(unsigned width,unsigned height){
    scene::Surface result(width,height);
    for(std::size_t i=0;i<result.depth.size();++i){result.depth[i]=static_cast<std::uint16_t>(1000+i%500);
        result.pixels[i*4]=90;result.pixels[i*4+1]=static_cast<std::uint8_t>(i);result.pixels[i*4+2]=77;result.pixels[i*4+3]=255;}
    return result;
}
unsigned red(const scene::Surface& s,int x,int y){return s.pixels[(static_cast<std::size_t>(y)*s.width+x)*4];}
bool in(const scene::Rect& r,int x,int y){return x>=r.x&&y>=r.y&&x-r.x<r.width&&y-r.y<r.height;}
void same_pixel(const scene::Surface& a,const scene::Surface& b,int x,int y){
    const std::size_t i=(static_cast<std::size_t>(y)*a.width+x)*4;
    check(std::equal(a.pixels.begin()+i,a.pixels.begin()+i+4,b.pixels.begin()+i),"HUD changed a protected pixel");
}
void synthetic_tests(){
    const table::TableModel model=fixture();const hud::ScoreDisplay display(model);
    check(display.layout().score.x==37&&display.layout().score_restore.x==35&&display.layout().digit_group==8,"synthetic metadata/ten-digit extent");
    const scene::Surface background=base(60,10);scene::Surface target=background;hud::HudState state;
    check(state.score==0&&state.ball_number==1&&state.player==1&&!state.game_over&&state.visible,"initial HUD state");
    display.render(target,background,state);
    check(red(target,53,4)==1&&red(target,25,4)==2&&red(target,53,0)==2,"initial zero/ball/player digit order");
    same_pixel(target,background,54,4);same_pixel(target,background,53,5);
    check(red(target,54,5)==1,"digit top-down mask/orientation");
    state.score=1023;display.render(target,background,state);
    check(red(target,47,4)==2&&red(target,49,4)==1&&red(target,51,4)==3&&red(target,53,4)==4,"decimal order/right alignment/no gap");
    state.score=1000000000ULL;display.render(target,background,state);
    check(red(target,35,4)==2&&red(target,37,4)==1&&red(target,53,4)==1,"exact billion must show all ten digits");
    state.score=0;display.render(target,background,state);same_pixel(target,background,35,4);same_pixel(target,background,47,4);
    state.ball_number=0;state.game_over=true;display.render(target,background,state);
    for(int y=0;y<2;++y)for(int x=53;x<55;++x)same_pixel(target,background,x,y);
    check(red(target,25,4)==2&&red(target,53,4)==1,"gameover must preserve player/score while clearing ball");
    check(target.depth==background.depth,"numeric HUD must preserve all depth");
    state.visible=false;display.render(target,background,state);
    check(target.pixels==background.pixels,"hidden HUD must restore every numeric pixel including extended score");
    state.visible=true;state.score=1023;display.render(target,background,state);
    const scene::Surface before=target;
    state.score=1000000001ULL;error([&]{display.render(target,background,state);});check(target.pixels==before.pixels,"invalid score mutated target");
    state.score=0;state.player=0;error([&]{display.render(target,background,state);});state.player=10;error([&]{display.render(target,background,state);});
    state.player=1;state.ball_number=10;error([&]{display.render(target,background,state);});state.ball_number=1;
    error([&]{display.render(target,target,state);});
    scene::Surface malformed=target;malformed.pixels.pop_back();error([&]{display.render(malformed,background,state);});
    scene::Surface different(59,10);error([&]{display.render(target,different,state);});
    error([&]{hud::ScoreDisplay invalid(fixture(true));});error([&]{hud::ScoreDisplay invalid(fixture(false,true));});error([&]{hud::ScoreDisplay invalid(fixture(false,false,true));});
    for(std::size_t i=0;i<target.depth.size();++i){target.pixels[i*4]=201;target.pixels[i*4+1]=202;target.pixels[i*4+2]=203;target.pixels[i*4+3]=255;}
    const scene::Surface dirty=target;
    display.render(target,background,state,scene::Rect(52,4,2,1));
    check(red(target,52,4)==90&&red(target,53,4)==1,"clip must restore background and draw only covered glyph samples");
    for(int y=0;y<10;++y)for(int x=0;x<60;++x)if(y!=4||x<52||x>=54)same_pixel(target,dirty,x,y);
    const scene::Surface clipped=target;
    display.render(target,background,state,scene::Rect((std::numeric_limits<int>::max)(),0,8,8));
    display.render(target,background,state,scene::Rect((std::numeric_limits<int>::min)(),0,(std::numeric_limits<int>::max)(),8));
    display.render(target,background,state,scene::Rect(0,0,-1,8));
    check(target.pixels==clipped.pixels&&target.depth==clipped.depth,"extreme/empty HUD clips must be no-ops");
}
void save(const std::string& directory,const char* name,const scene::Surface& surface){if(!directory.empty())scene::write_bmp(directory+"/"+name,surface);}
void actual_assets(const std::string& path,const std::string& output){
    const table::TableModel model=table::TableModel::load(path);const hud::ScoreDisplay display(model);
    const hud::HudLayout& layout=display.layout();
    check(layout.digit_group==5&&layout.score.x==433&&layout.score.y==197&&layout.score.width==135&&layout.score.height==22,"actual score1 layout");
    check(layout.ball.x==553&&layout.ball.y==152&&layout.ball.width==15&&layout.player.x==403&&layout.player.y==197,"actual ball/player layout");
    check(layout.info.x==405&&layout.info.y==239&&layout.info.width==165&&layout.info.height==44,"actual info box metadata");
    check(layout.score_restore.x==418&&layout.score_restore.width==150,"actual exact1e9 extended footprint");
    for(unsigned digit=0;digit<10;++digit){const scene::Sprite& image=display.digit(digit);
        check(image.group_index==5+digit&&image.bitmap.width==15&&image.bitmap.height==22,"actual consecutive original digits");
        check(image.depth.values.empty(),"original numeric glyph unexpectedly has depth");}
    scene::ResourceScene scene=scene::static_resource_scene(model);const scene::Surface baseline=scene.surface;
    hud::HudState state;display.render(scene.surface,scene.background,state);save(output,"hud-initial.bmp",scene.surface);
    for(int y=0;y<416;++y)for(int x=0;x<600;++x)
        if(!in(layout.score_restore,x,y)&&!in(layout.ball,x,y)&&!in(layout.player,x,y))same_pixel(scene.surface,baseline,x,y);
    state.score=1000000000ULL;display.render(scene.surface,scene.background,state);save(output,"hud-billion.bmp",scene.surface);
    for(unsigned n=0;n<10;++n){const scene::Sprite& image=display.digit(n==0?1:0);
        for(unsigned y=0;y<22;++y)for(unsigned x=0;x<15;++x){const std::size_t source=y*15+x;
            const std::size_t dest=(static_cast<std::size_t>(197+y)*600+418+n*15+x)*4;
            const std::vector<std::uint8_t>& expected=image.indices[source]?image.bitmap.pixels:scene.background.pixels;
            const std::size_t offset=image.indices[source]?source*4:dest;
            check(std::equal(expected.begin()+offset,expected.begin()+offset+4,scene.surface.pixels.begin()+dest),"actual ten-digit palette/key/layout mismatch");}}
    state.score=12345;state.ball_number=0;state.game_over=true;
    display.render(scene.surface,scene.background,state);save(output,"hud-gameover-numbers.bmp",scene.surface);
    for(int y=152;y<174;++y)for(int x=553;x<568;++x)same_pixel(scene.surface,scene.background,x,y);
    for(int y=197;y<219;++y)for(int x=418;x<433;++x)same_pixel(scene.surface,scene.background,x,y);
    check(scene.surface.depth==baseline.depth,"actual HUD changed playfield/sidebar depth");
    state.visible=false;display.render(scene.surface,scene.background,state);
    check(scene.surface.pixels==baseline.pixels&&scene.surface.depth==baseline.depth,"actual HUD hide did not restore exact original scene");
    std::cout<<"Original HUD assets PASS: digits5..14, score/ball/player rectangles, ten-digit1e9, keyed palette, blank ball0, immutable background/depth.\n";
}
}
int main(int argc,char** argv){
    try{if(argc>3)throw std::runtime_error("Usage: score_display_tests [PINBALL.DAT [existing-output-directory]]");
        synthetic_tests();
        if(argc>1)actual_assets(argv[1],argc>2?argv[2]:"");
#if defined(__XSDK__)
        else actual_assets("game:\\PINBALL.DAT","");
#endif
        std::cout<<"Score display tests PASS: original-order decimal layout, restore/transparency, exact1e9, ball0/gameover, clipping and validation.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Score display test failed: "<<e.what()<<'\n';return 1;}
}
