#include "material_response.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace pinball::response;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void close(double a,double b,const char* message,double tolerance=.00001){
    check(std::isfinite(a)&&std::fabs(a-b)<=tolerance,message);
}
double speed(Vec2 v){return std::sqrt(double(v.x)*v.x+double(v.y)*v.y);}
template<class E,class F>void throws(F f,const char* message){try{f();}catch(const E&){return;}throw std::runtime_error(message);}
void collision_oracles(){
    const Vec2 n(0,1);
    const Material wall(.5f,.8f,1e9f,0);
    CollisionResult r=collide(Vec2(0,-10),n,wall);
    close(r.velocity.x,0,"head-on x");close(r.velocity.y,5,"head-on material attenuation");
    close(r.incident_normal_speed,10,"pre-damping incidence");check(!r.threshold_met&&!r.boost_applied,"passive boost flag");
    r=collide(Vec2(3,-4),n,wall);
    close(r.velocity.x,2.30466384,"oblique E/S direction x");close(r.velocity.y,1.92055320,"oblique E/S direction y");
    close(speed(r.velocity),3,"oblique total speed attenuation");close(r.incident_normal_speed,4,"oblique threshold quantity");
    r=collide(Vec2(3,4),n,wall);
    close(r.velocity.x,1.8,"outgoing speed attenuation x");close(r.velocity.y,2.4,"outgoing was incorrectly reflected");
    r=collide(Vec2(3,0),n,wall);close(r.velocity.x,3,"tangent velocity changed");close(r.velocity.y,0,"tangent gained normal speed");
    r=collide(Vec2(0,-10),n,Material(.75f,.5f,1e9f,0));
    close(r.velocity.y,7.5,"flipper passive material oracle");
    r=collide(Vec2(0,-4),n,Material(.5f,.5f,0,100));
    close(r.velocity.y,102,"boost replaced rather than added to damped velocity");
    check(r.threshold_met&&r.boost_applied,"eligible boost flags");
    r=collide(Vec2(),n,Material(.5f,.5f,0,100));close(r.velocity.y,100,"stationary zero-threshold boost");
    r=collide(Vec2(),n,Material(.5f,.5f,-1,2));close(r.velocity.y,2,"negative threshold did not allow stationary boost");
    r=collide(Vec2(0,-2),n,Material(.5f,.8f,2,3));close(r.velocity.y,4,"inclusive threshold equality");
    r=collide(Vec2(0,-2),n,Material(.5f,.8f,2.0001f,3));close(r.velocity.y,1,"below-threshold boost applied");
    check(!r.threshold_met&&!r.boost_applied,"below-threshold flags");
    r=collide(Vec2(3,-4),n,Material(.5f,.8f,4,2));
    close(r.velocity.x,2.30466384,"boost changed tangent");close(r.velocity.y,3.92055320,"boosted oblique normal");
    r=collide(Vec2(3,-4),Vec2(0,8),wall);
    close(r.velocity.x,2.30466384,"normal length changed material result");
    const CollisionResult rotated=collide(Vec2(4,3),Vec2(-1,0),wall);
    close(rotated.velocity.x,-r.velocity.y,"rotation covariance x");close(rotated.velocity.y,r.velocity.x,"rotation covariance y");
    r=collide(Vec2(3,-4),n,Material(0,0,1e9f,0));check(r.velocity.x==0&&r.velocity.y==0,"zero-weight absorption policy");
}
void passive_invariants(){
    for(int i=0;i<36;++i){
        const double angle=i*6.283185307179586/36;
        const Vec2 n(static_cast<float>(std::cos(angle)),static_cast<float>(std::sin(angle)));
        for(int e=0;e<=4;++e)for(int s=0;s<=4;++s){
            const Material m(e*.25f,s*.25f,1e9f,0);
            for(int x=-3;x<=3;++x)for(int y=-3;y<=3;++y){
                const Vec2 v(static_cast<float>(x),static_cast<float>(y));
                const CollisionResult r=collide(v,n,m);
                check(speed(r.velocity)<=speed(v)+.00001,"passive material created kinetic energy");
                check(!r.boost_applied,"passive invariant boost flag");
            }
        }
    }
    const Vec2 physical=pinball::physics::contact_velocity(Vec2(3,-4),Vec2(0,1),.5f);
    close(physical.x,3,"generic physical helper changed");close(physical.y,2,"generic physical response changed");
    const Vec2 material=collide(Vec2(3,-4),Vec2(0,1),Material(.5f,.8f,1e9f,0)).velocity;
    check(std::fabs(material.x-physical.x)>.5,"material and physical policies accidentally conflated");
}
void field_oracles(){
    const FieldConfig field(Vec2(0,12),.2f,.1f,60);
    Vec2 v=advance_field(Vec2(3,-4),.01,field,0);
    close(v.x,2.994,"field x drag");close(v.y,-3.872,"field gravity/drag");
    const FieldConfig noise(Vec2(),0,.1f,60);
    const Vec2 plus=advance_field(Vec2(3,4),.2,noise,1),minus=advance_field(Vec2(3,4),.2,noise,-1);
    close(plus.x,3.1,"positive speed-scaled noise");close(minus.x,2.9,"negative speed-scaled noise");
    close(plus.y,4,"noise leaked into y");close(minus.y,4,"negative noise leaked into y");
    v=advance_field(Vec2(0,102),1.0/120,field,0);close(v.y,60,"ordinary refresh did not cap previous collision boost");
    v=advance_field(Vec2(3,4),0,FieldConfig(Vec2(),0,0,2),0);
    close(v.x,1.2,"zero-time refresh cap x");close(v.y,1.6,"zero-time refresh cap y");
    v=Vec2(3,4);const FieldConfig drag(Vec2(),.2f,0,60);
    double previous=speed(v);
    for(int i=0;i<1000;++i){v=advance_field(v,.01,drag,0);check(speed(v)<=previous+.000001,"passive drag gained energy");previous=speed(v);}
    v=advance_field(Vec2(1,0),10,FieldConfig(Vec2(),.2f,0,60),0);
    close(v.x,-1,"coarse Euler drag was silently clamped");
}
void validation_and_extremes(){
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    throws<std::invalid_argument>([]{Material(-1,.5f,0,0);},"negative elasticity accepted");
    throws<std::invalid_argument>([]{Material(.5f,1.1f,0,0);},"invalid smoothness accepted");
    throws<std::invalid_argument>([&]{Material(.5f,.5f,nan,0);},"nonfinite threshold accepted");
    throws<std::invalid_argument>([]{Material(.5f,.5f,0,-1);},"negative boost accepted");
    const Material m(.5f,.8f,1e9f,0);
    throws<std::invalid_argument>([&]{collide(Vec2(1,0),Vec2(),m);},"zero normal accepted");
    throws<std::invalid_argument>([&]{collide(Vec2(inf,0),Vec2(0,1),m);},"infinite input velocity accepted");
    Material changed=m;changed.boost=nan;
    throws<std::invalid_argument>([&]{collide(Vec2(),Vec2(0,1),changed);},"mutated invalid material accepted");
    throws<std::invalid_argument>([]{FieldConfig(Vec2(),-1,0,60);},"negative drag accepted");
    throws<std::invalid_argument>([]{FieldConfig(Vec2(),0,-1,60);},"negative noise accepted");
    throws<std::invalid_argument>([]{FieldConfig(Vec2(),0,0,0);},"zero speed limit accepted");
    const FieldConfig f(Vec2(),0,0,60);
    throws<std::invalid_argument>([&]{advance_field(Vec2(),-1,f,0);},"negative field time accepted");
    throws<std::invalid_argument>([&]{advance_field(Vec2(),std::numeric_limits<double>::infinity(),f,0);},"infinite field time accepted");
    throws<std::invalid_argument>([&]{advance_field(Vec2(),0,f,1.01f);},"out-of-range random sample accepted");
    FieldConfig changed_field=f;changed_field.gravity.x=nan;
    throws<std::invalid_argument>([&]{advance_field(Vec2(),0,changed_field,0);},"mutated invalid field accepted");
    const float maximum=std::numeric_limits<float>::max();
    const CollisionResult large=collide(Vec2(0,-maximum),Vec2(0,maximum),Material(1,1,1e9f,0));
    check(large.velocity.y==maximum,"large normal normalization overflowed");
    const Vec2 limited=advance_field(Vec2(maximum,maximum),0,f,0);
    close(speed(limited),60,"large finite vector could not be capped",.0001);
    throws<std::overflow_error>([&]{collide(Vec2(0,-maximum),Vec2(0,1),Material(1,1,0,maximum));},
                                "unrepresentable boosted result accepted");
    throws<std::overflow_error>([&]{advance_field(Vec2(maximum,0),std::numeric_limits<double>::max(),
        FieldConfig(Vec2(maximum,0),0,0,60),0);},"field intermediate overflow was not rejected");
}
}
int main(){
    try{collision_oracles();passive_invariants();field_oracles();validation_and_extremes();
        std::cout<<"material response and field checks passed; policy/oracles derive from authorized behavioral study\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
