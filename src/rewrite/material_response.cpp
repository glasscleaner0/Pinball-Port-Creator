#include "material_response.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace pinball {
namespace response {
namespace {
struct Wide {
    double x,y;
    Wide(double a=0,double b=0):x(a),y(b){}
    explicit Wide(Vec2 p):x(p.x),y(p.y){}
};
bool finite(Vec2 p){return std::isfinite(p.x)&&std::isfinite(p.y);}
double length(Wide p){
    const double scale=std::max(std::fabs(p.x),std::fabs(p.y));
    if(!std::isfinite(scale))throw std::overflow_error("response vector exceeds finite range");
    if(scale==0)return 0;
    const double x=p.x/scale,y=p.y/scale;
    const double result=scale*std::sqrt(x*x+y*y);
    if(!std::isfinite(result))throw std::overflow_error("response magnitude exceeds finite range");
    return result;
}
float narrow(double value){
    if(!std::isfinite(value)||std::fabs(value)>std::numeric_limits<float>::max())
        throw std::overflow_error("response result exceeds finite float range");
    return static_cast<float>(value);
}
Vec2 narrow(Wide p){return Vec2(narrow(p.x),narrow(p.y));}
void validate(const Material& m){
    if(!std::isfinite(m.elasticity)||m.elasticity<0||m.elasticity>1||
       !std::isfinite(m.smoothness)||m.smoothness<0||m.smoothness>1||
       !std::isfinite(m.threshold)||!std::isfinite(m.boost)||m.boost<0)
        throw std::invalid_argument("invalid material response parameters");
}
void validate(const FieldConfig& c){
    if(!finite(c.gravity)||!std::isfinite(c.drag_per_second)||c.drag_per_second<0||
       !std::isfinite(c.lateral_noise_per_speed)||c.lateral_noise_per_speed<0||
       !std::isfinite(c.maximum_speed)||c.maximum_speed<=0)
        throw std::invalid_argument("invalid field parameters");
}
}
Material::Material():Material(1,1,(std::numeric_limits<float>::max)(),0) {}
Material::Material(float e,float s,float t,float b):elasticity(e),smoothness(s),threshold(t),boost(b){validate(*this);}
CollisionResult collide(Vec2 velocity,Vec2 normal,const Material& material){
    validate(material);
    if(!finite(velocity)||!finite(normal))throw std::invalid_argument("response vectors must be finite");
    const Wide v(velocity);
    const double speed=length(v),normal_length=length(Wide(normal));
    if(normal_length==0)throw std::invalid_argument("response normal must be nonzero");
    const Wide n(double(normal.x)/normal_length,double(normal.y)/normal_length);
    const double projection=v.x*n.x+v.y*n.y;
    const double incident=std::fabs(projection);
    const double remaining=std::max(0.0,speed-(1-double(material.elasticity))*incident);
    Wide direction;
    if(projection>0){direction=v;}
    else{
        // Resolve in an orthonormal contact basis. The relative weights set
        // direction; the separate incidence law sets total speed.
        const double tx=v.x-projection*n.x,ty=v.y-projection*n.y;
        direction=Wide(double(material.smoothness)*tx-double(material.elasticity)*projection*n.x,
                       double(material.smoothness)*ty-double(material.elasticity)*projection*n.y);
    }
    const double magnitude=length(direction);
    Wide out;
    if(magnitude>0){out=Wide(direction.x*(remaining/magnitude),direction.y*(remaining/magnitude));}
    const bool eligible=incident>=double(material.threshold);
    if(eligible){out.x+=double(material.boost)*n.x;out.y+=double(material.boost)*n.y;}
    const CollisionResult result={narrow(out),narrow(incident),eligible,eligible&&material.boost>0};
    return result;
}
FieldConfig::FieldConfig(Vec2 g,float drag,float noise,float limit)
    :gravity(g),drag_per_second(drag),lateral_noise_per_speed(noise),maximum_speed(limit){validate(*this);}
Vec2 advance_field(Vec2 velocity,double seconds,const FieldConfig& config,float random_unit){
    validate(config);
    if(!finite(velocity)||!std::isfinite(seconds)||seconds<0||
       !std::isfinite(random_unit)||random_unit< -1||random_unit>1)
        throw std::invalid_argument("invalid field input/time/noise sample");
    const Wide v(velocity);
    const double speed=length(v);
    const double ax=double(config.gravity.x)-double(config.drag_per_second)*v.x+
        double(config.lateral_noise_per_speed)*speed*random_unit;
    const double ay=double(config.gravity.y)-double(config.drag_per_second)*v.y;
    Wide out(v.x+ax*seconds,v.y+ay*seconds);
    const double magnitude=length(out);
    if(magnitude>config.maximum_speed){
        const double scale=double(config.maximum_speed)/magnitude;out.x*=scale;out.y*=scale;
    }
    return narrow(out);
}
} // namespace response
} // namespace pinball
