#include "projection.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace pinball;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void near(double actual,double expected,double tolerance,const char* message) {
    check(std::fabs(actual-expected)<=tolerance,message);
}
template<class F> void throws(F function) {
    bool caught=false;try{function();}catch(const std::exception&){caught=true;}
    check(caught,"expected projection rejection");
}
void synthetic() {
    visual::Camera camera;
    camera.transform[11]=8;
    camera.center_x=10;camera.center_y=20;camera.focal=-100;
    camera.depth_origin=2;camera.depth_scale=10;
    const visual::ProjectedPoint p=visual::project(camera,3,4,4);
    near(p.x,-15,1e-12,"perspective x");near(p.y,-13.3333333333333,1e-10,"perspective y");
    near(p.distance,13,1e-12,"radial distance");near(p.depth,110,1e-12,"radial depth");
    check(p.in_front,"front visibility");
    double x=99,y=98;
    check(visual::unproject_plane(camera,p.x,p.y,4,x,y),"inverse plane");
    near(x,3,1e-12,"inverse x");near(y,4,1e-12,"inverse y");
    check(!visual::project(camera,0,0,-8).in_front,"camera plane is excluded");
    check(!visual::project(camera,0,0,-9).in_front,"behind camera excluded");
    x=99;y=98;
    check(!visual::unproject_plane(camera,10,20,-8,x,y),"degenerate inverse excluded");
    near(x,99,0,"failed inverse leaves x");near(y,98,0,"failed inverse leaves y");
    visual::Camera small;
    small.transform[0]=small.transform[5]=small.transform[10]=small.transform[11]=1e-9;
    const visual::ProjectedPoint tiny=visual::project(small,1,2,0);
    check(visual::unproject_plane(small,tiny.x,tiny.y,0,x,y),"scaled camera inverse");
    near(x,1,1e-12,"scaled inverse x");near(y,2,1e-12,"scaled inverse y");
    camera.focal=0;throws([&](){visual::project(camera,0,0,1);});
    camera.focal=-100;camera.depth_scale=-1;throws([&](){visual::project(camera,0,0,1);});
    camera.depth_scale=1;
    throws([&](){visual::project(camera,std::numeric_limits<double>::infinity(),0,1);});
    camera.transform[0]=2;
    throws([&](){visual::project(camera,std::numeric_limits<double>::max(),0,1);});
}
void real(const char* path) {
    const table::TableModel table=table::TableModel::load(path);
    const visual::Camera camera=visual::camera_from_table(table);
    // Numeric reconstruction check, not a runtime screenshot match.
    const visual::ProjectedPoint seat=visual::project(camera,-7.193486215,11.6749784,.3000000119);
    near(seat.x,329.4125,.01,"asset camera candidate seat x");
    near(seat.y,375.4308,.01,"asset camera candidate seat y");
    for(int a=-7;a<=7;++a)for(int b=-13;b<=13;++b) {
        const visual::ProjectedPoint p=visual::project(camera,a,b,.3);
        double x=0,y=0;
        check(visual::unproject_plane(camera,p.x,p.y,.3,x,y),"asset plane inverse");
        near(x,a,1e-10,"asset inverse x");near(y,b,1e-10,"asset inverse y");
    }
    // The independently probed exposed floor pixel supports radial rather
    // than camera-Z depth. It does not validate raised objects or ramp paths.
    double x=0,y=0;
    check(visual::unproject_plane(camera,183,420,0,x,y),"floor inverse");
    near(visual::project(camera,x,y,0).depth,6453,1,"floor depth sample");
}
}
int main(int argc,char**argv) {
    try {
        synthetic();if(argc>1)real(argv[1]);
        std::cout<<"projection tests passed\n";return 0;
    } catch(const std::exception&e) {std::cerr<<e.what()<<'\n';return 1;}
}
