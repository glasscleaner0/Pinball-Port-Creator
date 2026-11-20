#include "flipper.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace pinball {
namespace flipper {
namespace {
struct Wide {
    double x,y;
    Wide(double a=0,double b=0):x(a),y(b){}
    explicit Wide(Vec2 p):x(p.x),y(p.y){}
};
Wide add(Wide a,Wide b){return Wide(a.x+b.x,a.y+b.y);}
Wide sub(Wide a,Wide b){return Wide(a.x-b.x,a.y-b.y);}
Wide mul(Wide a,double v){return Wide(a.x*v,a.y*v);}
double dot(Wide a,Wide b){return a.x*b.x+a.y*b.y;}
double norm(Wide a){
    const double scale=std::max(std::fabs(a.x),std::fabs(a.y));
    if(!std::isfinite(scale))throw std::overflow_error("flipper vector exceeds finite double range");
    if(scale==0)return 0;
    const double x=a.x/scale,y=a.y/scale;
    const double value=scale*std::sqrt(x*x+y*y);
    if(!std::isfinite(value))throw std::overflow_error("flipper distance exceeds finite double range");
    return value;
}
bool finite(Vec2 p){return std::isfinite(p.x)&&std::isfinite(p.y);}
float narrow(double v){
    const double limit=std::numeric_limits<float>::max();
    if(!std::isfinite(v)||v>limit||v< -limit)throw std::overflow_error("flipper result exceeds finite float range");
    return static_cast<float>(v);
}
Vec2 narrow(Wide p){return Vec2(narrow(p.x),narrow(p.y));}
void require_time(Tick value){
    if((static_cast<std::uint64_t>(value)>>63)!=0)throw std::invalid_argument("flipper time must be nonnegative");
}
bool nonzero(Tick value){
    const std::uint64_t bits=static_cast<std::uint64_t>(value);
    volatile std::uint32_t words=static_cast<std::uint32_t>(bits)|static_cast<std::uint32_t>(bits>>32);
    return words!=0;
}
void seconds(double value){
    if(!std::isfinite(value)||value<0)throw std::invalid_argument("flipper seconds must be finite and nonnegative");
}
double rotation(const FlipperConfig& c){
    const Wide a(c.rest_tip_vector),b(c.raised_tip_vector);
    const double cross=a.x*b.y-a.y*b.x;
    // Antiparallel vectors choose +pi deterministically, including signed zero.
    return std::atan2(cross==0?0:cross,dot(a,b));
}
void validate(const FlipperConfig& c){
    require_time(c.raise_duration);require_time(c.return_duration);
    if(!finite(c.pivot)||!finite(c.rest_tip_vector)||!finite(c.raised_tip_vector)||
       !std::isfinite(c.pivot_radius)||!std::isfinite(c.tip_radius)||c.pivot_radius<0||c.tip_radius<0||
       !nonzero(c.raise_duration)||!nonzero(c.return_duration)||
       !std::isfinite(c.restitution)||c.restitution<0||c.restitution>1||
       !std::isfinite(c.smoothness)||c.smoothness<0||c.smoothness>1||
       !std::isfinite(c.impulse_gain)||c.impulse_gain<0||
       (c.response_mode!=ResponseMode::PhysicalSurface&&c.response_mode!=ResponseMode::ReferenceMaterial))
        throw std::invalid_argument("invalid flipper configuration");
    const double a=norm(Wide(c.rest_tip_vector)),b=norm(Wide(c.raised_tip_vector));
    if(a==0||b==0)throw std::invalid_argument("flipper tip vectors must be nonzero");
    const double extent=std::max(a,b)+std::max(c.pivot_radius,c.tip_radius);
    const double rate=double(runtime::ticks_per_second)/double(std::min(c.raise_duration,c.return_duration));
    const double speed=rate*(std::fabs(rotation(c))*extent+std::fabs(b-a));
    if(!std::isfinite(speed)||speed>std::numeric_limits<float>::max())
        throw std::overflow_error("flipper surface speed exceeds finite float range");
}
void validate(const MovingSweepConfig& c){
    if(c.max_iterations==0||c.max_total_iterations==0||
       !std::isfinite(c.distance_tolerance)||c.distance_tolerance<=0||
       !std::isfinite(c.time_tolerance)||c.time_tolerance<=0)
        throw std::invalid_argument("invalid moving sweep budget or tolerance");
}
}
FlipperConfig::FlipperConfig(Vec2 p,Vec2 rest,Vec2 raised,float base,float tip,
                             Tick rising,Tick returning,float bounce,float smoothing,float gain,ResponseMode policy)
    :pivot(p),rest_tip_vector(rest),raised_tip_vector(raised),pivot_radius(base),tip_radius(tip),
     raise_duration(rising),return_duration(returning),restitution(bounce),smoothness(smoothing),
     impulse_gain(gain),response_mode(policy){validate(*this);}
FlipperMotion::FlipperMotion(const FlipperConfig& config,double progress,double rate)
    :config_(config),start_(progress),rate_(rate),angle_(0),angle_delta_(0),
     length_(0),length_delta_(0),duration_(0),speed_(0){
    validate(config_);
    if(!std::isfinite(start_)||start_<0||start_>1||!std::isfinite(rate_))
        throw std::invalid_argument("invalid flipper motion pose/rate");
    angle_=std::atan2(double(config_.rest_tip_vector.y),double(config_.rest_tip_vector.x));
    angle_delta_=rotation(config_);
    length_=norm(Wide(config_.rest_tip_vector));
    length_delta_=norm(Wide(config_.raised_tip_vector))-length_;
    if(rate_>0&&start_<1)duration_=(1-start_)/rate_;
    else if(rate_<0&&start_>0)duration_=-start_/rate_;
    else rate_=0;
    const double extent=std::max(length_,length_+length_delta_)+std::max(config_.pivot_radius,config_.tip_radius);
    speed_=std::fabs(rate_)*(std::fabs(angle_delta_)*extent+std::fabs(length_delta_));
    if(!std::isfinite(speed_)||speed_>std::numeric_limits<float>::max())
        throw std::overflow_error("flipper motion speed exceeds finite float range");
}
FlipperPose FlipperMotion::pose(double time)const{
    seconds(time);
    const bool moving=rate_!=0&&time<duration_;
    const double progress=moving?std::max(0.0,std::min(1.0,start_+rate_*time)):
        (rate_>0?1:(rate_<0?0:start_));
    const double angle=angle_+angle_delta_*progress;
    const double length=length_+length_delta_*progress;
    Wide axis;
    if(progress==0)axis=Wide(config_.rest_tip_vector);
    else if(progress==1)axis=Wide(config_.raised_tip_vector);
    else axis=Wide(std::cos(angle)*length,std::sin(angle)*length);
    const FlipperPose result={config_.pivot,narrow(add(Wide(config_.pivot),axis)),
        config_.pivot_radius,config_.tip_radius,progress,angle,
        moving?angle_delta_*rate_:0,moving?length_delta_*rate_:0};
    return result;
}
double FlipperMotion::duration()const{return duration_;}
double FlipperMotion::speed_bound()const{return speed_;}
float FlipperMotion::restitution()const{return config_.restitution;}
bool FlipperMotion::reference_response()const{return config_.response_mode==ResponseMode::ReferenceMaterial;}
bool FlipperMotion::reference_response_enabled()const{return reference_response();}
FlipperMotion FlipperMotion::frozen_at(double time)const{
    return FlipperMotion(config_,pose(time).normalized,0);
}
response::CollisionResult FlipperMotion::respond(Vec2 incoming,Vec2 normal,Vec2 point,
                                                float ball_radius,double time)const{
    if(!finite(incoming)||!finite(normal)||!finite(point)||!std::isfinite(ball_radius)||ball_radius<=0)
        throw std::invalid_argument("invalid flipper response inputs");
    const Vec2 normal_unit=physics::unit(normal);
    const Wide n(normal_unit);
    const FlipperPose body=pose(time);
    if(!reference_response()){
        const Vec2 surface_velocity=surface(point,time).velocity;
        const Vec2 relative=physics::subtract(incoming,surface_velocity);
        const Vec2 velocity=physics::add(physics::contact_velocity(relative,normal_unit,config_.restitution),surface_velocity);
        const response::CollisionResult result={velocity,narrow(std::fabs(dot(Wide(relative),n))),false,false};
        return result;
    }
    double elasticity=config_.restitution,boost=0;
    if(body.angular_velocity!=0){
        const Wide center=add(Wide(point),mul(n,ball_radius));
        const Wide arm=sub(center,Wide(config_.pivot));
        const double distance=norm(arm);
        const double denominator=length_+ball_radius+config_.tip_radius;
        const double lever=std::max(0.0,std::min(1.0,distance/denominator));
        const double expanded_base=double(config_.pivot_radius)+ball_radius;
        const bool outside_base=distance*distance>1.01*expanded_base*expanded_base;
        const Wide axis(std::cos(body.angle),std::sin(body.angle));
        const double side=axis.x*arm.y-axis.y*arm.x;
        const bool front=body.angular_velocity*side>0||(side==0&&body.angular_velocity>0);
        if(outside_base&&front){
            const Wide perpendicular(-axis.y,axis.x);
            const double sign=body.angular_velocity>0?1:-1;
            const Wide face=add(mul(axis,double(config_.pivot_radius)-config_.tip_radius),
                                mul(perpendicular,sign*length_));
            const double alignment=std::max(0.0,std::min(1.0,dot(face,n)/norm(face)));
            boost=double(config_.impulse_gain)*std::fabs(body.angular_velocity)*lever*alignment;
        }else if(outside_base&&!front){elasticity*=1-lever;}
    }
    return response::collide(incoming,normal_unit,response::Material(narrow(elasticity),config_.smoothness,
        boost>0?-1.0f:std::numeric_limits<float>::max(),narrow(boost)));
}
SurfaceSample FlipperMotion::surface(Vec2 point,double time)const{
    return surface(point.x,point.y,time);
}
SurfaceSample FlipperMotion::surface(double px,double py,double time)const{
    if(!std::isfinite(px)||!std::isfinite(py))throw std::invalid_argument("nonfinite point for flipper distance");
    const FlipperPose body=pose(time);
    const double length=length_+length_delta_*body.normalized;
    Wide axis;
    if(body.normalized==0)axis=Wide(config_.rest_tip_vector);
    else if(body.normalized==1)axis=Wide(config_.raised_tip_vector);
    else axis=Wide(std::cos(body.angle)*length,std::sin(body.angle)*length);
    const Wide base(config_.pivot),p(px,py);
    const Wide direction=mul(axis,1/length),perp(-direction.y,direction.x);
    const Wide offset=sub(p,base);
    const double dr=double(body.tip_radius)-body.pivot_radius;
    double u=0;
    double slope=0;
    if(std::fabs(dr)>=length)u=dr>0?1:0;
    else{
        slope=dr/length;
        const double projection=dot(offset,direction);
        const double across=dot(offset,perp);
        const double along=projection+slope*std::fabs(across)/std::sqrt(1-slope*slope);
        u=std::max(0.0,std::min(1.0,along/length));
    }
    const Wide center=add(base,mul(axis,u));
    const Wide delta=sub(p,center);
    const double distance=norm(delta);
    const double radius=body.pivot_radius+dr*u;
    Wide normal;
    if(distance>0)normal=mul(delta,1/distance);
    else if(u>0&&u<1&&std::fabs(dr)<length)
        normal=add(mul(direction,-slope),mul(perp,std::sqrt(1-slope*slope)));
    else normal=perp;
    const Wide contact=add(center,mul(normal,radius));
    const Wide arm=sub(contact,base);
    const Wide velocity=add(Wide(-body.angular_velocity*arm.y,body.angular_velocity*arm.x),
                            mul(direction,body.length_velocity*u));
    const SurfaceSample result={distance-radius,narrow(contact),narrow(normal),narrow(velocity)};
    return result;
}
TimedFlipper::TimedFlipper(const FlipperConfig& config,Tick now)
    :config_(config),last_time_(now),phase_start_(now),phase_pose_(0),pose_(0),direction_(0),held_(false){
    validate(config_);require_time(now);
}
void TimedFlipper::update(Tick now){
    require_time(now);
    if(now<last_time_)throw std::invalid_argument("flipper clock moved backward");
    const Tick elapsed=now-phase_start_; // Ordered nonnegative subtraction.
    if(direction_>0){
        pose_=std::min(1.0,phase_pose_+double(elapsed)/double(config_.raise_duration));
        if(pose_==1)direction_=0;
    }else if(direction_<0){
        pose_=std::max(0.0,phase_pose_-double(elapsed)/double(config_.return_duration));
        if(pose_==0)direction_=0;
    }
    last_time_=now;
}
void TimedFlipper::set_pressed(bool pressed,Tick now,runtime::TransitionCause cause){
    update(now);
    if(cause!=runtime::TransitionCause::Physical){cancel(now);return;}
    if(pressed==held_)return;
    held_=pressed;phase_start_=now;phase_pose_=pose_;
    direction_=pressed?(pose_<1?1:0):(pose_>0?-1:0);
}
void TimedFlipper::cancel(Tick now){
    update(now);phase_start_=now;phase_pose_=pose_;direction_=0;held_=false;
}
void TimedFlipper::resume_released(Tick now){
    update(now);phase_start_=now;phase_pose_=pose_;held_=false;direction_=pose_>0?-1:0;
}
void TimedFlipper::commit_yield(Tick contact,Tick ending){
    require_time(contact);require_time(ending);
    if(contact<last_time_||ending<contact)throw std::invalid_argument("flipper yield clock order is invalid");
    update(contact);
    phase_pose_=pose_;phase_start_=ending;last_time_=ending;
}
void TimedFlipper::reset(Tick now){
    require_time(now);last_time_=phase_start_=now;phase_pose_=pose_=0;direction_=0;held_=false;
}
double TimedFlipper::pose()const{return pose_;}
bool TimedFlipper::held()const{return held_;}
Tick TimedFlipper::time()const{return last_time_;}
FlipperMotion TimedFlipper::motion()const{
    const double rate=direction_>0?double(runtime::ticks_per_second)/double(config_.raise_duration):
        (direction_<0?-double(runtime::ticks_per_second)/double(config_.return_duration):0);
    return FlipperMotion(config_,pose_,rate);
}
MovingSweepConfig::MovingSweepConfig(std::size_t query,std::size_t step,double distance,double time)
    :max_iterations(query),max_total_iterations(step),distance_tolerance(distance),time_tolerance(time){validate(*this);}
MovingSweep::MovingSweep():hit(),surface_velocity(),time(0),iterations(0),work_budget_exhausted(false){}
MovingSweep sweep_circle(Vec2 position,Vec2 velocity,float radius,const FlipperMotion& motion,
                         double start,double maximum,const MovingSweepConfig& config,
                         std::size_t available){
    validate(config);seconds(start);seconds(maximum);
    if(!finite(position)||!finite(velocity)||!std::isfinite(radius)||radius<=0)
        throw std::invalid_argument("invalid ball for moving flipper sweep");
    if(start>std::numeric_limits<double>::max()-maximum)
        throw std::overflow_error("flipper sweep interval overflow");
    MovingSweep result;
    const double ball_speed=norm(Wide(velocity));
    const std::size_t budget=std::min(config.max_iterations,available);
    double time=0;
    for(std::size_t i=0;i<budget;++i){
        ++result.iterations;
        const double moving_speed=start+time>=motion.duration()?0:motion.speed_bound();
        const double bound=ball_speed+moving_speed;
        const SurfaceSample surface=motion.surface(double(position.x)+double(velocity.x)*time,
            double(position.y)+double(velocity.y)*time,start+time);
        const double gap=surface.separation-radius;
        const Wide relative=sub(Wide(velocity),Wide(surface.velocity));
        const double incoming=dot(relative,Wide(surface.normal));
        if(gap<0 || gap<=config.distance_tolerance || (bound>0&&gap/bound<=config.time_tolerance)){
            // A supporting plane proves an outgoing linear path cannot enter
            // a stationary convex hull. This avoids repeated resting contacts.
            if(gap>=0&&incoming>=0&&moving_speed==0)return result;
            if(bound==0&&gap>=0)return result;
            result.hit.hit=true;
            result.hit.initial_overlap=time==0&&gap<0;
            result.hit.time=narrow(time);
            result.hit.penetration=narrow(std::max(0.0,-gap));
            result.hit.point=surface.point;result.hit.normal=surface.normal;
            result.surface_velocity=surface.velocity;result.time=time;
            return result;
        }
        if(time>=maximum || bound==0)return result;
        // Distance is 1-Lipschitz in ball travel and in the bound on every
        // material surface speed. This increment cannot skip first contact.
        const double increment=gap/bound;
        const double remaining=maximum-time;
        if(increment>remaining)return result;
        const double next=time+increment;
        if(next<=time){result.work_budget_exhausted=true;return result;}
        time=std::min(maximum,next);
    }
    result.work_budget_exhausted=true;
    return result;
}

} // namespace flipper
} // namespace pinball
