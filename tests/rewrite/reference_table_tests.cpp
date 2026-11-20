#include "reference_table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using pinball::reference::TablePhysics;
using pinball::reference::material_for_group;
using pinball::table::TableModel;
using pinball::response::Material;
typedef std::vector<std::uint8_t> Bytes;
struct Entry {
    std::uint8_t type;
    Bytes bytes;
    Entry(std::uint8_t t,Bytes b):type(t),bytes(std::move(b)) {}
};
typedef std::vector<Entry> Group;
typedef std::vector<Group> Groups;
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void close(double actual,double expected,const char* message,double epsilon=.00001) {
    check(std::isfinite(actual) && std::fabs(actual-expected)<=epsilon,message);
}
template<class Exception,class Operation>
void throws(Operation operation,const char* message) {
    try { operation(); } catch (const Exception&) { return; }
    throw std::runtime_error(message);
}
void put16(Bytes& b,std::size_t at,std::uint16_t n) {
    b.at(at)=static_cast<std::uint8_t>(n);b.at(at+1)=static_cast<std::uint8_t>(n>>8);
}
void put32(Bytes& b,std::size_t at,std::uint32_t n) {
    for (unsigned i=0;i<4;++i) b.at(at+i)=static_cast<std::uint8_t>(n>>(8*i));
}
Entry shorts(std::uint8_t type,std::initializer_list<std::int16_t> values) {
    Bytes b(values.size()*2);std::size_t at=0;
    for (std::initializer_list<std::int16_t>::const_iterator i=values.begin();i!=values.end();++i,at+=2)
        put16(b,at,static_cast<std::uint16_t>(*i));
    return Entry(type,b);
}
Entry floats(std::initializer_list<float> values) {
    Bytes b(values.size()*4);std::size_t at=0;
    for (std::initializer_list<float>::const_iterator i=values.begin();i!=values.end();++i,at+=4) {
        std::uint32_t bits=0;std::memcpy(&bits,&*i,4);put32(b,at,bits);
    }
    return Entry(11,b);
}
Entry name(const std::string& text) {
    Bytes b(text.begin(),text.end());b.push_back(0);return Entry(3,b);
}
Entry image() {
    Bytes b(15,0);put16(b,1,1);put16(b,3,1);put32(b,9,1);b[13]=1;b[14]=2;
    return Entry(1,b);
}
Groups fixture() {
    Groups g(40);
    g[0]=Group{name("table_size"),shorts(10,{600,416})};
    g[1]=Group{name("table_objects"),shorts(10,{1025,1000,7,1000,8,1000,12,
        1010,13,1016,15,1001,4,1003,5,1004,6,1000,7,1017,16,1017,17,1017,18,1017,22,1012,23,1021,25,1021,26,1022,27,1007,28,1016,29,1016,30,1006,31,1006,32,1006,33,1026,35,1002,36,1002,37,1002,38,1002,39})};
    g[2]=Group{name("table"),floats({305,25,.5f,1.570796012878418f}),floats({701,.2f})};
    g[3]=Group{name("ball"),floats({500,.3f})};
    g[4]=Group{shorts(0,{200}),name("plunger"),shorts(10,{100,1}),
        floats({600,2,-8,12,-6,12,-1}),floats({601,-7,10,.13f,-1})};
    for (unsigned i=0;i<8;++i) g[4].push_back(image());
    g[5]=Group{shorts(0,{200}),name("a_flip1"),shorts(10,{100,1,300,9}),
        floats({800,2,10,.31f,-1}),floats({801,1,11,.19f,-1}),
        floats({802,1,9,.19f,-1}),floats({803,.9f}),floats({804,.04f}),floats({805,.08f})};
    g[6]=Group{shorts(0,{200}),name("a_flip2"),shorts(10,{100,1,300,9}),
        floats({800,-2,10,.31f,-1}),floats({801,-1,11,.19f,-1}),
        floats({802,-1,9,.19f,-1}),floats({803,.9f}),floats({804,.04f}),floats({805,.08f})};
    g[7]=Group{shorts(0,{200}),name("wall"),shorts(10,{100,1,300,10}),floats({600,2,-1,0,1,0,-1})};
    g[8]=Group{shorts(0,{200}),name("powered"),shorts(10,{100,1,400,11}),floats({600,2,0,0,0,1,-.9965f})};
    g[9]=Group{shorts(0,{300}),floats({301,.5f,302,.75f})};
    g[10]=Group{shorts(0,{300}),floats({301,.8f,302,.5f,304,42})};
    // The vector deliberately resembles other attribute tags. It must remain
    // an ignored three-component throw vector rather than parsed as pairs.
    g[11]=Group{shorts(0,{400}),floats({401,1,402,18,403,.2f,404,300,301,302,405,.1f,406,22})};
    g[12]=Group{shorts(0,{200}),name("excluded"),shorts(10,{100,1,602,2,602,1,602,2,300,10}),floats({600,2,3,0,4,0,-5})};
    g[13]=Group{shorts(0,{200}),name("disk"),shorts(10,{100,1,300,14}),floats({600,1,0,2,.5f,0})};
    g[14]=Group{shorts(0,{300}),floats({301,.6f,302,.6f})};
    g[15]=Group{shorts(0,{200}),name("unselected"),shorts(10,{100,1}),floats({600,2,3,0,4,0,-1})};
    g[16]=Group{shorts(0,{200}),name("v_sink1"),shorts(10,{100,1,400,19}),
        floats({600,2,2,-5,4,-5,-1}),floats({601,3,-4,.1f,-1}),floats({407,2})};
    g[17]=Group{shorts(0,{200}),name("v_sink2"),shorts(10,{100,1,400,20}),
        floats({600,2,2,-7,4,-7,-1}),floats({601,3,-6,.1f,-1}),floats({407,2})};
    g[18]=Group{shorts(0,{200}),name("v_sink3"),shorts(10,{100,1,400,21}),
        floats({600,2,-5,2,-3,2,-1}),floats({601,-4,3,.1f,-1}),floats({407,2})};
    g[19]=Group{shorts(0,{400}),floats({402,35,403,5,404,1,1,0,405,0})};
    g[20]=Group{shorts(0,{400}),floats({402,35,403,5,404,1,-1,0,405,0})};
    g[21]=Group{shorts(0,{400}),floats({402,35,403,5,404,1,1,0,405,0})};
    // The fourth sink belongs to other game behavior and is deliberately not
    // loaded as a default wormhole, even though it has the same numeric type.
    g[22]=Group{shorts(0,{200}),name("v_sink7"),shorts(10,{100,1,400,19}),
        floats({600,2,-30,50,-28,50,-1}),floats({601,-25,40,.1f,-1}),floats({407,2})};
    g[23]=Group{shorts(0,{200}),name("a_kout3"),shorts(10,{400,24}),
        floats({600,1,4,-.25f,.4f,-1}),floats({306,.2f}),floats({305,32}),floats({408,0,0,0})};
    g[24]=Group{shorts(0,{400}),floats({402,35,403,5,404,-1,-1,0,405,0})};
    g[25]=Group{shorts(0,{200}),name("ramp"),shorts(10,{602,1}),
        floats({1300,2,0,-1,2,2,2,0,2,0,0,.785398163f,1.570796327f,0,0,
            0,-1,2,2,2,0,0,2,0,.785398163f,1.570796327f,0,0}),floats({701,.2f}),
        floats({1301,0,1,2,2,2,0,2,-1}),floats({1302,2,1,2,0,0,2,0,-1}),
        floats({1303,2,2,10,0,10,1,-1}),floats({1305,0})};
    g[26]=Group{shorts(0,{200}),name("s_ramp9"),shorts(10,{602,1}),
        floats({1300,1,0,0,-5,-10,0,-12,0,-10,-2,0,0,0,0}),floats({701,.2f}),
        floats({1301,0,0,2,-12,0,-10,-2,0}),floats({1302,0,1,2,-10,0,-12,0,0}),
        floats({1303,1,2,-10,0,-12,0,0}),floats({1305,1})};
    g[27]=Group{shorts(0,{200}),name("ramp_hole"),shorts(10,{602,2}),
        floats({600,1,1,-1,.4f,0}),floats({306,.1f}),floats({305,10}),floats({408,1,-1,.325f,0}),
        floats({1304,1}),floats({701,.2f}),floats({407,.05f})};
    g[28]=Group{shorts(0,{200}),name("drain"),
        floats({600,2,-20,20,20,20,0}),floats({407,1.82f})};
    g[29]=Group{shorts(0,{200}),name("s_onewy1"),floats({600,2,-8,5,-6,5,-1})};
    g[30]=Group{shorts(0,{200}),name("s_onewy10"),floats({600,2,-8,3,-6,3,-1})};
    for (unsigned i=0;i<3;++i) {
        const float y=-8.0f+2*i;
        g[31+i]=Group{shorts(0,{200}),name("a_targ"+std::to_string(7+i)),
            shorts(10,{100,1,400,34}),floats({600,5,10,y,10,y+1,9.9f,y+1,9.9f,y,10,y,-1}),
            floats({407,.25f}),image()};
    }
    g[32][2]=shorts(10,{100,1,400,34,300,10,602,2});
    g[34]=Group{shorts(0,{400}),floats({401,3,402,5})};
    g[35]=Group{shorts(0,{200}),name("top_target_lights"),shorts(10,{1027,36,37,38,39}),floats({903,.08f})};
    for (unsigned i=0;i<4;++i)
        g[36+i]=Group{shorts(0,{200}),name("bank_lamp"+std::to_string(i)),image(),
            floats({901,.1f}),floats({900,.1f})};

    return g;
}
void object(Groups& groups,std::int16_t type,std::size_t group) {
    Bytes& data=groups[1][1].bytes;
    const std::size_t at=data.size();data.resize(at+4);
    put16(data,at,static_cast<std::uint16_t>(type));
    put16(data,at+2,static_cast<std::uint16_t>(group));
}
Groups lane_fixture() {
    Groups g=fixture();g.resize(63);
    const char* names[]={"a_roll1","a_roll2","a_roll3","a_roll110","a_roll111","a_roll112"};
    for (unsigned i=0;i<6;++i) {
        const float x=30.0f+3*i;
        g[40+i]=Group{shorts(0,{200}),name(names[i]),shorts(10,{602,static_cast<std::int16_t>(i<3?0:2)}),
            floats({600,5,x+1,51,x,51,x,50,x+1,50,x+1,51,-1}),
            floats({603,5,x+1,51,x+1,50,x,50,x,51,x+1,51,-1}),image()};
        object(g,1015,40+i);
    }
    const char* lamps[]={"lite8","lite9","lite10","lite171","lite170","lite169"};
    for (unsigned i=0;i<6;++i) {
        g[46+i]=Group{shorts(0,{200}),name(lamps[i]),image(),floats({901,.1f}),floats({900,.1f})};
        object(g,1002,46+i);
    }
    g[52]=Group{shorts(0,{200}),name("bmpr_inc_lights"),shorts(10,{1027,46,47,48})};
    g[53]=Group{shorts(0,{200}),name("ramp_bmpr_inc_lights"),shorts(10,{1027,49,50,51})};
    object(g,1026,52);object(g,1026,53);
    for (unsigned i=0;i<7;++i) {
        g[54+i]=Group{shorts(0,{200}),name("a_bump"+std::to_string(i+1)),
            shorts(10,{100,1,400,34,602,static_cast<std::int16_t>(i<4?0:2)}),
            floats({600,1,60.0f+2*i,60,.4f,-1}),floats({407,.1f})};
        for (unsigned state=0;state<8;++state) g[54+i].push_back(image());
        object(g,1005,54+i);
    }
    g[61]=Group{shorts(0,{200}),name("attack_bumpers"),shorts(10,{1027,54,55,56,57})};
    g[62]=Group{shorts(0,{200}),name("launch_bumpers"),shorts(10,{1027,58,59,60})};
    object(g,1028,61);object(g,1028,62);
    return g;
}
Groups medal_fixture() {
    Groups g=fixture();g.resize(47);
    for (unsigned i=0;i<3;++i) {
        const float x=20.0f+i*2;
        g[40+i]=Group{shorts(0,{200}),name("a_targ"+std::to_string(4+i)),
            shorts(10,{400,34}),floats({600,5,x,4,x,5,x-1,5,x-1,4,x,4,-1}),
            floats({407,.25f}),image()};
        object(g,1006,40+i);
        g[44+i]=Group{shorts(0,{200}),name("medal_lamp"+std::to_string(i)),image(),
            floats({900,.1f}),floats({901,.1f})};
        object(g,1002,44+i);
    }
    g[43]=Group{shorts(0,{200}),name("bumper_target_lights"),shorts(10,{1027,46,45,44})};
    object(g,1026,43);
    return g;
}
Groups advanced_fixture() {
    Groups g=fixture();g.resize(76);
    g[40]=Group{shorts(0,{200}),name("a_targ13"),shorts(10,{400,34}),
        floats({600,5,20,0,20,1,19,1,19,0,20,0,-1}),image(),image()};
    object(g,1019,40);
    g[41]=Group{shorts(0,{200}),name("lite101"),image(),floats({900,.1f}),floats({901,.1f})};object(g,1002,41);
    g[42]=Group{shorts(0,{200}),name("fuel_bargraph"),shorts(10,{1027,43,44,45,46,47,48}),
        floats({904,3,14,3,12,3,10,3,8,3,6,3,4})};object(g,1030,42);
    for (unsigned i=0;i<6;++i) {
        g[43+i]=Group{shorts(0,{200}),name("fuel_lamp"+std::to_string(i)),image(),floats({900,.1f}),floats({901,.1f})};
        object(g,1002,43+i);
    }
    g[49]=Group{shorts(0,{200}),name("s_trip1"),floats({600,2,0,80,2,80,-1})};object(g,1024,49);
    g[50]=Group{shorts(0,{200}),name("lite68"),image(),floats({900,.1f}),floats({901,.1f})};object(g,1002,50);
    g[51]=Group{shorts(0,{200}),name("a_roll179"),
        floats({600,5,31,51,30,51,30,50,31,50,31,51,-1}),
        floats({603,5,31,51,31,50,30,50,30,51,31,51,-1})};object(g,1015,51);
    g[52]=Group{shorts(0,{200}),name("literoll179"),image(),floats({900,.1f}),floats({901,.1f})};object(g,1002,52);
    g[53]=Group{shorts(0,{200}),name("a_roll9"),
        floats({600,7,2,30,1,32,-1,32,-2,30,-1,28,1,28,2,30,-1}),
        floats({603,7,2,30,1,28,-1,28,-2,30,-1,32,1,32,2,30,-1}),floats({407,.15f}),image()};object(g,1020,53);
    g[54]=Group{shorts(0,{200}),name("a_kout2"),shorts(10,{400,55}),
        floats({600,1,-30,-30,.4f,-1}),floats({306,.9f}),floats({305,32}),floats({408,0,0,0})};object(g,1012,54);
    g[55]=Group{shorts(0,{400}),floats({402,35,403,5,404,-1,1,0,405,0})};
    for (unsigned i=0;i<4;++i) {
        g[56+i]=Group{shorts(0,{200}),name("hyper_lamp"+std::to_string(i)),image(),floats({900,.1f}),floats({901,.1f})};
        object(g,1002,56+i);
    }
    g[60]=Group{shorts(0,{200}),name("hyperspace_lights"),shorts(10,{1027,56,57,58,59})};object(g,1026,60);
    const char* sounds[]={"soundwave35","soundwave36","soundwave38","soundwave39","soundwave21","soundwave40","soundwave41"};
    for (unsigned i=0;i<7;++i) {
        g[61+i]=Group{shorts(0,{200}),name(sounds[i]),shorts(10,{1100,static_cast<std::int16_t>(68+i)})};object(g,1031,61+i);
        Entry filename=name("fixture"+std::to_string(i)+".wav");filename.type=9;
        g[68+i]=Group{shorts(0,{202}),filename};
    }
    g[75]=Group{shorts(0,{200}),name("s_onewy4"),floats({600,2,-12,10,-12,12,-1})};object(g,1016,75);
    return g;
}
Groups moving_fixture() {
    Groups g=fixture();g.resize(55);
    for (unsigned i=0;i<2;++i) {
        const float y=30.0f+2*i;
        g[40+i]=Group{shorts(0,{200}),name("a_flag"+std::to_string(i+1)),
            floats({600,2,30,y,32,y,-1}),floats({1201,5}),floats({1200,50000}),floats({1202,.65f})};
        for (unsigned image_index=0;image_index<8;++image_index) g[40+i].push_back(image());
        object(g,1018,40+i);
        g[42+i]=Group{shorts(0,{200}),name("v_gate"+std::to_string(i+1)),shorts(10,{300,10}),
            floats({600,2,40.0f+2*i,0,40.0f+2*i,1,-1}),image()};
        object(g,1013,42+i);
    }
    g[44]=Group{shorts(0,{200}),name("v_bloc1"),shorts(10,{300,10}),floats({600,1,44,.5f,.175f,-1}),image()};object(g,1011,44);
    const char* lamps[]={"lite30","lite196","lite29","lite195","lite1"};
    for (unsigned i=0;i<5;++i) {
        g[45+i]=Group{shorts(0,{200}),name(lamps[i]),image(),floats({900,.1f}),floats({901,.1f})};object(g,1002,45+i);
    }
    g[50]=Group{shorts(0,{200}),name("a_kout1"),shorts(10,{400,51}),
        floats({600,1,50,5,3.2f}),floats({306,.05f}),floats({305,30}),floats({408,0,0,.3f})};object(g,1029,50);
    g[51]=Group{shorts(0,{400}),floats({402,45,403,5,404,0,-1,0,405,1.2566371f})};
    g[52]=Group{shorts(0,{200}),name("soundwave7"),shorts(10,{1100,53})};object(g,1031,52);
    Entry filename=name("gravity.wav");filename.type=9;g[53]=Group{shorts(0,{202}),filename};
    g[54]=Group{shorts(0,{200}),name("lite62"),image(),floats({900,.2f}),floats({901,.2f})};object(g,1002,54);
    return g;
}
Groups remaining_wall_fixture() {
    Groups g=moving_fixture();g.resize(59);
    for (unsigned i=0;i<2;++i) {
        const float y=30.0f+2*i;
        g[55+i]=Group{shorts(0,{200}),name("a_kick"+std::to_string(i+1)),
            shorts(10,{300,57,400,58}),floats({600,2,60,y,62,y,-1}),image(),image()};
        object(g,1014,55+i);
    }
    g[57]=Group{shorts(0,{300}),floats({301,.7f,302,.2f})};
    g[58]=Group{shorts(0,{400}),floats({402,55})};
    return g;
}
TableModel model(const Groups& g) {
    Bytes b(183,0);const char signature[]="PARTOUT(4.0)RESOURCE";
    for (std::size_t i=0;i<sizeof(signature);++i) b[i]=signature[i];
    put16(b,175,static_cast<std::uint16_t>(g.size()));
    for (std::size_t i=0;i<g.size();++i) {
        b.push_back(static_cast<std::uint8_t>(g[i].size()));
        for (std::size_t j=0;j<g[i].size();++j) {
            const Entry& e=g[i][j];b.push_back(e.type);
            if (e.type!=0 && e.type!=2 && e.type!=13) {
                const std::size_t at=b.size();b.resize(at+4);put32(b,at,static_cast<std::uint32_t>(e.bytes.size()));
            }
            b.insert(b.end(),e.bytes.begin(),e.bytes.end());
        }
    }
    put32(b,171,static_cast<std::uint32_t>(b.size()));put32(b,177,static_cast<std::uint32_t>(b.size()-183));
    return TableModel::from_dat(pinball::data::DatFile::parse(std::move(b)));
}
void material_checks(const Material& m,double e,double s,double t,double boost) {
    close(m.elasticity,e,"material elasticity");close(m.smoothness,s,"material smoothness");
    close(m.threshold,t,"material threshold",t>1e8?8192:.00001);close(m.boost,boost,"material boost");
}
void synthetic() {
    const TableModel data=model(fixture());const TablePhysics p(data);
    close(p.ball_radius,.3,"synthetic ball radius");
    close(p.feed_position.x,-7,"feed X");close(p.feed_position.y,10,"feed Y");
    close(p.seat_position.x,-7,"seat must use feed X");close(p.seat_position.y,11.7,"seat from striker minus radius");
    check(p.plunger_image_count==8 && p.plunger_collider_index==4 && p.static_colliders.size()==42,
        "selection, duplicate suppression or plunger append");
    check(p.collider_groups.size()==p.static_colliders.size() && p.collider_groups[0]==7 &&
        p.collider_groups[1]==8 && p.collider_groups[2]==12 && p.collider_groups[3]==13 && p.collider_groups[4]==4,"collider group provenance");
    check(p.ground_excluded_groups==std::vector<std::size_t>(1,12) &&
        p.ground_excluded_collider_count==1,"ground exclusion provenance");
    check(pinball::reference::collision_mask_for_group(data,12)==6 &&
        pinball::reference::collision_mask_for_group(data,7)==1,"OR bit positions/default ground mask");
    material_checks(p.static_colliders[0].material,.5,.8,8.9999999e10,0);
    material_checks(p.static_colliders[1].material,.6,.95,1,18);
    material_checks(p.static_colliders[2].material,.5,.8,8.9999999e10,0);
    material_checks(p.static_colliders[3].material,.6,.6,8.9999999e10,0);
    material_checks(p.static_colliders[4].material,.5,.5,1e9,0);
    check(p.static_colliders[3].kind==pinball::motion::ColliderKind::Disk,"circle shape not retained");
    for (std::size_t i=0;i<p.static_colliders.size();++i) {
        check(p.static_colliders[i].reference_response!=p.static_colliders[i].center_crossing,"response/transition role");
        check(p.static_colliders[i].captures_ball==((i>=5 && i<=8) || i==21 || i==22 || i==p.advanced.escape_chute.collider_index),"capture role not retained");
    }
    check(p.sinks.size()==3,"default wormhole count");
    for (std::size_t i=0;i<p.sinks.size();++i) {
        const pinball::reference::SinkProfile& sink=p.sinks[i];
        check(sink.group==16+i && sink.collider_index==5+i,"synthetic wormhole indices");
        close(sink.speed,35,"sink scalar speed");close(sink.speed_variation,.05,"sink percent conversion");
        close(sink.angle_multiplier,0,"sink angle multiplier");
        close(sink.direction.x,1,"sink raw direction X");close(sink.direction.y,i==1?-1:1,"sink raw direction Y");
        check(sink.delay==2000000000,"sink two-second delay");
    }
    check(p.kickouts.size()==1,"only default black-hole kickout loaded");
    const pinball::reference::KickoutProfile& black=p.kickouts[0];
    check(black.group==23 && black.collider_index==8,"synthetic kickout provenance");
    close(black.center.x,4,"kickout center X");close(black.center.y,-.25,"kickout center Y");
    close(black.field_radius,.4,"kickout field radius");close(black.capture_radius,.08,"kickout center trigger radius");
    close(black.field_strength,32,"kickout radial field strength");
    close(black.direction.x,-1,"kickout raw direction X");close(black.direction.y,-1,"kickout raw direction Y");
    close(black.speed,35,"kickout scalar speed");close(black.speed_variation,.05,"kickout spread");
    close(black.angle_multiplier,0,"kickout angle");close(black.held_z,0,"kickout held Z");
    check(black.delay==1500000000 && black.cooldown==50000000,"kickout timing");
    const pinball::motion::StaticCollider& sensor=p.static_colliders[black.collider_index];
    check(sensor.kind==pinball::motion::ColliderKind::Disk && sensor.center_trigger && sensor.captures_ball,
        "kickout must use a ball-center sensor");
    close(sensor.radius,black.capture_radius,"sensor must not add ball radius");
    pinball::motion::BallState probe(pinball::physics::Vec2(4.2f,-.25f),pinball::physics::Vec2(-1,0),.3f);
    pinball::motion::BallStepper stepper(pinball::motion::MotionConfig(pinball::physics::Vec2(),1000000000,4,.0001f));
    std::vector<pinball::motion::StaticCollider> isolated(1,sensor);
    check(!stepper.step(probe,100000000,isolated).captured,"trigger incorrectly inflated by ball radius");
    const pinball::motion::MotionStep captured=stepper.step(probe,100000000,isolated);
    check(captured.captured && captured.capture_collider_index==0,"kickout sensor did not capture center");
    close(probe.position.x,4.08,"terminal center trigger position");
    const pinball::motion::StaticCollider& wall=p.static_colliders[0];
    check(!wall.two_sided,"wall still two-sided");
    check(pinball::physics::sweep_circle_segment(pinball::physics::Vec2(0,-1),
        pinball::physics::Vec2(0,1),.1f,wall.first,wall.second,2,wall.two_sided).hit,"source wall front missed");
    check(!pinball::physics::sweep_circle_segment(pinball::physics::Vec2(0,1),
        pinball::physics::Vec2(0,-1),.1f,wall.first,wall.second,2,wall.two_sided).hit,"source wall back collided");
    const pinball::motion::StaticCollider& capture=p.static_colliders[p.sinks[0].collider_index];
    check(!capture.two_sided && capture.captures_ball,"sink role/front policy");
    check(pinball::physics::sweep_circle_segment(pinball::physics::Vec2(3,-6),
        pinball::physics::Vec2(0,1),.1f,capture.first,capture.second,2,false).hit,"sink front missed");
    check(!pinball::physics::sweep_circle_segment(pinball::physics::Vec2(3,-4),
        pinball::physics::Vec2(0,-1),.1f,capture.first,capture.second,2,false).hit,"sink back collided");
    material_checks(material_for_group(data,15),.6,.95,8.9999999e10,0);
    close(p.field.gravity.y,11.9856385,"gravity slope projection");
    close(p.field.gravity.x,.00000376249,"gravity azimuth projection",.000000001);
    close(p.field.drag_per_second,.2,"drag701");close(p.field.lateral_noise_per_speed,.1,"source X noise scale");
    close(p.field.maximum_speed,p.ball_radius*200,"ball speed cap");
    for (unsigned i=0;i<2;++i) {
        const pinball::flipper::FlipperConfig& f=p.flippers[i];
        close(f.restitution,.75,"flipper E");close(f.smoothness,.5,"flipper S");close(f.impulse_gain,.9,"flipper gain803");
        check(f.raise_duration==40000000 && f.return_duration==80000000,"float-precision duration conversion");
        check(f.response_mode==pinball::flipper::ResponseMode::ReferenceMaterial,"flipper reference mode");
        close(f.rest_tip_vector.y,1,"rest relative Y");close(f.raised_tip_vector.y,-1,"raised relative Y");
        close(f.rest_tip_vector.x,i?1:-1,"relative X pose");
        close(f.pivot_radius,.31,"pivot radius800");close(f.tip_radius,.19,"tip radius801");
    }
}
void collision_masks_and_trigger_fallback() {
    Groups g=fixture();g[7][2]=shorts(10,{100,1,602,31,602,0,300,10});
    const TableModel high=model(g);
    check(pinball::reference::collision_mask_for_group(high,7)==0x80000001u,"unsigned high collision bit");
    const TablePhysics valid(high);
    check(valid.plunger_collider_index==4 && valid.static_colliders[0].collision_mask==0x80000001u,"all-layer mask was not retained");
    g=fixture();g[23][4]=floats({306,0});
    const TablePhysics fallback(model(g));
    close(fallback.kickouts[0].capture_radius,.001,"explicit zero306 trigger fallback");
}
void ramps_and_hole() {
    const TablePhysics p(model(fixture()));
    check(p.ramp_planes.size()==3 && p.ramp_transition_indices.size()==12,"synthetic ramp topology count");
    for (std::size_t i=0;i<p.ramp_planes.size();++i)
        check(p.ramp_planes[i].id==i && p.ramp_planes[i].surface.id==i && p.ramp_planes[i].surface.active,
            "stable ramp plane IDs");
    std::vector<pinball::motion::StaticCollider> first,second;
    for (std::size_t i=0;i<p.ramp_transition_indices.size();++i) {
        const std::size_t at=p.ramp_transition_indices[i];
        check(p.static_colliders[at].center_crossing,"ramp event inflated into solid geometry");
        (p.collider_groups[at]==25?first:second).push_back(p.static_colliders[at]);
    }
    check(first.size()==8 && second.size()==4,"disabled external entry not honored");
    pinball::motion::BallStepper stepper(pinball::motion::MotionConfig(pinball::physics::Vec2(),3000000000LL,16,.0001f));
    pinball::motion::BallState ball(pinball::physics::Vec2(1,3),pinball::physics::Vec2(0,-1),.3f);
    const pinball::motion::MotionStep ascent=stepper.step(ball,2500000000LL,first);
    check(!ascent.contact_budget_exhausted && ball.collision_mask==2 && ball.plane.active && ball.plane.id==1,
        "ground entry/adjacent plane traversal");
    close(ball.position.y,.5,"ramp center crossing preserves planar motion");close(ball.height,1.8,"constrained height");
    stepper.step(ball,1000000000,first);
    check(ball.collision_mask==4 && !ball.plane.active,"upper exit layer/constraint");close(ball.height,2.3,"exit must preserve height");
    ball.velocity=pinball::physics::Vec2(0,1);stepper.step(ball,1000000000,first);
    check(ball.collision_mask==2 && ball.plane.active,"upper connector reverse entry");close(ball.height,1.8,"descending height");
    stepper.step(ball,2500000000LL,first);
    check(ball.collision_mask==1 && !ball.plane.active,"ground backout layer");close(ball.height,.3,"ground backout height");
    pinball::motion::BallState tunnel(pinball::physics::Vec2(-11,-.5f),pinball::physics::Vec2(0,1),.3f);
    tunnel.collision_mask=2;tunnel.plane=p.ramp_planes[2].surface;tunnel.height=-4.7f;
    stepper.step(tunnel,1000000000,second);
    check(tunnel.collision_mask==1 && !tunnel.plane.active,"reset-height exit layer");close(tunnel.height,.3,"1305 exit reset");
    check(p.holes.size()==1 && p.holes[0].group==27 && p.holes[0].collider_index==21,"synthetic ramp hole");
    const pinball::reference::HoleProfile& hole=p.holes[0];
    close(hole.field_radius,.4,"hole field radius");close(hole.capture_radius,.04,"hole center trigger");
    close(hole.field_strength,10,"hole radial pull");close(hole.landing_z,.325,"hole literal landing Z");
    close(hole.fall_acceleration,5,"hole fall acceleration");
    check(hole.collision_mask==4 && hole.landing_mask==1 && hole.delay==500000000,"hole layer/delay");
    std::vector<pinball::motion::StaticCollider> sensor(1,p.static_colliders[hole.collider_index]);
    pinball::motion::BallState probe(hole.center,pinball::physics::Vec2(),.3f);
    check(!stepper.step(probe,1,sensor).captured,"ground ball hit ramp-layer hole");
    probe.collision_mask=4;
    check(stepper.step(probe,1,sensor).captured,"raised ball missed ramp hole");
    Groups g=fixture();g[27][7]=floats({1304,4});
    const TablePhysics direct(model(g));check(direct.holes[0].landing_mask==4,"1304 treated as bit position");
}
Groups component_fixture() {
    Groups g=fixture();g.resize(43);
    g[1]=Group{name("table_objects"),shorts(10,{1025,1000,7,1000,8,1000,12,
        1010,13,1016,15,1001,4,1003,5,1004,6,1000,7,1017,16,1017,17,1017,18,
        1017,22,1012,23,1021,25,1021,26,1022,27,1007,28,1016,29,1016,30,1006,31,1006,32,1006,33,1026,35,1002,36,1002,37,1002,38,1002,39,1005,40,1005,42,1005,40})};
    g[8][1]=name("v_rebo1");g[8].push_back(image());
    g[40]=Group{shorts(0,{200}),name("a_bump1"),shorts(10,{100,1,400,41,602,2}),
        floats({600,1,4,-6,.4f,-1}),floats({407,.1f}),image(),image()};
    g[41]=Group{shorts(0,{400}),floats({401,2.5f,402,17})};
    g[42]=Group{shorts(0,{200}),name("a_bump2"),shorts(10,{100,1,300,10,400,41}),
        floats({600,1,-4,-6,.35f,-1}),floats({407,.125f}),image(),image()};
    return g;
}
void hit_component_profiles() {
    const TablePhysics p(model(component_fixture()));
    check(p.static_colliders.size()==44 && p.plunger_collider_index==4 &&
        p.holes[0].collider_index==21,"bumper append disturbed existing geometry indices");
    check(p.components.size()==3,"duplicate bumper or missing rebound profile");
    const pinball::reference::ComponentProfile& first=p.components[0];
    const pinball::reference::ComponentProfile& second=p.components[1];
    const pinball::reference::ComponentProfile& wall=p.components[2];
    check(first.kind==pinball::reference::HitComponentKind::Bumper && first.group==40 &&
        first.collider_indices==std::vector<std::size_t>(1,22) &&
        first.idle_frame==0 && first.fired_frame==1 && first.duration==100000000,
        "default bumper level/image/timer/provenance");
    check(second.group==42 && second.collider_indices==std::vector<std::size_t>(1,23) &&
        second.duration==125000000,"component duration must be data-driven");
    material_checks(first.material,.6,.95,2.5,17);
    material_checks(second.material,.5,.8,2.5,17);
    const pinball::motion::StaticCollider& body=p.static_colliders[22];
    check(body.kind==pinball::motion::ColliderKind::Disk && body.reference_response &&
        body.latch_response && !body.center_trigger && !body.captures_ball &&
        body.collision_mask==4,"bumper body/latch/layer flags");
    close(body.radius,.4,"bumper body radius must not include ball radius");
    close(body.first.x,4,"bumper center X");close(body.first.y,-6,"bumper center Y");
    check(wall.kind==pinball::reference::HitComponentKind::Rebound && wall.group==8 &&
        wall.collider_indices==std::vector<std::size_t>(1,1) &&
        wall.idle_frame==-1 && wall.fired_frame==0 && wall.duration==100000000,
        "rebound should reference existing walls and begin hidden");
    material_checks(wall.material,.6,.95,1,18);
    check(!p.static_colliders[1].latch_response,"rebound incorrectly acquired bumper kick suppression");

    std::vector<pinball::motion::StaticCollider> only(1,body);
    pinball::motion::BallStepper stepper(pinball::motion::MotionConfig(pinball::physics::Vec2(),20000000,16,.0001f));
    pinball::motion::BallState wrong(pinball::physics::Vec2(4.72f,-6),pinball::physics::Vec2(-3,0),.3f);
    check(stepper.step(wrong,10000000,only).contacts.empty(),"ground ball collided with raised bumper");
    pinball::motion::BallState hit(pinball::physics::Vec2(4.72f,-6),pinball::physics::Vec2(-3,0),.3f);
    hit.collision_mask=4;
    const pinball::motion::MotionStep result=stepper.step(hit,10000000,only);
    check(result.contacts.size()==1 && result.contacts[0].activated && result.contacts[0].boost_applied,
        "bumper failed strict-threshold activation and powered collision");
    close(result.contacts[0].incident_normal_speed,3,"bumper incident threshold speed");
    close(hit.velocity.x,18.8,"head-on bumper material/kick result",.0001);
    close(hit.velocity.y,0,"head-on bumper introduced tangential velocity");
    // At equality the response's boost remains inclusive while the component
    // activation/latch uses strict greater-than, as separately specified.
    pinball::motion::BallState equal(pinball::physics::Vec2(4.72f,-6),pinball::physics::Vec2(-2.5f,0),.3f);
    equal.collision_mask=4;
    const pinball::motion::MotionStep boundary=stepper.step(equal,10000000,only);
    check(boundary.contacts.size()==1 && boundary.contacts[0].boost_applied && !boundary.contacts[0].activated,
        "response and animation threshold boundary must remain distinct");
    close(equal.velocity.x,18.5,"threshold-equality head-on bumper response",.0001);
    pinball::motion::BallState slow(pinball::physics::Vec2(4.72f,-6),pinball::physics::Vec2(-2,0),.3f);
    slow.collision_mask=4;
    const pinball::motion::MotionStep passive=stepper.step(slow,20000000,only);
    check(passive.contacts.size()==1 && !passive.contacts[0].boost_applied && !passive.contacts[0].activated,
        "below-threshold contact should remain passive");
    close(slow.velocity.x,1.2,"passive bumper elasticity",.0001);

    Groups g=component_fixture();g[40][3]=floats({600,1,4,-6,0,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"zero bumper radius accepted");
    g=component_fixture();g[40][3]=floats({600,2,4,-6,.4f,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"noncircular bumper accepted");
    g=component_fixture();g[40][4]=floats({407,0});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"zero bumper timer accepted");
    g=component_fixture();g[40][4]=floats({407,(std::numeric_limits<float>::quiet_NaN)()});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"nonfinite bumper timer accepted");
    g=component_fixture();g[40].pop_back();
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"missing bumper fired image accepted");
    g=component_fixture();g[40].push_back(image());
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"unpaired bumper images accepted");
    g=component_fixture();g[8].pop_back();
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"missing rebound image accepted");
}
void rule_metadata() {
    using namespace pinball;
    const reference::RuleProfile defaults;
    check(defaults.reflex_duration==5000000000LL && defaults.reflex_award==25000,
        "source reflex metadata defaults");
    const TablePhysics p(model(fixture()));
    const reference::RuleProfile& rules=p.rules;
    check(rules.drain_group==28 && rules.drain_collider_index==22 &&
        rules.skill_gate_group==29 && rules.skill_gate_collider_index==23 &&
        rules.ramp_score_group==25 && rules.ramp_score_collider_index==24 &&
        rules.skill_clear_group==30 && rules.skill_clear_collider_index==25,
        "rule append provenance/order");
    check(rules.balls_per_game==3 && rules.initial_bonus==10000 &&
        rules.subsequent_bonus==25000 && rules.score_wrap==1000000000,
        "source default balls/bonus/strict-wrap metadata");
    check(rules.drain_delay==1820000000 && rules.feed_delay==960000000 &&
        rules.shoot_again_on==5000000000LL && rules.shoot_again_flash==5000000000LL &&
        rules.reflex_duration==5000000000LL && rules.reflex_award==25000,
        "source drain/feed/save timing metadata");
    check(rules.event_group_awards.size()==5 && rules.event_group_awards.at(16)==2500 &&
        rules.event_group_awards.at(17)==2500 && rules.event_group_awards.at(18)==2500 &&
        rules.event_group_awards.at(23)==20000 && rules.event_group_awards.at(25)==5000,
        "default capture and actual score-line awards");
    check(rules.event_group_awards.count(27)==0 && rules.event_group_awards.count(29)==0 &&
        rules.event_group_awards.count(30)==0,
        "hole landing and skill gate must not invent score");
    const motion::StaticCollider& drain=p.static_colliders[22];
    const motion::StaticCollider& gate=p.static_colliders[23];
    const motion::StaticCollider& score=p.static_colliders[24];
    check(drain.captures_ball && !drain.center_crossing && !drain.center_trigger &&
        !drain.two_sided && drain.collision_mask==1,"drain must be an inflated directed capture");
    check(gate.center_crossing && !gate.applies_transition && !gate.captures_ball &&
        !gate.reference_response && gate.collision_mask==1,"skill gate must be a plain center event");
    close(gate.first.x,-8,"skill gate raw direction X0");
    close(gate.second.x,-6,"skill gate raw direction X1");
    close(gate.first.y,4.76,"skill gate must use source negative .8R trigger offset");
    check(score.center_crossing && !score.applies_transition && score.collision_mask==4,
        "ramp score trigger must retain 1303 mask and no plane mutation");
    close(score.first.x,10,"ramp score raw X0");close(score.first.y,0,"ramp score raw Y0");
    close(score.second.y,1,"ramp score raw Y1");
    motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),1000000000,16,.0001f));
    std::vector<motion::StaticCollider> only(1,gate);
    motion::BallState pass(physics::Vec2(-7,6),physics::Vec2(0,-2),.3f);
    pass.height=1.23f;
    const motion::MotionStep crossed=stepper.step(pass,1000000000,only);
    check(crossed.contacts.size()==1 && crossed.contacts[0].triggered &&
        !crossed.contacts[0].transitioned && !crossed.contacts[0].activated &&
        !crossed.contacts[0].boost_applied && !crossed.captured &&
        pass.collision_mask==1 && !pass.plane.active,"skill gate changed physics/layer or missed");
    close(pass.position.y,4,"plain gate event changed free travel");
    close(pass.velocity.y,-2,"plain gate event bounced");close(pass.height,1.23,"plain event changed fixed Z");
    close(crossed.contacts[0].seconds_from_start,.62,"gate event had extra ball-radius inflation");
    motion::BallState back(physics::Vec2(-7,4),physics::Vec2(0,2),.3f);
    check(stepper.step(back,1000000000,only).contacts.empty(),"skill gate fired from its back");
    motion::BallState beside(physics::Vec2(-8.01f,6),physics::Vec2(0,-2),.3f);
    check(stepper.step(beside,1000000000,only).contacts.empty(),"skill gate acquired inflated endcaps");
    motion::BallState wrong(physics::Vec2(-7,6),physics::Vec2(0,-2),.3f);wrong.collision_mask=4;
    check(stepper.step(wrong,1000000000,only).contacts.empty(),"raised ball fired ground skill gate");
    only[0]=score;
    motion::BallState ramp(physics::Vec2(9,.5f),physics::Vec2(2,0),.3f);ramp.collision_mask=4;ramp.height=1.36f;
    const motion::MotionStep scored=stepper.step(ramp,1000000000,only);
    check(scored.contacts.size()==1 && scored.contacts[0].triggered && !scored.contacts[0].transitioned &&
        ramp.collision_mask==4 && !ramp.plane.active,"ramp score line changed layer or counted as plane transition");
    close(scored.contacts[0].seconds_from_start,.5,"ramp score line must be unoffset");
    close(ramp.position.x,11,"ramp score event changed travel");close(ramp.height,1.36,"ramp score changed Z");
    only[0]=p.static_colliders[25];
    check(only[0].center_crossing && !only[0].applies_transition && only[0].collision_mask==1,
        "skill-clear gate must be a plain center event");
    close(only[0].first.y,2.76,"skill-clear gate source .8R offset");
    motion::BallState clear(physics::Vec2(-7,4),physics::Vec2(0,-2),.3f);
    const motion::MotionStep cleared=stepper.step(clear,1000000000,only);
    check(cleared.contacts.size()==1 && cleared.contacts[0].triggered &&
        !cleared.contacts[0].transitioned && clear.collision_mask==1,
        "skill-clear event missed or mutated layer");
    close(cleared.contacts[0].seconds_from_start,.62,"skill-clear center event inflation");
    close(clear.position.y,2,"skill-clear event changed travel");
    only[0]=drain;
    motion::BallState loss(physics::Vec2(0,19),physics::Vec2(0,2),.3f);
    const motion::MotionStep captured=stepper.step(loss,1000000000,only);
    check(captured.captured && captured.capture_collider_index==0 && captured.contacts.size()==1 &&
        !captured.contacts[0].triggered,"drain did not terminally capture physical ball");
    close(captured.contacts[0].seconds_from_start,.35,"drain must include ball radius",.00001);
    close(loss.position.y,19.7,"drain center at offset boundary",.001);
    close(physics::length(loss.velocity),0,"drain retained XY velocity");
    motion::BallState raised(physics::Vec2(0,19),physics::Vec2(0,2),.3f);raised.collision_mask=4;
    check(!stepper.step(raised,1000000000,only).captured,"raised ball hit ground drain");
    Groups g=fixture();g[28][3]=floats({407,0});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"zero drain delay accepted");
    g=fixture();g[29][2]=floats({600,2,-8,5,-8,5,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"zero skill-gate length accepted");
    g=fixture();g[30][2]=floats({600,2,-8,3,-8,3,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"zero skill-clear length accepted");
    g=fixture();g[25][7]=floats({1303,32,2,10,0,10,1,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"invalid ramp-score bit accepted");
    g=fixture();g[25][7]=floats({1303,2,3,10,0,10,1,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"nonline ramp-score record accepted");
}
void multiplier_bank_profile() {
    using namespace pinball;
    const TablePhysics p(model(fixture()));
    const reference::TargetBankProfile& bank=p.multiplier_bank;
    check(bank.light_group==35 && bank.hit_award==500 && bank.complete_award==1500 &&
        bank.multipliers==std::array<std::uint32_t,5>{{1,2,3,5,10}} &&
        bank.flash_duration==2000000000LL && bank.decay_period==30000000000LL,
        "source multiplier-bank constants");
    check(p.rules.event_group_awards.size()==5,"target bank leaked into generic award map");
    for (std::size_t i=0;i<3;++i) {
        const reference::TargetProfile& target=bank.targets[i];
        check(target.group==31+i && target.collider_indices.size()==4 &&
            target.idle_frame==0 && target.down_frame==-1 && target.rearm_delay==250000000,
            "target-bank owner/image/rearm metadata");
        material_checks(target.material,i==1?.5:.6,i==1?.8:.95,3,5);
        check(p.rules.event_group_awards.count(target.group)==0,"target bank could double-score");
        for (std::size_t e=0;e<4;++e) {
            const std::size_t at=26+4*i+e;
            check(target.collider_indices[e]==at && p.collider_groups[at]==target.group,
                "target-bank append order");
            const motion::StaticCollider& edge=p.static_colliders[at];
            check(edge.kind==motion::ColliderKind::Segment && edge.reference_response &&
                !edge.captures_ball && !edge.center_crossing && !edge.two_sided &&
                edge.response_owner==target.group && edge.deactivate_on_activation &&
                !edge.latch_response && edge.enabled && edge.collision_mask==(i==1?4u:1u),
                "target-bank source front/layer/owner activation flags");
        }
    }
    close(p.static_colliders[26].first.x,10,"target edge reversed X0");
    close(p.static_colliders[26].first.y,-7,"target edge reversed Y0");
    close(p.static_colliders[26].second.y,-8,"target edge reversed Y1");
    for (std::size_t i=0;i<4;++i)
        check(bank.lamps[i].group==36+i && bank.lamps[i].on_frame==0 &&
            bank.lamps[i].off_delay==100000000 && bank.lamps[i].on_delay==100000000,
            "ordered lamp source state/flash periods");

    std::vector<motion::StaticCollider> only;
    for (std::size_t e=0;e<4;++e) only.push_back(p.static_colliders[26+e]);
    motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),100000000,16,.0001f));
    motion::BallState hit(physics::Vec2(10.5f,-7.5f),physics::Vec2(-10,0),.3f);
    const motion::MotionStep hard=stepper.step(hit,100000000,only);
    check(hard.contacts.size()==1 && hard.contacts[0].activated && hard.contacts[0].boost_applied &&
        !hard.contact_budget_exhausted,"target-bank hard contact response");
    close(hit.velocity.x,11,"target default elasticity/kick",.0001);
    for (std::size_t e=0;e<4;++e)check(only[e].enabled,"atomic owner drop mutated profile input");
    motion::BallState weak(physics::Vec2(10.32f,-7.5f),physics::Vec2(-2,0),.3f);
    const motion::MotionStep low=stepper.step(weak,100000000,only);
    check(low.contacts.size()==1 && !low.contacts[0].activated && !low.contacts[0].boost_applied,
        "weak target contact should not drop");
    close(weak.velocity.x,1.2,"weak target passive response",.0001);
    motion::BallState exact(physics::Vec2(10.32f,-7.5f),physics::Vec2(-3,0),.3f);
    const motion::MotionStep equal=stepper.step(exact,100000000,only);
    check(equal.contacts.size()==1 && !equal.contacts[0].activated && equal.contacts[0].boost_applied,
        "target strict drop threshold differs from inclusive kick threshold");

    Groups g=fixture();g[35][2]=shorts(10,{1027,38,36,39,37});
    const TablePhysics reordered(model(g));
    check(reordered.multiplier_bank.lamps[0].group==38 && reordered.multiplier_bank.lamps[3].group==37,
        "bank lamp order was sorted instead of retained");
    g=fixture();g[31][3]=floats({600,5,10,-8,10,-7,9.9f,-7,9.9f,-8,11,-8,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"open target body accepted");
    g=fixture();g[31][3]=floats({600,5,10,-8,10,-8,9.9f,-7,9.9f,-8,10,-8,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"degenerate target body accepted");
    g=fixture();g[31][3]=floats({600,5,10,-8,9.9f,-7,10,-7,9.9f,-8,10,-8,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"self-crossed target body accepted");
    g=fixture();g[31][3]=floats({600,4,10,-8,10,-7,9.9f,-7,9.9f,-8,10,-8,-1});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"wrong target point count accepted");
    g=fixture();g[31][4]=floats({407,0});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"zero target rearm accepted");
    g=fixture();g[32][4]=floats({407,.5f});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"inconsistent bank rearm periods accepted");
    g=fixture();g[31].pop_back();
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"missing raised target image accepted");
    g=fixture();g[35][2]=shorts(10,{1027,36,36,38,39});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"duplicate bank lamp accepted");
    g=fixture();g[35][2]=shorts(10,{1027,36,37,-1,39});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"negative bank lamp accepted");
    g=fixture();g[35][2]=shorts(10,{1027,36,37,38});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"truncated bank lamp list accepted");
    g=fixture();g[36][4]=floats({900,0});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"zero bank flash period accepted");
    g=fixture();g[37][3]=floats({901,.2f});
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"unequal bank flash period accepted");
    g=fixture();g[36].erase(g[36].begin()+2);
    throws<std::exception>([&]{TablePhysics invalid(model(g));},"missing bank lamp image accepted");
}
void lane_profiles() {
    using namespace pinball;
    const TablePhysics p(model(lane_fixture()));
    check(p.static_colliders.size()==97 && p.rollovers.size()==6,"synthetic lane append count");
    for (std::size_t i=0;i<6;++i) {
        const reference::RolloverProfile& r=p.rollovers[i];
        check(r.group==40+i && r.entry_indices.size()==4 && r.exit_indices.size()==4 &&
            r.idle_frame==0 && r.active_frame==-1 && r.rearm_delay==100000000,
            "paired rollover role/frame/rearm");
        for (std::size_t j=0;j<4;++j) {
            check(r.entry_indices[j]==45+8*i+j && r.exit_indices[j]==49+8*i+j,
                "lane event append order");
            const motion::StaticCollider& entry=p.static_colliders[r.entry_indices[j]];
            check(entry.center_crossing && !entry.applies_transition && !entry.reference_response &&
                !entry.captures_ball && entry.collision_mask==(i<3?1u:4u),"lane center event semantics/mask");
        }
    }
    for (std::size_t bank=0;bank<2;++bank) {
        const reference::LaneGroupProfile& lane=p.lane_groups[bank];
        check(lane.light_group==52+bank && lane.bumper_group==61+bank &&
            lane.bumper_groups.size()==(bank?3u:4u) && lane.rollover_award==(bank?500u:2000u) &&
            lane.completion_flash==5000000000LL && lane.decay_period==60000000000LL,
            "lane collection/bumper progression constants");
        for (std::size_t j=0;j<3;++j) {
            check(lane.rollover_groups[j]==42+3*bank-j && lane.lamps[j].group==46+3*bank+j &&
                lane.lamps[j].off_delay==100000000 && lane.lamps[j].on_delay==100000000,
                "lane controller mapping/declared lamp order");
        }
    }
    check(p.lane_groups[0].bumper_scores==std::array<std::uint32_t,4>{{500,1000,1500,2000}} &&
        p.lane_groups[1].bumper_scores==std::array<std::uint32_t,4>{{1500,2500,3500,4500}},
        "source bumper score tiers");
    const reference::RolloverProfile& first=p.rollovers[0];
    std::vector<motion::StaticCollider> edges;
    for (std::size_t i=0;i<4;++i) edges.push_back(p.static_colliders[first.entry_indices[i]]);
    for (std::size_t i=0;i<4;++i) edges.push_back(p.static_colliders[first.exit_indices[i]]);
    // An intentionally huge radius proves that the event follows the center,
    // with neither ball inflation nor physical response or layer mutation.
    motion::BallState ball(physics::Vec2(30.5f,51.2f),physics::Vec2(0,-10),10);
    ball.height=4;
    motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),1000000000,16,.0001f));
    motion::MotionStep moved=stepper.step(ball,200000000,edges);
    check(moved.contacts.size()==2 && moved.contacts[0].triggered && moved.contacts[1].triggered &&
        !moved.contacts[0].transitioned && !moved.contacts[1].transitioned && !moved.contact_budget_exhausted && !moved.moving_work_exhausted,
        "rollover must produce ordered entry/exit events");
    close(ball.position.y,49.2,"rollover should not stop movement");
    close(ball.velocity.y,-10,"rollover should not change velocity");
    close(ball.height,4,"rollover should not change height");
    check(ball.collision_mask==1,"rollover should not change layer");
    Groups g=lane_fixture();g[52][2]=shorts(10,{1027,48,46,47});
    const TablePhysics reordered(model(g));
    check(reordered.lane_groups[0].lamps[0].group==48 &&
        reordered.lane_groups[0].rollover_groups[0]==40,"lamp rotation order/controller identity conflated");
    g=lane_fixture();g[40][4]=floats({603,5,31,51,30,51,30,50,31,50,31,51,-1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"same winding rollover exit accepted");
    g=lane_fixture();g[40][3]=floats({600,5,31,51,30,51,30,51,31,50,31,51,-1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"degenerate rollover region accepted");
    g=lane_fixture();g[40].erase(g[40].begin()+4);
    throws<std::exception>([&]{TablePhysics bad(model(g));},"missing rollover exit accepted");
    g=lane_fixture();g[45][1]=name("missing_lane");
    throws<std::exception>([&]{TablePhysics bad(model(g));},"partial lane feature accepted");
    g=lane_fixture();g[52][2]=shorts(10,{1027,46,46,48});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"duplicate lane lamp accepted");
    g=lane_fixture();g[61][2]=shorts(10,{1027,54,55,56});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"short bumper group accepted");
    g=lane_fixture();g[54].pop_back();
    throws<std::exception>([&]{TablePhysics bad(model(g));},"missing bumper level image accepted");
    g=lane_fixture();g[46][4]=floats({900,0});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero lane flash delay accepted");
}
void medal_profile() {
    const TablePhysics p(model(medal_fixture()));
    const pinball::reference::MedalBankProfile& bank=p.medal_bank;
    check(p.static_colliders.size()==54 && bank.light_group==43 && bank.hit_award==1500 &&
        bank.completion_awards==std::array<std::uint32_t,3>{{10000,50000,0}} &&
        bank.extra_ball_level==3 && bank.flash_duration==2000000000LL &&
        bank.decay_period==30000000000LL,"source medal stages/awards");
    for (std::size_t i=0;i<3;++i) {
        const pinball::reference::TargetProfile& target=bank.targets[i];
        check(target.group==40+i && target.collider_indices.size()==4 &&
            target.collider_indices[0]==38+4*i && target.rearm_delay==250000000 &&
            target.idle_frame==0 && target.down_frame==-1,"medal drop body/frame/rearm");
        material_checks(target.material,.6,.95,3,5);
        check(bank.lamps[i].group==46-i && bank.lamps[i].on_frame==0 &&
            bank.lamps[i].off_delay==100000000 && bank.lamps[i].on_delay==100000000,
            "medal declared light order/cadence");
        for (std::size_t edge=0;edge<4;++edge) {
            const pinball::motion::StaticCollider& body=p.static_colliders[target.collider_indices[edge]];
            check(body.response_owner==target.group && body.deactivate_on_activation && body.reference_response &&
                !body.two_sided && body.collision_mask==1,"medal source owner response");
        }
        check(p.rules.event_group_awards.count(target.group)==0,"medal award would be double counted");
    }
    Groups g=medal_fixture();g[42][1]=name("missing_medal");
    throws<std::exception>([&]{TablePhysics bad(model(g));},"partial medal bank accepted");
    g=medal_fixture();g[41][4]=floats({407,.5f});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"different medal rearm accepted");
    g=medal_fixture();g[43][2]=shorts(10,{1027,46,45,45});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"duplicate medal light accepted");
    g=medal_fixture();g[44][4]=floats({901,.2f});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"different medal flash interval accepted");
}
void advanced_profiles() {
    using namespace pinball;
    const TablePhysics p(model(advanced_fixture()));
    const reference::AdvancedTableProfile& a=p.advanced;
    check(p.static_colliders.size()==70 && a.targets.size()==1 && a.rollovers.size()==2 &&
        a.skill_tripwires.size()==1 && p.rollovers.size()==2,"synthetic additive feature counts");
    for (std::size_t i=38;i<p.static_colliders.size();++i)
        check(!p.static_colliders[i].enabled,"unwired advanced geometry active by default");
    const reference::AdvancedTargetProfile& target=a.targets[0];
    check(target.number==13 && target.index==0 && target.kind==reference::AdvancedTargetKind::Mission &&
        target.score==1000 && target.lamp_group==41 && target.body.group==40 &&
        target.body.idle_frame==0 && target.body.down_frame==1 && target.body.rearm_delay==100000000,
        "mission standalone target metadata");
    material_checks(target.body.material,.6,.95,3,5);
    const reference::RolloverProfile& fuel=p.rollovers[a.rollovers[0].region_index];
    const reference::RolloverProfile& warp=p.rollovers[a.rollovers[1].region_index];
    check(a.rollovers[0].number==179 && a.rollovers[0].kind==reference::AdvancedRolloverKind::Fuel &&
        a.rollovers[0].score==500 && a.rollovers[0].fuel_level==1 && fuel.idle_frame==-1 &&
        fuel.active_frame==-1 && fuel.entry_indices.size()==4,"fuel rollover metadata/images");
    check(a.rollovers[1].number==9 && a.rollovers[1].score==0 && warp.entry_indices.size()==6 &&
        warp.exit_indices.size()==6 && warp.idle_frame==-1 && warp.active_frame==0 &&
        warp.exit_hide_delay==150000000,"light rollover hexagon/delay/no direct award");
    check(a.fuel.present && a.fuel.group==42 && a.fuel.level_durations==std::array<runtime::Tick,12>{{
        3000000000LL,14000000000LL,3000000000LL,12000000000LL,3000000000LL,10000000000LL,
        3000000000LL,8000000000LL,3000000000LL,6000000000LL,3000000000LL,4000000000LL}},"fuel index countdown durations");
    const reference::SkillTripwireProfile& trip=a.skill_tripwires[0];
    check(trip.group==49 && trip.number==1 && trip.lamp_group==50,"skill tripwire owner/lamp");
    const motion::StaticCollider& sensor=p.static_colliders[trip.collider_index];
    close(sensor.first.y,79.7,"source tripwire +R offset");close(sensor.second.y,79.7,"tripwire offset endpoint");
    std::vector<motion::StaticCollider> one(1,sensor);one[0].enabled=true;
    motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),100000000,16,.0001f));
    motion::BallState ball(physics::Vec2(1,79.5f),physics::Vec2(0,10),10);
    const motion::MotionStep moved=stepper.step(ball,40000000,one);
    check(moved.contacts.size()==1 && moved.contacts[0].triggered && !moved.contacts[0].transitioned,
        "tripwire incorrectly inflated/physical");
    close(ball.position.y,79.9,"tripwire interrupted free motion");
    check(a.has_hyperspace && a.hyperspace.group==54 && a.hyperspace.delay==0 &&
        a.hyperspace.cooldown==50000000 && a.hyperspace_light_group==60,"hyperspace bounded metadata");
    close(a.hyperspace.capture_radius,.36,"hyperspace center trigger scale");
    check(a.hyperspace_sound_refs[0].resource_group==68 && a.hyperspace_sound_refs[0].filename=="fixture0.wav" &&
        a.hyperspace_sound_refs[1].resource_group==69 && a.hyperspace_reflex_sound.resource_group==72 &&
        a.hyperspace_combined_sound.resource_group==73 && a.hyperspace_special_sound.resource_group==74,
        "hyperspace sound references should not require opening audio files");
    check(a.deployment_gates.size()==2 && a.deployment_gates[0].number==4 &&
        a.deployment_gates[1].number==10 && a.deployment_gates[1].collider_index==p.rules.skill_clear_collider_index,
        "deployment gates duplicate/misidentify existing main-table exit");
    Groups g=advanced_fixture();g[40].pop_back();
    throws<std::exception>([&]{TablePhysics bad(model(g));},"solo target missing hit image accepted");
    g=advanced_fixture();g[42][3]=floats({904,3,14,3,12,3,10,3,8,3,6,3,0});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero fuel duration accepted");
    g=advanced_fixture();g[49][2]=floats({600,2,0,80,0,80,-1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero-length skill tripwire accepted");
    g=advanced_fixture();g[53][4]=floats({407,0});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero light rollover expiry accepted");
    g=advanced_fixture();g[68][1].type=3;
    throws<std::exception>([&]{TablePhysics bad(model(g));},"missing sound filename accepted");
    g=advanced_fixture();g[61][2]=shorts(10,{1100,68,1100,69});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"duplicate sound reference accepted");
}
void moving_feature_profiles() {
    using namespace pinball;
    const TablePhysics p(model(moving_fixture()));
    check(p.static_colliders.size()==50 && p.advanced.spinners.size()==2 &&
        p.advanced.dynamic_walls.size()==3 && p.advanced.has_gravity_well,"moving feature count");
    for (std::size_t i=38;i<p.static_colliders.size();++i)
        check(!p.static_colliders[i].enabled,"unwired moving feature active");
    for (std::size_t i=0;i<2;++i) {
        const reference::FlagSpinnerProfile& flag=p.advanced.spinners[i];
        check(flag.group==40+i && flag.number==i+1 && flag.forward_index==38+2*i &&
            flag.reverse_index==39+2*i && flag.image_count==8 && flag.idle_frame==0 && flag.default_enabled &&
            flag.score==500 && flag.powered_score==2500,"flag spinner owner/frame/award facts");
        close(flag.minimum_speed,5,"spinner min speed");close(flag.maximum_speed,50000,"spinner max speed");
        close(flag.speed_scale,20,"spinner ball speed multiplier");close(flag.speed_decay,.65,"spinner decay");
    }
    std::vector<motion::StaticCollider> sensor;
    sensor.push_back(p.static_colliders[38]);sensor.push_back(p.static_colliders[39]);
    sensor[0].enabled=sensor[1].enabled=true;
    motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),100000000,8,.0001f));
    motion::BallState front(physics::Vec2(31,30.2f),physics::Vec2(0,-20),10);
    motion::MotionStep event=stepper.step(front,20000000,sensor);
    check(event.contacts.size()==1 && event.contacts[0].collider_index==0 && event.contacts[0].triggered,
        "source forward spinner side/direction");
    close(front.position.y,29.8,"spinner must not stop or inflate ball");close(front.velocity.y,-20,"spinner changed velocity");
    motion::BallState back(physics::Vec2(31,29.8f),physics::Vec2(0,20),10);
    event=stepper.step(back,20000000,sensor);
    check(event.contacts.size()==1 && event.contacts[0].collider_index==1,"source reverse spinner side");
    for (std::size_t i=0;i<3;++i) {
        const reference::DynamicWallProfile& wall=p.advanced.dynamic_walls[i];
        check(wall.group==42+i && wall.collider_indices.size()==1 && wall.collider_indices[0]==42+i &&
            wall.closed_frame==0 && wall.open_frame==-1 && wall.default_enabled==(i<2),
            "dynamic wall identity/closed/open/default state");
        material_checks(wall.material,.5,.8,i<2?8.9999999e10:1e9,0);
    }
    const reference::DynamicWallProfile& blocker=p.advanced.dynamic_walls[2];
    check(blocker.kind==reference::DynamicWallKind::Blocker && blocker.initial_duration==55000000000LL &&
        blocker.extended_duration==5000000000LL && blocker.lamp_groups[0]==49,"source drain-blocker timers/lamp");
    check(p.static_colliders[44].kind==motion::ColliderKind::Disk && p.static_colliders[42].kind==motion::ColliderKind::Segment &&
        !p.static_colliders[42].two_sided,"source dynamic wall shape/front");
    const reference::GravityWellProfile& well=p.advanced.gravity_well;
    check(well.capture.group==50 && well.capture.collider_index==45 && well.lamp_group==54 &&
        !well.default_enabled && !well.reactivate_after_cooldown && well.score==50000 &&
        well.capture.delay==0 && well.capture.cooldown==50000000 && well.release_sound.filename=="gravity.wav",
        "gravity well activation/sound-driven delay policy");
    close(well.capture.field_radius,3.2,"gravity field radius");close(well.capture.capture_radius,.16,"gravity trigger radius");
    close(well.capture.field_strength,30,"gravity pull");close(well.capture.speed,45,"gravity throw scalar");
    close(well.capture.direction.x,0,"gravity raw direction X");close(well.capture.direction.y,-1,"gravity raw direction Y");
    close(well.capture.angle_multiplier,1.2566371,"gravity throw angle radians");close(well.capture.held_z,.3,"gravity held Z");
    Groups g=moving_fixture();g[40][3]=floats({1201,0});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero spinner min speed accepted");
    g=moving_fixture();g[40][5]=floats({1202,1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"nondecaying spinner accepted");
    g=moving_fixture();g[40][2]=floats({600,2,30,30,30,30,-1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero spinner line accepted");
    g=moving_fixture();g[42][3]=floats({600,2,40,0,40,0,-1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero dynamic gate line accepted");
    g=moving_fixture();g[44][3]=floats({600,1,44,.5f,0,-1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero blocker radius accepted");
    g=moving_fixture();g[50][3]=floats({600,1,50,5});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"short gravity well shape accepted");
}
void remaining_wall_profiles() {
    using namespace pinball;
    const TablePhysics base(model(fixture()));
    check(base.advanced.one_ways.size()==3 && base.advanced.has_escape_chute,
        "all one-way objects and escape route need metadata");
    const reference::OneWayProfile* oneway=0;
    for (std::size_t i=0;i<base.advanced.one_ways.size();++i)
        if (base.advanced.one_ways[i].group==29) oneway=&base.advanced.one_ways[i];
    check(oneway && oneway->number==1 && oneway->pass_event_index==base.rules.skill_gate_collider_index &&
        base.static_colliders[oneway->solid_index].directional_approach_only,
        "one-way solid/pass ownership");
    const motion::StaticCollider& solid=base.static_colliders[oneway->solid_index];
    close(solid.first.y,5,"one-way solid must store raw endpoints, not source's pre-offset ray");
    close(solid.second.y,5,"one-way raw second endpoint");
    std::vector<motion::StaticCollider> pair;pair.push_back(solid);
    pair.push_back(base.static_colliders[oneway->pass_event_index]);pair[0].enabled=pair[1].enabled=true;
    motion::BallStepper stepper(motion::MotionConfig(physics::Vec2(),1000000000,16,.0001f));
    motion::BallState from_front(physics::Vec2(-7,4.5f),physics::Vec2(0,1),.3f);
    motion::MotionStep moved=stepper.step(from_front,1000000000,pair);
    check(!moved.contacts.empty() && !moved.contacts[0].triggered && moved.contacts[0].collider_index==0,
        "one-way source solid front missed");
    close(moved.contacts[0].hit.point.y+.3f*moved.contacts[0].hit.normal.y,4.7,"ball radius applied more than once at one-way wall");
    close(moved.contacts[0].seconds_from_start,.2,"source +R solid contact plane");
    // The allowed direction is nonphysical and crosses the separate .8R plane.
    motion::BallState from_back(physics::Vec2(-7,5.5f),physics::Vec2(0,-1),.3f);
    moved=stepper.step(from_back,1000000000,pair);
    check(moved.contacts.size()==1 && moved.contacts[0].triggered && moved.contacts[0].collider_index==1,
        "one-way back should pass and emit only the pass event");
    close(moved.contacts[0].hit.point.y,4.76,"source .8R pass event plane");
    close(from_back.position.y,4.5,"one-way pass was blocked or displaced");
    const TablePhysics p(model(remaining_wall_fixture()));
    check(p.static_colliders.size()==52 && p.advanced.kickbacks.size()==2,"kickback append count");
    for (std::size_t i=0;i<2;++i) {
        const reference::KickbackProfile& kick=p.advanced.kickbacks[i];
        check(kick.group==55+i && kick.number==i+1 && kick.collider_index==46+i && kick.gate_group==42+i &&
            kick.armed_threshold==0 && kick.arm_delay==700000000 && kick.pulse_period==100000000 &&
            kick.idle_frame==0 && kick.fired_frame==1 && kick.reset_frame==-1 && kick.default_enabled,
            "kickback state/material/timing metadata");
        material_checks(kick.material,.2,.7,1e9,55);
        const motion::StaticCollider& body=p.static_colliders[kick.collider_index];
        check(!body.enabled && body.latch_response && !body.two_sided && body.directional_approach_only,"kickback local powered-response latch");
        close(body.first.y,30.0+2*i,"kickback raw line should not pre-inflate radius");
    }
    const reference::KickbackProfile& first=p.advanced.kickbacks[0];
    std::vector<motion::StaticCollider> one(1,p.static_colliders[first.collider_index]);one[0].enabled=true;
    motion::BallStepper short_step(motion::MotionConfig(physics::Vec2(),100000000,16,.0001f));
    motion::BallState ball(physics::Vec2(61,29.5f),physics::Vec2(0,10),.3f);
    moved=short_step.step(ball,100000000,one);
    check(moved.contacts.size()==1 && !moved.contacts[0].activated && !moved.contacts[0].boost_applied,
        "kickback should initially be passive");
    close(moved.contacts[0].hit.point.y+.3f*moved.contacts[0].hit.normal.y,29.7,"kickback ball radius applied twice");
    close(ball.velocity.y,-2,"kickback passive source elasticity");
    one[0].material.threshold=first.armed_threshold;
    ball=motion::BallState(physics::Vec2(61,29.5f),physics::Vec2(0,10),.3f);
    moved=short_step.step(ball,100000000,one);
    check(moved.contacts.size()==1 && moved.contacts[0].activated && moved.contacts[0].boost_applied,
        "armed kickback did not kick once");
    close(ball.velocity.y,-57,"kickback source additive boost");
    const reference::SinkProfile& escape=p.advanced.escape_chute;
    check(escape.group==22 && escape.delay==2000000000 && p.rules.event_group_awards.count(escape.group)==0 &&
        p.sinks.size()==3,"escape chute must not join scored/routed wormholes");
    check(p.static_colliders[escape.collider_index].captures_ball && !p.static_colliders[escape.collider_index].two_sided &&
        !p.static_colliders[escape.collider_index].enabled,"escape source capture/front/default-disabled role");
    Groups g=remaining_wall_fixture();g[55].pop_back();
    throws<std::exception>([&]{TablePhysics bad(model(g));},"kickback missing fired image accepted");
    g=remaining_wall_fixture();g[55][3]=floats({600,2,60,30,60,30,-1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero kickback line accepted");
    g=fixture();g[15][3]=floats({600,2,3,0,3,0,-1});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero unbound one-way solid accepted");
    g=fixture();g[22][5]=floats({407,0});
    throws<std::exception>([&]{TablePhysics bad(model(g));},"zero escape return timer accepted");
}
void malformed() {
    Groups g=fixture();g[7][2]=shorts(10,{100,1,602,-1,300,10});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"negative collision bit accepted");
    g=fixture();g[7][2]=shorts(10,{100,1,602,32,300,10});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"collision bit32 accepted");
    g=fixture();g[7][2]=shorts(10,{100,1,602});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"truncated collision bit pair accepted");
    g=fixture();g[23][4]=floats({306,-.2f});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"negative kickout capture factor accepted");
    g=fixture();g[23][3]=floats({600,1,4,-.25f,0,-1});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"zero kickout field radius accepted");
    g=fixture();g[23][6]=floats({408,0,0});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"truncated kickout held-Z accepted");
    g=fixture();g[7][2]=shorts(10,{100,1,300,-1});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"negative material reference accepted");
    g=fixture();g[7][2]=shorts(10,{100,1,300,10,300,10});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"duplicate material reference accepted");
    g=fixture();g[9][1]=floats({301,.5f,301,.6f,302,.75f});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"duplicate material attribute accepted");
    g=fixture();g[11][0]=shorts(0,{300});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"wrong kicker marker accepted");
    g=fixture();g[11][1]=floats({401,1,404,0,1});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"truncated kicker vector accepted");
    g=fixture();g[10][1]=floats({301,(std::numeric_limits<float>::quiet_NaN)(),302,.5f});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"nonfinite material accepted");
    g=fixture();g[7][3]=floats({600,3,0,0,1,1,-1});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"malformed selected geometry accepted");
    g=fixture();g[4][4]=floats({601,0,10,.13f,-1});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"feed outside striker accepted");
    g=fixture();g[19][1]=floats({402,35,403,5,404,0,0,0,405,0});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"zero sink direction accepted");
    g=fixture();g[19][1]=floats({402,35,403,5,404,1,1,1,405,0});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"nonplanar sink direction accepted");
    g=fixture();g[16][5]=floats({407,0});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"zero sink delay accepted");
    g=fixture();g[5][5]=floats({802,1,9,.2f,-1});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"different tip radii silently discarded");
    g=fixture();g[5][7]=floats({804,0});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"zero flipper duration accepted");
    g=fixture();g[25][3]=floats({1300,2,0});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"malformed ramp stride accepted");
    g=fixture();g[25][5]=floats({1301,32,1,2,2,2,0,2,-1});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"ramp external bit32 accepted");
    g=fixture();g[25][8]=floats({1305,2});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"invalid ramp reset flag accepted");
    g=fixture();g[27][7]=floats({1304,1.5f});
    throws<std::exception>([&] { TablePhysics p(model(g)); },"fractional hole landing mask accepted");
    const float biggest=(std::numeric_limits<float>::max)();
    g=fixture();g[3][1]=floats({500,biggest});
    throws<std::overflow_error>([&] { TablePhysics p(model(g)); },"ball speed cap overflow accepted");
    g=fixture();g[5][7]=floats({804,biggest});
    throws<std::overflow_error>([&] { TablePhysics p(model(g)); },"duration tick overflow accepted");
    g=fixture();g[5][3]=floats({800,biggest,10,.31f,-1});g[5][4]=floats({801,-biggest,11,.19f,-1});
    throws<std::overflow_error>([&] { TablePhysics p(model(g)); },"relative vector overflow accepted");
}
void real(const char* path) {
    const TableModel data=TableModel::load(path);
    const TablePhysics p(data);
    check(p.table_group==107 && p.ball_group==108 && p.plunger_group==473,"real resource IDs");
    check(p.flipper_groups[0]==355 && p.flipper_groups[1]==363,"real flipper IDs");
    check(p.static_colliders.size()==813 && p.plunger_collider_index==470 && p.plunger_image_count==8,"real813 count");
    close(p.feed_position.x,-7.0209388733,"real601 X");close(p.feed_position.y,10.084854126,"real601 Y");
    close(p.seat_position.x,p.feed_position.x,"real seat must use feed X");
    close(p.seat_position.y,11.974978447-p.ball_radius,"real600 settling Y");
    close(p.ball_radius,.3000000119,"real500 radius");close(p.field.gravity.y,11.9856385,"real305 gravity");
    close(p.field.drag_per_second,.2,"real701 damping");close(p.field.lateral_noise_per_speed,.1,"real noise");
    close(p.field.maximum_speed,60.0000038,"real ball cap");
    unsigned ordinary=0,slick=0,circle=0,powered=0;
    for (std::size_t i=0;i<p.plunger_collider_index;++i) {
        const Material& m=p.static_colliders[i].material;
        check(p.static_colliders[i].reference_response && p.static_colliders[i].collision_mask==pinball::reference::collision_mask_for_group(data,p.collider_groups[i]),"real response or layer mask");
        if (!(p.static_colliders[i].collision_mask&1u)) continue;
        if (p.collider_groups[i]>=469 && p.collider_groups[i]<=472) {
            material_checks(m,.6,.95,1,18);++powered;
        } else if (std::fabs(m.elasticity-.5f)<.00001 && std::fabs(m.smoothness-.8f)<.00001) ++ordinary;
        else if (std::fabs(m.elasticity-.5f)<.00001 && std::fabs(m.smoothness-.9f)<.00001) ++slick;
        else if (std::fabs(m.elasticity-.6f)<.00001 && std::fabs(m.smoothness-.6f)<.00001) {
            check(p.static_colliders[i].kind==pinball::motion::ColliderKind::Disk,"real circular material shape");++circle;
        } else throw std::runtime_error("unexpected real material mapping");
    }
    check(ordinary==317 && slick==30 && circle==16 && powered==19,"real material histogram");
    material_checks(p.static_colliders[p.plunger_collider_index].material,.5,.5,1e9,0);
    std::vector<std::size_t> excluded=p.ground_excluded_groups;
    std::sort(excluded.begin(),excluded.end());
    const std::size_t expected_excluded[]={103,106,306,307,308,309,326,327,328};
    check(excluded==std::vector<std::size_t>(expected_excluded,expected_excluded+9) &&
        p.ground_excluded_collider_count==88,"real excluded-layer provenance");
    check(pinball::reference::collision_mask_for_group(data,103)==6 &&
        pinball::reference::collision_mask_for_group(data,308)==4 &&
        pinball::reference::collision_mask_for_group(data,106)==2,"real602 bit-position interpretation");
    const std::size_t restored[]={313,314,316,325,472};
    for (std::size_t i=0;i<5;++i)
        check(std::find(p.collider_groups.begin(),p.collider_groups.end(),restored[i])!=p.collider_groups.end(),
            "valid ground geometry still excluded by its trailer");
    check(p.kickouts.size()==1 && p.kickouts[0].group==514 && p.kickouts[0].collider_index==474,
        "real default black-hole profile");
    const pinball::reference::KickoutProfile& black=p.kickouts[0];
    close(black.center.x,3.932240963,"real black-hole center X");close(black.center.y,-.270534009,"real center Y");
    close(black.field_radius,.4125260115,"real field600 radius");
    close(black.capture_radius,.0825052035,"real306 center capture radius");close(black.field_strength,32,"real305 pull");
    close(black.direction.x,-1,"real404 X");close(black.direction.y,-1,"real404 Y");
    close(black.angle_multiplier,0,"real405 angle");close(black.speed,35,"real402 speed");
    close(black.speed_variation,.05,"real403 variation");close(black.held_z,0,"real408 held Z");
    check(black.delay==1500000000 && black.cooldown==50000000,"real black-hole timing");
    check(p.static_colliders[black.collider_index].center_trigger &&
        p.static_colliders[black.collider_index].captures_ball,"real black-hole center sensor flags");
    // Reproduce the old false-layer pocket, then steer toward the black hole.
    // A ground ball must reach its center sensor instead of hitting wall103.
    const pinball::physics::Vec2 old_pocket(3.6124716f,-.2235600f);
    const double dx=double(black.center.x)-old_pocket.x,dy=double(black.center.y)-old_pocket.y;
    const double distance=std::sqrt(dx*dx+dy*dy);
    pinball::motion::BallState probe(old_pocket,pinball::physics::Vec2(static_cast<float>(dx/distance),
        static_cast<float>(dy/distance)),p.ball_radius);
    pinball::motion::BallStepper stepper(pinball::motion::MotionConfig(pinball::physics::Vec2(),1000000000,16,.0001f));
    const pinball::motion::MotionStep capture=stepper.step(probe,1000000000,p.static_colliders);
    check(capture.captured && capture.capture_collider_index==black.collider_index && !capture.contact_budget_exhausted,
        "old ramp-layer wall still blocks the black-hole mouth");
    check(p.sinks.size()==3,"real default wormhole count");
    const std::size_t sink_groups[]={338,340,342};
    const double sink_x[]={-2.6182670593,3.1718900204,-4.7144298553};
    const double sink_y[]={-8.8287105560,-9.7572031021,3.3290209770};
    for (std::size_t i=0;i<3;++i) {
        const pinball::reference::SinkProfile& sink=p.sinks[i];
        check(sink.group==sink_groups[i] && sink.collider_index==471+i,"real wormhole indices");
        check(sink.delay==2000000000,"real407 delay");
        close(sink.position.x,sink_x[i],"real sink601 X");close(sink.position.y,sink_y[i],"real sink601 Y");
        close(sink.direction.x,1,"real raw404 X");close(sink.direction.y,i==1?-1:1,"real raw404 Y");
        close(sink.speed,35,"real sink402 speed");close(sink.speed_variation,.05,"real sink403 spread");
        close(sink.angle_multiplier,0,"real sink405 angle");
        const pinball::motion::StaticCollider& c=p.static_colliders[sink.collider_index];
        check(c.captures_ball && !c.two_sided,"real sink capture/front flag");
        const double mx=(double(c.first.x)+c.second.x)*.5,my=(double(c.first.y)+c.second.y)*.5;
        const double line_dx=double(c.second.x)-c.first.x,line_dy=double(c.second.y)-c.first.y,L=std::sqrt(line_dx*line_dx+line_dy*line_dy);
        const double nx=-line_dy/L,ny=line_dx/L;
        const pinball::physics::Vec2 front(static_cast<float>(mx+nx),static_cast<float>(my+ny));
        const pinball::physics::Vec2 back(static_cast<float>(mx-nx),static_cast<float>(my-ny));
        check(pinball::physics::sweep_circle_segment(front,pinball::physics::Vec2(static_cast<float>(-nx),static_cast<float>(-ny)),
            p.ball_radius,c.first,c.second,2,false).hit,"real sink front missed");
        check(!pinball::physics::sweep_circle_segment(back,pinball::physics::Vec2(static_cast<float>(nx),static_cast<float>(ny)),
            p.ball_radius,c.first,c.second,2,false).hit,"real sink back collided");
    }
    for (unsigned i=0;i<2;++i) {
        const pinball::flipper::FlipperConfig& f=p.flippers[i];
        check(f.raise_duration==40000000 && f.return_duration==80000000,"real804/805 durations");
        check(f.response_mode==pinball::flipper::ResponseMode::ReferenceMaterial,"real reference flipper mode");
        close(f.restitution,.75,"real material354 E");close(f.smoothness,.5,"real material354 S");
        close(f.impulse_gain,.9,"real803 gain");close(f.pivot_radius,.3109810054,"real800 radius");
        close(f.tip_radius,.1933079958,"real801 radius");
        close(f.rest_tip_vector.y,1.0678987503,"real801 relative Y");
        close(f.raised_tip_vector.y,-1.0564899445,"real802 relative Y");
    }
    check(p.ramp_planes.size()==20 && p.ramp_transition_indices.size()==63,"real ramp profile counts");
    for (std::size_t i=0;i<20;++i) {
        check(p.ramp_planes[i].id==i && p.ramp_planes[i].surface.id==i,"real ramp plane IDs");
        check(p.ramp_planes[i].group==(i<18?101:105),"real ramp group order");
    }
    const pinball::reference::RampPlaneProfile& entrance=p.ramp_planes[17];
    close(entrance.vertices[0].x,3.2963819504,"selected101 mouth X0");
    close(entrance.vertices[0].y,1.2324700356,"selected101 mouth Y0");
    close(entrance.vertices[1].x,1.5560959578,"selected101 mouth X1");
    close(entrance.surface.gradient.x,.3181520104,"entry height coefficient X");
    close(entrance.surface.gradient.y,-.8643119931,"entry height coefficient Y");
    close(entrance.surface.field_force.x,-5.85049461,"entry field X");close(entrance.surface.field_force.y,15.89386394,"entry field Y");
    close(entrance.surface.drag,.2,"ramp drag");
    unsigned main_events=0,secondary_events=0;
    for (std::size_t i=0;i<p.ramp_transition_indices.size();++i) {
        const std::size_t index=p.ramp_transition_indices[i];
        check(p.static_colliders[index].center_crossing && !p.static_colliders[index].captures_ball,"real ramp crossing role");
        if (p.collider_groups[index]==101) ++main_events;else if (p.collider_groups[index]==105) ++secondary_events;
        else throw std::runtime_error("unexpected ramp event resource");
    }
    check(main_events==56 && secondary_events==7,"real disabled-entry/omitted-score counts");
    const double mx=(double(entrance.vertices[0].x)+entrance.vertices[1].x)*.5;
    const double my=(double(entrance.vertices[0].y)+entrance.vertices[1].y)*.5;
    const double nx=-.3454385243,ny=.9384413812;
    pinball::motion::BallState mouth(pinball::physics::Vec2(static_cast<float>(mx+.8*nx),static_cast<float>(my+.8*ny)),
        pinball::physics::Vec2(static_cast<float>(-20*nx),static_cast<float>(-20*ny)),p.ball_radius);
    const pinball::motion::MotionStep entered=stepper.step(mouth,100000000,p.static_colliders);
    check(mouth.collision_mask==2 && mouth.plane.active && !entered.contact_budget_exhausted,
        "missing mask1-to2 ramp entry");
    for (std::size_t i=0;i<entered.contacts.size();++i)
        if (!entered.contacts[i].moving)
            check(p.collider_groups[entered.contacts[i].collider_index]!=102,"ground wall102 blocked after ramp entry");
    check(p.holes.size()==1 && p.holes[0].group==104 && p.holes[0].collider_index==538,"real ramp-hole provenance");
    const pinball::reference::HoleProfile& hole=p.holes[0];
    check(hole.collision_mask==4 && hole.landing_mask==1 && hole.delay==500000000,"real hole masks/delay");
    close(hole.center.x,5.8701739311,"real hole X");close(hole.center.y,7.6076359749,"real hole Y");
    close(hole.field_radius,.3257719874,"real hole field R");close(hole.capture_radius,.0325771992,"real hole point trigger");
    close(hole.landing_z,.3257719874,"real literal408 landing Z");close(hole.fall_acceleration,5,"real hole fall acceleration");
    check(p.components.size()==11,"real bumper/rebound profile count");
    const std::size_t bumper_groups[]={372,380,388,396,405,413,421};
    const double bumper_x[]={.008,-1.37751997,1.25105703,5.76945114,5.28000116,7.13999987,6.45129395};
    const double bumper_y[]={-3.72000003,-6.61803102,-5.96047783,-10.9551191,4,3.5,4.9078989};
    for (std::size_t i=0;i<7;++i) {
        const pinball::reference::ComponentProfile& b=p.components[i];
        check(b.kind==pinball::reference::HitComponentKind::Bumper && b.group==bumper_groups[i] &&
            b.collider_indices==std::vector<std::size_t>(1,539+i) && b.idle_frame==0 &&
            b.fired_frame==1 && b.duration==100000000,"real bumper default/timer/index");
        check(data.image_states(b.group).size()==8,"real four paired bumper levels");
        material_checks(b.material,.6,.95,2.5,i<4?17:12);
        const pinball::motion::StaticCollider& c=p.static_colliders[539+i];
        check(c.kind==pinball::motion::ColliderKind::Disk && c.reference_response &&
            c.latch_response && !c.center_trigger && !c.captures_ball &&
            c.collision_mask==(i<4?1u:4u) && p.collider_groups[539+i]==b.group,
            "real bumper body/reference/layer flags");
        close(c.first.x,bumper_x[i],"real bumper center X");close(c.first.y,bumper_y[i],"real bumper center Y");
        close(c.radius,i<4?.350001:.250001,"real bumper unexpanded body radius");
        material_checks(c.material,.6,.95,2.5,i<4?17:12);
    }
    const std::size_t rebound_counts[]={4,4,6,5};
    for (std::size_t i=0;i<4;++i) {
        const pinball::reference::ComponentProfile& r=p.components[7+i];
        check(r.kind==pinball::reference::HitComponentKind::Rebound && r.group==469+i &&
            r.collider_indices.size()==rebound_counts[i] && r.idle_frame==-1 &&
            r.fired_frame==0 && r.duration==100000000,"real rebound hidden/image/timer/geometry count");
        material_checks(r.material,.6,.95,1,18);
        for (std::size_t j=0;j<r.collider_indices.size();++j) {
            const std::size_t at=r.collider_indices[j];
            check(at<470 && p.collider_groups[at]==r.group && !p.static_colliders[at].latch_response &&
                p.static_colliders[at].collision_mask==1,"real rebound geometry duplicated or latch/layer changed");
        }
    }
    const pinball::reference::RuleProfile& rules=p.rules;
    check(rules.drain_group==499 && rules.drain_collider_index==546 &&
        rules.skill_gate_group==345 && rules.skill_gate_collider_index==547 &&
        rules.ramp_score_group==101 && rules.ramp_score_collider_index==548 &&
        rules.skill_clear_group==350 && rules.skill_clear_collider_index==549,"real rule append indices");
    check(rules.event_group_awards.size()==16 && rules.balls_per_game==3 &&
        rules.initial_bonus==10000 && rules.subsequent_bonus==25000 &&
        rules.score_wrap==1000000000 && rules.drain_delay==1820000000 &&
        rules.feed_delay==960000000 && rules.shoot_again_on==5000000000LL &&
        rules.shoot_again_flash==5000000000LL && rules.reflex_duration==5000000000LL &&
        rules.reflex_award==25000,"real rule constants/timings");
    for (std::size_t i=0;i<7;++i)
        check(rules.event_group_awards.at(bumper_groups[i])==(i<4?500u:1500u),"real default-level bumper points");
    for (std::size_t i=0;i<4;++i) check(rules.event_group_awards.at(469+i)==500,"real rebound points");
    for (std::size_t i=0;i<3;++i) check(rules.event_group_awards.at(sink_groups[i])==2500,"real unlit wormhole points");
    check(rules.event_group_awards.at(514)==20000 && rules.event_group_awards.at(101)==5000 &&
        rules.event_group_awards.count(104)==0 && rules.event_group_awards.count(345)==0 &&
        rules.event_group_awards.count(350)==0,
        "real capture/ramp awards and nonaward owners");
    const pinball::motion::StaticCollider& drain=p.static_colliders[546];
    check(drain.captures_ball && !drain.center_crossing && drain.collision_mask==1 &&
        !drain.two_sided && p.collider_groups[546]==499,"real drain capture/front");
    close(drain.first.x,10.3214102,"real drain reversed X0");close(drain.second.x,-10.3919888,"real drain reversed X1");
    close(drain.first.y,14.4372463,"real drain raw source Y");
    const pinball::motion::StaticCollider& skill=p.static_colliders[547];
    check(skill.center_crossing && !skill.applies_transition && skill.collision_mask==1 &&
        p.collider_groups[547]==345,"real skill trigger type/layer");
    close(skill.first.x,-7.42655769,"real skill shifted X0",.00002);
    close(skill.first.y,4.01870946,"real skill shifted Y0",.00002);
    const pinball::motion::StaticCollider& ramp_score=p.static_colliders[548];
    check(ramp_score.center_crossing && !ramp_score.applies_transition && ramp_score.collision_mask==4 &&
        p.collider_groups[548]==101,"real ramp score trigger role");
    close(ramp_score.first.x,6.68472815,"real1303 raw X0");
    close(ramp_score.first.y,-1.75184202,"real1303 raw Y0");
    close(ramp_score.second.x,6.1174202,"real1303 raw X1");
    close(ramp_score.second.y,-.918833017,"real1303 raw Y1");
    const pinball::motion::StaticCollider& clear=p.static_colliders[549];
    check(clear.center_crossing && !clear.applies_transition && clear.collision_mask==1 &&
        p.collider_groups[549]==350,"real skill-clear event role/layer");
    close(clear.first.x,-3.11587400,"real clear shifted X0");
    close(clear.first.y,-12.07423112,"real clear shifted Y0");
    close(clear.second.x,-2.41648716,"real clear shifted X1");
    close(clear.second.y,-10.83246324,"real clear shifted Y1");
    const pinball::reference::TargetBankProfile& bank=p.multiplier_bank;
    check(bank.light_group==274 && bank.hit_award==500 && bank.complete_award==1500 &&
        bank.multipliers==std::array<std::uint32_t,5>{{1,2,3,5,10}} &&
        bank.flash_duration==2000000000LL && bank.decay_period==30000000000LL,
        "real bank scoring/tier/timing constants");
    for (std::size_t i=0;i<3;++i) {
        const pinball::reference::TargetProfile& target=bank.targets[i];
        check(target.group==438+i && target.collider_indices.size()==4 &&
            target.idle_frame==0 && target.down_frame==-1 && target.rearm_delay==250000000,
            "real target-bank group/image/rearm metadata");
        material_checks(target.material,.6,.95,3,5);
        check(data.image_states(target.group).size()==1 &&
            rules.event_group_awards.count(target.group)==0,"real bank image or duplicate score path");
        for (std::size_t e=0;e<4;++e) {
            const std::size_t at=550+4*i+e;
            const pinball::motion::StaticCollider& edge=p.static_colliders[at];
            check(target.collider_indices[e]==at && p.collider_groups[at]==target.group &&
                edge.kind==pinball::motion::ColliderKind::Segment && !edge.two_sided &&
                edge.reference_response && edge.enabled && !edge.captures_ball &&
                edge.collision_mask==1 && edge.response_owner==target.group &&
                edge.deactivate_on_activation && !edge.latch_response,
                "real target-bank geometry/owner flags");
            material_checks(edge.material,.6,.95,3,5);
        }
    }
    const std::size_t lamps[]={172,171,170,169};
    for (std::size_t i=0;i<4;++i)
        check(bank.lamps[i].group==lamps[i] && bank.lamps[i].on_frame==0 &&
            bank.lamps[i].off_delay==100000000 && bank.lamps[i].on_delay==100000000 &&
            data.image_states(lamps[i]).size()==1,"real ordered lamp image/cadence");
    close(p.static_colliders[550].first.x,3.32464099,"real target first reversed X0");
    close(p.static_colliders[550].first.y,-7.85313416,"real target first reversed Y0");
    close(p.static_colliders[550].second.x,3.36917496,"real target first reversed X1");
    check(p.rollovers.size()==18,"real eighteen rollover owners");
    const std::size_t lane_owners[]={520,521,522,528,529,530};
    for (std::size_t i=0;i<6;++i) {
        const pinball::reference::RolloverProfile& r=p.rollovers[i];
        check(r.group==lane_owners[i] && r.entry_indices.front()==562+8*i &&
            r.exit_indices.front()==566+8*i && r.rearm_delay==100000000,"real lane event provenance");
        check(p.static_colliders[r.entry_indices[0]].collision_mask==(i<3?1u:4u),"real lane layer mask");
    }
    check(p.lane_groups[0].light_group==276 && p.lane_groups[1].light_group==277 &&
        p.lane_groups[0].bumper_group==429 && p.lane_groups[1].bumper_group==430 &&
        p.lane_groups[0].rollover_groups==std::array<std::size_t,3>{{522,521,520}} &&
        p.lane_groups[1].rollover_groups==std::array<std::size_t,3>{{530,529,528}},"real lane controller identities");
    check(p.lane_groups[0].bumper_groups==std::vector<std::size_t>({372,380,388,396}) &&
        p.lane_groups[1].bumper_groups==std::vector<std::size_t>({405,413,421}),"real bumper group members");
    const std::size_t lane_lamps[2][3]={{128,129,130},{216,215,214}};
    for (std::size_t bank_index=0;bank_index<2;++bank_index)
        for (std::size_t light=0;light<3;++light)
            check(p.lane_groups[bank_index].lamps[light].group==lane_lamps[bank_index][light] &&
                p.lane_groups[bank_index].lamps[light].on_frame==0,"real lane lamps");
    check(p.medal_bank.light_group==275 && p.medal_bank.hit_award==1500 &&
        p.medal_bank.completion_awards==std::array<std::uint32_t,3>{{10000,50000,0}} &&
        p.medal_bank.extra_ball_level==3,"real medal policy");
    for (std::size_t i=0;i<3;++i) {
        const pinball::reference::TargetProfile& target=p.medal_bank.targets[i];
        check(target.group==435+i && target.collider_indices.front()==610+4*i &&
            target.collider_indices.back()==613+4*i && target.rearm_delay==250000000,"real medal target indices");
        material_checks(target.material,.6,.95,3,5);
        check(p.medal_bank.lamps[i].group==133-i && p.medal_bank.lamps[i].on_frame==0,
            "real medal lamp order");
    }
    check(p.advanced.targets.size()==16 && p.advanced.rollovers.size()==12 && p.advanced.skill_tripwires.size()==5,
        "real advanced target/rollover/tripwire counts");
    for (std::size_t i=622;i<p.static_colliders.size();++i)
        check(!p.static_colliders[i].enabled,"real unwired advanced primitive active");
    for (std::size_t i=0;i<16;++i) {
        const pinball::reference::AdvancedTargetProfile& target=p.advanced.targets[i];
        const std::size_t number=i<3?i+1:i+7;
        check(target.number==number && target.body.collider_indices.front()==622+4*i &&
            target.body.collider_indices.back()==625+4*i,"real advanced target source identity/order");
        material_checks(target.body.material,.6,.95,3,5);
        check(target.body.down_frame==(i<3?-1:1) && target.body.rearm_delay==(i<3?250000000:100000000),
            "real popup versus solo state/timer");
    }
    check(p.advanced.targets[6].number==13 && p.advanced.targets[6].body.group==448 &&
        p.advanced.targets[6].lamp_group==182 && p.advanced.targets[6].score==1000,"real mission target source facts");
    check(p.advanced.fuel.present && p.advanced.fuel.group==262 &&
        p.advanced.fuel.level_durations[1]==14000000000LL && p.advanced.fuel.level_durations[11]==4000000000LL,
        "real fuel profile/countdown");
    check(p.advanced.has_hyperspace && p.advanced.hyperspace.group==512 && p.advanced.hyperspace.collider_index==791 &&
        p.advanced.hyperspace.delay==0 && p.advanced.hyperspace_sound_refs[0].filename=="sound35.wav" &&
        p.advanced.hyperspace_reflex_sound.filename=="sound21.wav","real hyperspace source facts");
    close(p.advanced.hyperspace.capture_radius,.371273399,"real hyperspace trigger");
    check(p.advanced.deployment_gates.size()==2 && p.advanced.deployment_gates[0].group==346 &&
        p.advanced.deployment_gates[0].collider_index==792 && p.advanced.deployment_gates[1].group==350 &&
        p.advanced.deployment_gates[1].collider_index==549,"real deployment gate identities");
    check(p.advanced.spinners.size()==2 && p.advanced.dynamic_walls.size()==3 && p.advanced.has_gravity_well,
        "real mission-critical profile count");
    for (std::size_t i=0;i<2;++i) {
        const pinball::reference::FlagSpinnerProfile& spinner=p.advanced.spinners[i];
        check(spinner.group==481+8*i && spinner.forward_index==793+2*i && spinner.reverse_index==794+2*i &&
            spinner.image_count==8 && spinner.number==i+1,"real flag spinner profile");
        close(spinner.speed_decay,.649999976,"real flag speed decay");
    }
    check(p.advanced.dynamic_walls[0].group==501 && p.advanced.dynamic_walls[1].group==502 &&
        p.advanced.dynamic_walls[2].group==500 && p.advanced.dynamic_walls[0].collider_indices[0]==797 &&
        p.advanced.dynamic_walls[1].collider_indices[0]==798 && p.advanced.dynamic_walls[2].collider_indices[0]==799,
        "real gates/blocker append identities");
    const pinball::reference::GravityWellProfile& well=p.advanced.gravity_well;
    check(well.capture.group==510 && well.capture.collider_index==800 && well.lamp_group==168 &&
        well.release_sound.group==66 && well.release_sound.resource_group==23 &&
        well.release_sound.filename=="sound7.wav" && !well.reactivate_after_cooldown,"real gravity well ownership/timing reference");
    close(well.capture.capture_radius,.160000004,"real gravity well center trigger");
    close(well.capture.field_radius,3.20000005,"real gravity well field radius");
    close(well.capture.held_z,.300000012,"real gravity well held height");
    check(p.advanced.kickbacks.size()==2 && p.advanced.one_ways.size()==9 && p.advanced.has_escape_chute,
        "real remaining physical roles");
    for (std::size_t i=0;i<2;++i) {
        const pinball::reference::KickbackProfile& kick=p.advanced.kickbacks[i];
        check(kick.group==505+2*i && kick.collider_index==801+i && kick.gate_group==501+i &&
            kick.arm_delay==700000000 && kick.pulse_period==100000000,"real kickback identities/timings");
        material_checks(kick.material,.2,.7,1e9,55);
    }
    const std::size_t oneway_numbers[]={1,4,7,8,9,10,11,12,13};
    for (std::size_t i=0;i<9;++i) {
        const pinball::reference::OneWayProfile& wall=p.advanced.one_ways[i];
        check(wall.group==345+i && wall.number==oneway_numbers[i] && wall.solid_index==803+i && wall.default_enabled,
            "real one-way physical descriptors");
        material_checks(wall.material,.8,.9,8.9999999e10,0);
        check(p.static_colliders[wall.solid_index].collision_mask==(i==7?4u:1u),"real one-way source mask");
    }
    const pinball::reference::SinkProfile& escape=p.advanced.escape_chute;
    check(escape.group==344 && escape.collider_index==812 && escape.delay==2000000000 &&
        p.rules.event_group_awards.count(344)==0 && p.sinks.size()==3,"real escape return ownership/no award");
    close(escape.position.x,-7.06160307,"real escape601 X");close(escape.position.y,-1.56909204,"real escape601 Y");
    close(escape.direction.x,0,"real escape direction X");close(escape.direction.y,-1,"real escape direction Y");
    close(escape.speed,35,"real escape throw");close(escape.speed_variation,.05,"real escape variation");
    std::cout << "Reference table real data: 813 descriptors; all ordinary-play physical class families represented\n";
}
}
int main(int argc,char** argv) {
    try {
        if (argc==1) { synthetic();collision_masks_and_trigger_fallback();ramps_and_hole();hit_component_profiles();rule_metadata();multiplier_bank_profile();lane_profiles();medal_profile();advanced_profiles();moving_feature_profiles();remaining_wall_profiles();malformed(); }
        else if (argc==3 && std::string(argv[1])=="--real") real(argv[2]);
        else { std::cerr << "Usage: reference_table_tests [--real <PINBALL.DAT>]\n";return 2; }
        std::cout << "reference_table_tests PASS\n";return 0;
    } catch (const std::exception& error) {
        std::cerr << "reference_table_tests FAIL: " << error.what() << '\n';return 1;
    }
}
