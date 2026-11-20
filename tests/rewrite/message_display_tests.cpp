#include "message_display.hpp"
#include "table_model.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace pinball;
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void error(const std::function<void()>& run) {
    try { run(); } catch (const std::exception&) { return; }
    throw std::runtime_error("expected message-display validation error");
}
void put16(std::vector<std::uint8_t>& b,std::size_t at,unsigned value) {
    b.at(at)=static_cast<std::uint8_t>(value);b.at(at+1)=static_cast<std::uint8_t>(value>>8);
}
font::BitmapFont test_font() {
    std::vector<std::uint8_t> bytes(134,0);
    put16(bytes,0,1);put16(bytes,2,15);put16(bytes,4,2);
    for (unsigned c=32;c<128;++c) {
        const unsigned w=c=='B'?2:1;bytes[6+c]=static_cast<std::uint8_t>(w);
        bytes.push_back(static_cast<std::uint8_t>(w));
        if(c==' ') {bytes.push_back(0);bytes.push_back(0);}
        else if(c=='B') {bytes.push_back(100);bytes.push_back(0);bytes.push_back(0);bytes.push_back(48);}
        else {bytes.push_back(48);bytes.push_back(55);}
    }
    return font::BitmapFont::parse(bytes);
}
font::ColorLookup colors() {
    font::ColorLookup result={};
    for (unsigned i=0;i<256;++i) result[i].a=255;
    result[48].r=48;result[55].g=55;result[100].b=100;
    return result;
}
hud::MessageLayout layout() {
    hud::MessageLayout result;
    result.sidebar=scene::Rect(2,0,10,10);result.info=scene::Rect(3,1,8,4);
    result.mission=scene::Rect(3,6,8,4);return result;
}
std::size_t sample(const scene::Surface& s,int x,int y) { return static_cast<std::size_t>(y)*s.width+x; }
unsigned channel(const scene::Surface& s,int x,int y,unsigned c) {return s.pixels.at(sample(s,x,y)*4+c);}
void same(const scene::Surface& a,const scene::Surface& b,int x,int y) {
    for(unsigned c=0;c<4;++c) check(channel(a,x,y,c)==channel(b,x,y,c),"pixel outside message change differs");
}
bool in(const scene::Rect& r,int x,int y) {return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
void synthetic() {
    const font::BitmapFont font=test_font();const hud::MessageDisplay display(layout(),font,colors());
    scene::Surface background(12,10);pinball360::Rgba8 color={7,9,11,255};background.clear(color,999);
    scene::Surface target=background;
    for(std::size_t i=0;i<target.depth.size();++i)target.depth[i]=static_cast<std::uint16_t>(i);
    const std::vector<std::uint16_t> depths=target.depth;
    hud::MessageState state;state.info="A";state.mission="B";
    display.render(target,background,state);
    check(channel(target,3,1,0)==48&&channel(target,3,2,1)==55,"font top-down orientation/index colors");
    check(channel(target,3,6,2)==100&&channel(target,4,7,0)==48,"mission box placement");
    same(target,background,4,6);same(target,background,3,7);
    check(target.depth==depths,"message drawing/restoration must preserve existing depth");
    state.info="AA AA";state.mission.clear();display.render(target,background,state);
    check(channel(target,3,1,0)==48&&channel(target,5,1,0)==48&&channel(target,3,3,0)==48&&channel(target,5,3,0)==48,"word wrap at font-gap boundary");
    same(target,background,7,1);same(target,background,7,3);
    state.info="AAAAA";display.render(target,background,state);
    check(channel(target,9,1,0)==48&&channel(target,3,3,0)==48,"long unbroken word continues next line");
    same(target,background,5,3);
    state.info="A\r\nB\nA";display.render(target,background,state);
    check(channel(target,3,1,0)==48&&channel(target,3,3,2)==100,"CRLF normalization and full-line height limit");
    state.info="\nA";display.render(target,background,state);
    same(target,background,3,1);check(channel(target,3,3,0)==48,"explicit empty line consumes one font height");
    state.info=" AAAAA";display.render(target,background,state);
    check(channel(target,3,3,0)==48,"leading-space wrap must make progress");
    state.info="A";state.mission="B";display.render(target,background,state);
    const scene::Surface before=target;
    state.info="A\tB";error([&]{display.render(target,background,state);});check(target.pixels==before.pixels,"invalid message partially mutated target");
    state.info="A";state.mission=std::string(1,static_cast<char>(128));error([&]{display.render(target,background,state);});
    check(target.pixels==before.pixels,"invalid second message mutated first box");
    state.mission="B\r";error([&]{display.render(target,background,state);});
    state.info="A";state.mission="B";state.visible=false;display.render(target,background,state);
    check(target.pixels==background.pixels&&target.depth==depths,"hidden messages must restore both boxes exactly");
    state.visible=true;state.info="";state.mission="";display.render(target,background,state);
    check(target.pixels==background.pixels,"empty message erases");
    for(std::size_t i=0;i<target.pixels.size();i+=4)target.pixels[i]=201;
    const scene::Surface dirty=target;
    state.info="B";state.mission="A";
    display.render(target,background,state,scene::Rect(4,1,1,1));
    same(target,background,4,1); // zero key must reveal restored original background
    for(int y=0;y<10;++y)for(int x=0;x<12;++x)if(x!=4||y!=1)same(target,dirty,x,y);
    const scene::Surface clipped=target;
    display.render(target,background,state,scene::Rect((std::numeric_limits<int>::min)(),0,(std::numeric_limits<int>::max)(),10));
    display.render(target,background,state,scene::Rect((std::numeric_limits<int>::max)(),0,10,10));
    display.render(target,background,state,scene::Rect(0,0,-1,10));
    check(target.pixels==clipped.pixels&&target.depth==depths,"empty/extreme clipping must preserve surface");
    error([&]{display.render(target,target,state);});
    scene::Surface bad=target;bad.depth.pop_back();error([&]{display.render(bad,background,state);});
    scene::Surface different(11,10);error([&]{display.render(target,different,state);});
    hud::MessageLayout invalid=layout();invalid.info.x=1;error([&]{hud::MessageDisplay p(invalid,font,colors());});
    invalid=layout();invalid.mission=invalid.info;error([&]{hud::MessageDisplay p(invalid,font,colors());});
    invalid=layout();invalid.sidebar.x=(std::numeric_limits<int>::max)();error([&]{hud::MessageDisplay p(invalid,font,colors());});
    invalid=layout();invalid.info.width=1;hud::MessageDisplay too_narrow(invalid,font,colors());
    target=background;state.info="B";state.mission.clear();too_narrow.render(target,background,state);
    check(target.pixels==background.pixels,"first glyph too wide must not escape or stall");
    error([&]{display.glyph(31);});error([&]{display.glyph(128);});
}
void actual_assets(const std::string& dat_path,const std::string& font_path,const std::string& output) {
    const table::TableModel model=table::TableModel::load(dat_path);
    const font::BitmapFont font=font::BitmapFont::load(font_path);
    check(font.gap()==2&&font.unknown_header_value()==15&&font.height()==22&&font.glyphs().size()==96,"actual XP font metadata");
    std::size_t indices[256]={};
    for(unsigned c=32;c<128;++c)for(std::uint8_t index:font.glyph(c).indices)++indices[index];
    check(indices[0]==11966&&indices[48]==4748&&indices[55]==2350&&indices[100]==2408,"actual font resource index counts");
    const hud::MessageDisplay display(model,font);const hud::MessageLayout& l=display.layout();
    check(l.info.x==405&&l.info.y==239&&l.info.width==165&&l.info.height==44,"actual info text box");
    check(l.mission.x==405&&l.mission.y==303&&l.mission.width==165&&l.mission.height==88,"actual mission text box");
    check(l.sidebar.x==386&&l.sidebar.y==12&&l.sidebar.width==203&&l.sidebar.height==394,"actual sidebar bounds");
    scene::ResourceScene scene=scene::static_resource_scene(model);const scene::Surface initial=scene.surface;
    hud::MessageState state;state.info="Bumper\n500 Points";state.mission="Game Over\nPlayer 1\nFinal Score\n60,500";
    display.render(scene.surface,scene.background,state);
    std::size_t changes=0;
    for(int y=0;y<416;++y)for(int x=0;x<600;++x) {
        const bool box=in(l.info,x,y)||in(l.mission,x,y);
        if(!box)same(scene.surface,initial,x,y);
        if(channel(scene.surface,x,y,0)!=channel(initial,x,y,0)||channel(scene.surface,x,y,1)!=channel(initial,x,y,1)||channel(scene.surface,x,y,2)!=channel(initial,x,y,2))++changes;
    }
    check(changes>500,"actual original message glyphs produced too few visible pixels");
    check(scene.surface.depth==initial.depth,"actual original message rendering altered depth");
    if(!output.empty())scene::write_bmp(output+"/message-original-xp.bmp",scene.surface);
    state.visible=false;display.render(scene.surface,scene.background,state);
    check(scene.surface.pixels==initial.pixels&&scene.surface.depth==initial.depth,"actual text hide must restore scene exactly");
}
}
int main(int argc,char** argv) {
    try {
        synthetic();
#if defined(__XSDK__)
        actual_assets(argc>1?argv[1]:"game:\\PINBALL.DAT",argc>2?argv[2]:"game:\\PBMSG_FT.bin",argc>3?argv[3]:"");
#else
        if(argc>2)actual_assets(argv[1],argv[2],argc>3?argv[3]:"");
#endif
        std::cout<<"message_display tests PASS\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"message_display tests FAIL: "<<e.what()<<'\n';return 1;}
}
