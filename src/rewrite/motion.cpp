#include "motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace pinball {
namespace motion {
namespace {
bool finite(Vec2 value) { return std::isfinite(value.x) && std::isfinite(value.y); }
void require_time(Tick value) {
    // Inspect the sign bit as a 0/1 value, avoiding signed subtraction and
    // the known Canary record-form 64-bit sign-check mismatch.
    if ((static_cast<std::uint64_t>(value) >> 63) != 0)
        throw std::invalid_argument("motion time must be nonnegative");
}
float narrow(double value) {
    const double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(value) || value > limit || value < -limit)
        throw std::overflow_error("motion result exceeds finite float range");
    return static_cast<float>(value);
}
Vec2 velocity_at(Vec2 velocity, Vec2 acceleration, double seconds) {
    return Vec2(narrow(velocity.x + double(acceleration.x) * seconds),
                narrow(velocity.y + double(acceleration.y) * seconds));
}
Vec2 position_at(Vec2 position, Vec2 velocity, Vec2 acceleration, double seconds) {
    return Vec2(narrow(position.x + double(velocity.x) * seconds +
                       0.5 * double(acceleration.x) * seconds * seconds),
                narrow(position.y + double(velocity.y) * seconds +
                       0.5 * double(acceleration.y) * seconds * seconds));
}
void validate_plane(const SurfacePlane& plane) {
    if(!finite(plane.gradient) || !std::isfinite(plane.offset) ||
       !finite(plane.field_force) || !std::isfinite(plane.drag) || plane.drag<0)
        throw std::invalid_argument("invalid surface plane");
}
void sync_height(BallState& ball) {
    if(ball.plane.active)
        ball.height=narrow(double(ball.radius)+double(ball.plane.gradient.x)*ball.position.x+
                           double(ball.plane.gradient.y)*ball.position.y+ball.plane.offset);
}
void validate_transition(const SurfaceTransition& target) {
    validate_plane(target.plane);
    if(target.target_mask==0 || !std::isfinite(target.height_offset))
        throw std::invalid_argument("invalid surface transition");
}
physics::SweepHit center_crossing(Vec2 position,Vec2 velocity,Vec2 start,Vec2 end,double horizon) {
    physics::SweepHit hit;
    const double dx=double(end.x)-start.x,dy=double(end.y)-start.y;
    const double length_squared=dx*dx+dy*dy,length=std::sqrt(length_squared);
    const double nx=-dy/length,ny=dx/length;
    const double px=double(position.x)-start.x,py=double(position.y)-start.y;
    const double distance=px*nx+py*ny,approach=double(velocity.x)*nx+double(velocity.y)*ny;
    if(distance<0 || approach>=0)return hit;
    const double time=-distance/approach;
    if(time<0 || time>horizon)return hit;
    const double u=((px+double(velocity.x)*time)*dx+(py+double(velocity.y)*time)*dy)/length_squared;
    if(u<0 || u>1)return hit;
    hit.hit=true;hit.time=narrow(time);
    hit.point=Vec2(narrow(start.x+u*dx),narrow(start.y+u*dy));
    hit.normal=Vec2(narrow(nx),narrow(ny));
    return hit;
}
void validate_ball(const BallState& ball) {
    if (!finite(ball.position) || !finite(ball.velocity) ||
        !std::isfinite(ball.radius) || ball.radius <= 0 || !std::isfinite(ball.height))
        throw std::invalid_argument("invalid ball state");
    validate_plane(ball.plane);
}
void validate_collider(const StaticCollider& collider) {
    if ((collider.kind != ColliderKind::Segment && collider.kind != ColliderKind::Disk) ||
        !finite(collider.first) || !finite(collider.second) ||
        !std::isfinite(collider.radius) || collider.radius < 0 ||
        !std::isfinite(collider.restitution) || collider.restitution < 0 ||
        collider.restitution > 1)
        throw std::invalid_argument("invalid static collider");
    if(collider.center_trigger &&
       (collider.kind != ColliderKind::Disk || !collider.captures_ball || collider.radius <= 0))
        throw std::invalid_argument("invalid center capture sensor");
    if(collider.directional_approach_only&&
       (collider.kind!=ColliderKind::Segment||collider.two_sided||collider.center_crossing||collider.center_trigger||
        (collider.first.x==collider.second.x&&collider.first.y==collider.second.y)))
        throw std::invalid_argument("directional approach requires a nonzero one-sided solid segment");
    validate_transition(collider.transition);
    if(collider.applies_transition && !collider.center_crossing)
        throw std::invalid_argument("surface transition requires a center crossing");
    if(collider.center_crossing &&
       (collider.kind!=ColliderKind::Segment || collider.captures_ball || collider.center_trigger ||
        (collider.first.x==collider.second.x && collider.first.y==collider.second.y)))
        throw std::invalid_argument("invalid surface crossing collider");
    if((collider.latch_response || collider.deactivate_on_activation) && (!collider.reference_response || collider.captures_ball || collider.center_crossing))
        throw std::invalid_argument("response latch requires a physical reference collider");
    if(collider.reference_response) {
        const response::Material checked(collider.material.elasticity,collider.material.smoothness,
            collider.material.threshold,collider.material.boost);
        (void)checked;
    }
}
void validate_config(const MotionConfig& config) {
    require_time(config.max_step);
    if (!finite(config.acceleration) || config.max_step == 0 ||
        config.max_contacts == 0 || !std::isfinite(config.separation_slop) ||
        config.separation_slop < 0 || !std::isfinite(config.maximum_query_speed) ||
        config.maximum_query_speed < 0)
        throw std::invalid_argument("invalid motion configuration");
}
void validate_plunger(const PlungerConfig& config) {
    require_time(config.pull_duration);
    require_time(config.return_duration);
    if (config.pull_duration == 0 || config.return_duration == 0 ||
        !finite(config.launch_direction) ||
        (config.launch_direction.x == 0 && config.launch_direction.y == 0) ||
        !std::isfinite(config.minimum_speed) || config.minimum_speed < 0 ||
        !std::isfinite(config.maximum_speed) || config.maximum_speed < config.minimum_speed ||
        !std::isfinite(config.minimum_pull) || config.minimum_pull < 0 || config.minimum_pull > 1)
        throw std::invalid_argument("invalid plunger configuration");
}
} // namespace

SurfacePlane::SurfacePlane():active(false),gradient(),offset(0),field_force(),drag(0),
    id((std::numeric_limits<std::size_t>::max)()) {}
SurfaceTransition::SurfaceTransition():target_mask(1),plane(),set_height(false),height_offset(0) {}
BallState::BallState(Vec2 p, Vec2 v, float r) : position(p), velocity(v), radius(r),
    collision_mask(1),height(r),plane() {
    validate_ball(*this);
}
StaticCollider StaticCollider::segment(Vec2 start, Vec2 end, float bounce, bool both) {
    StaticCollider result = { ColliderKind::Segment, start, end, 0, bounce, both, false, false, false, true, (std::numeric_limits<std::uint32_t>::max)(), false, SurfaceTransition(), response::Material(bounce,1,(std::numeric_limits<float>::max)(),0), false, false, (std::numeric_limits<std::size_t>::max)(), false, false };
    validate_collider(result);
    return result;
}
StaticCollider StaticCollider::disk(Vec2 center, float r, float bounce) {
    StaticCollider result = { ColliderKind::Disk, center, Vec2(), r, bounce, true, false, false, false, true, (std::numeric_limits<std::uint32_t>::max)(), false, SurfaceTransition(), response::Material(bounce,1,(std::numeric_limits<float>::max)(),0), false, false, (std::numeric_limits<std::size_t>::max)(), false, false };
    validate_collider(result);
    return result;
}
StaticCollider StaticCollider::center_capture(Vec2 center, float radius) {
    StaticCollider result = disk(center,radius,0);
    result.captures_ball = true;
    result.center_trigger = true;
    validate_collider(result);
    return result;
}
StaticCollider StaticCollider::event_segment(Vec2 start,Vec2 end,std::uint32_t listen) {
    StaticCollider result=segment(start,end,0,false);
    result.collision_mask=listen;result.center_crossing=true;
    validate_collider(result);return result;
}
StaticCollider StaticCollider::transition_segment(Vec2 start,Vec2 end,
    std::uint32_t listen,const SurfaceTransition& target) {
    StaticCollider result=segment(start,end,0,false);
    result.collision_mask=listen;result.center_crossing=true;result.transition=target;result.applies_transition=true;
    validate_collider(result);
    return result;
}
MotionConfig::MotionConfig(Vec2 a, Tick maximum, std::size_t contacts, float slop, float query_speed,
                           std::uint32_t moving_mask)
    : acceleration(a), max_step(maximum), max_contacts(contacts), separation_slop(slop),
      maximum_query_speed(query_speed),moving_collision_mask(moving_mask) {
    validate_config(*this);
}
BallStepper::BallStepper(const MotionConfig& config) : config_(config) { validate_config(config_); }
const MotionConfig& BallStepper::config() const { return config_; }

MotionStep BallStepper::step(BallState& ball, Tick duration,
                             const std::vector<StaticCollider>& colliders) const {
    const std::vector<flipper::FlipperMotion> none;
    // These placeholders are never used with an empty moving list.
    return step(ball,duration,colliders,none,flipper::MovingSweepConfig(1,1,1,1));
}
MotionStep BallStepper::step(BallState& ball,Tick duration,
                             const std::vector<StaticCollider>& colliders,
                             const std::vector<flipper::FlipperMotion>& moving,
                             const flipper::MovingSweepConfig& supplied)const{
    require_time(duration);
    if(duration>config_.max_step)throw std::invalid_argument("motion step exceeds time budget");
    const flipper::MovingSweepConfig sweep(supplied.max_iterations,supplied.max_total_iterations,
        supplied.distance_tolerance,supplied.time_tolerance);
    validate_ball(ball);
    for(std::size_t i=0;i<colliders.size();++i)validate_collider(colliders[i]);
    MotionStep result={0,0,false,std::vector<MotionContact>(),false,0,std::vector<FlipperYield>(),false,0};
    if(duration==0)return result;
    BallState next=ball;
    sync_height(next);
    std::vector<flipper::FlipperMotion> effective=moving;
    std::vector<bool> yielded(moving.size(),false);
    std::vector<bool> crossed(colliders.size(),false);
    std::vector<bool> latched(colliders.size(),false);
    std::vector<bool> deactivated(colliders.size(),false);
    double remaining=double(duration)/double(runtime::ticks_per_second);
    while(remaining>0){
        if(config_.maximum_query_speed>0){
            const double x=next.velocity.x,y=next.velocity.y;
            const double speed_squared=x*x+y*y;
            const double limit=config_.maximum_query_speed;
            if(speed_squared>limit*limit){
                const double scale=limit/std::sqrt(speed_squared);
                next.velocity=Vec2(narrow(x*scale),narrow(y*scale));
            }
        }
        double interval=remaining;
        const bool moving_enabled=(next.collision_mask&config_.moving_collision_mask)!=0;
        for(std::size_t i=0;moving_enabled&&i<moving.size();++i){
            const double until=effective[i].duration()-result.simulated_seconds;
            if(until>sweep.time_tolerance&&until<interval)interval=until;
        }
        const Vec2 chord=velocity_at(next.velocity,config_.acceleration,interval*0.5);
        const float horizon=narrow(interval);
        physics::SweepHit best;
        double best_time=interval;
        std::size_t best_index=0;
        bool best_moving=false;
        Vec2 surface_velocity;
        for(std::size_t i=0;i<colliders.size();++i){
            const StaticCollider& c=colliders[i];
            if(!c.enabled || deactivated[i] || !(c.collision_mask&next.collision_mask) || (c.center_crossing&&crossed[i]))continue;
            // Directed captures and explicitly one-way solids accept inward motion.
            // Outgoing starts may overlap the radius strip after a paired
            // pass event or release; they must clear it without depenetration.
            if((c.captures_ball||c.directional_approach_only) && c.kind==ColliderKind::Segment && !c.two_sided && !c.center_trigger) {
                const double dx=double(c.second.x)-c.first.x,dy=double(c.second.y)-c.first.y;
                if((dx!=0||dy!=0) && dx*chord.y-dy*chord.x>=0)continue;
            }
            physics::SweepHit hit;
            if(c.center_crossing){
                hit=center_crossing(next.position,chord,c.first,c.second,interval);
            }else if(c.center_trigger){
                // Only the sum of radii enters this relative sweep. Exchange
                // the positive trigger radius and zero point radius because
                // the shared primitive requires a positive moving radius.
                hit=physics::sweep_circle_circle(next.position,chord,c.radius,c.first,0,horizon);
                if(hit.hit)hit.point=position_at(next.position,chord,Vec2(),hit.time);
            }else if(c.kind==ColliderKind::Segment){
                hit=physics::sweep_circle_segment(next.position,chord,next.radius,c.first,c.second,horizon,c.two_sided);
            }else{
                hit=physics::sweep_circle_circle(next.position,chord,next.radius,c.first,c.radius,horizon);
            }
            if(hit.hit&&(!best.hit||hit.time<best_time)){
                best=hit;best_time=std::min(interval,double(hit.time));best_index=i;
            }
        }
        for(std::size_t i=0;moving_enabled&&i<moving.size()&&(!best.hit||best_time>0);++i){
            const std::size_t available=sweep.max_total_iterations-result.moving_iterations;
            const flipper::MovingSweep candidate=flipper::sweep_circle(next.position,chord,next.radius,
                effective[i],result.simulated_seconds,best.hit?best_time:interval,sweep,available);
            result.moving_iterations+=candidate.iterations;
            if(candidate.work_budget_exhausted){
                result.moving_work_exhausted=true;result.discarded_seconds=remaining;ball=next;return result;
            }
            if(candidate.hit.hit&&(!best.hit||candidate.time<best_time)){
                best=candidate.hit;best_time=candidate.time;best_index=i;best_moving=true;
                surface_velocity=candidate.surface_velocity;
            }
        }
        if(!best.hit){
            next.position=position_at(next.position,next.velocity,config_.acceleration,interval);
            sync_height(next);
            next.velocity=velocity_at(next.velocity,config_.acceleration,interval);
            result.simulated_seconds+=interval;remaining-=interval;
            continue;
        }
        if(!best_moving&&colliders[best_index].captures_ball){
            const double travel=std::min(interval,best_time);
            if(colliders[best_index].center_trigger){
                next.position=position_at(next.position,next.velocity,config_.acceleration,travel);
                best.point=next.position;
            }else{
                const Vec2 normal=physics::unit(best.normal);
                const double offset=double(next.radius)+config_.separation_slop;
                next.position=Vec2(narrow(best.point.x+double(normal.x)*offset),
                                   narrow(best.point.y+double(normal.y)*offset));
            }
            sync_height(next);
            const float incoming_speed=narrow(physics::length(velocity_at(next.velocity,config_.acceleration,travel)));
            next.velocity=Vec2();
            result.simulated_seconds+=travel;
            result.discarded_seconds=remaining-travel;
            result.captured=true;result.capture_collider_index=best_index;
            const MotionContact terminal={best_index,result.simulated_seconds,best,false,Vec2(),false,false,0,false,false,incoming_speed};
            result.contacts.push_back(terminal);
            ball=next;return result;
        }
        if(result.contacts.size()==config_.max_contacts){
            result.contact_budget_exhausted=true;result.discarded_seconds=remaining;break;
        }
        const double travel=std::min(interval,best_time);
        if(!best_moving&&colliders[best_index].center_crossing){
            next.position=position_at(next.position,next.velocity,config_.acceleration,travel);
            next.velocity=velocity_at(next.velocity,config_.acceleration,travel);
            sync_height(next);
            const bool transition=colliders[best_index].applies_transition;
            if(transition) {
                const SurfaceTransition& target=colliders[best_index].transition;
                next.collision_mask=target.target_mask;next.plane=target.plane;
                if(next.plane.active)sync_height(next);
                else if(target.set_height)next.height=narrow(double(next.radius)+target.height_offset);
            }
            best.point=next.position;
            result.simulated_seconds+=travel;remaining-=travel;crossed[best_index]=true;
            const MotionContact event={best_index,result.simulated_seconds,best,false,Vec2(),false,transition,0,false,true,narrow(physics::length(next.velocity))};
            result.contacts.push_back(event);
            continue;
        }
        next.velocity=velocity_at(next.velocity,config_.acceleration,travel);
        const float incoming_speed=narrow(physics::length(next.velocity));
        bool boosted=false,activated=false;
        float incident=0;
        if(best_moving) {
            const response::CollisionResult resolved=effective[best_index].respond(next.velocity,best.normal,
                best.point,next.radius,result.simulated_seconds+travel);
            next.velocity=resolved.velocity;boosted=resolved.boost_applied;
            incident=resolved.incident_normal_speed;
        } else if(colliders[best_index].reference_response) {
            response::Material material=colliders[best_index].material;
            if(latched[best_index])material.threshold=1e9f;
            const response::CollisionResult resolved=response::collide(next.velocity,best.normal,material);
            incident=resolved.incident_normal_speed;activated=incident>material.threshold;
            if(activated && colliders[best_index].latch_response)latched[best_index]=true;
            if(activated && colliders[best_index].deactivate_on_activation) {
                const std::size_t owner=colliders[best_index].response_owner;
                deactivated[best_index]=true;
                if(owner!=(std::numeric_limits<std::size_t>::max)())
                    for(std::size_t i=0;i<colliders.size();++i)
                        if(colliders[i].response_owner==owner)deactivated[i]=true;
            }
            next.velocity=resolved.velocity;boosted=resolved.boost_applied;
        } else {
            const Vec2 normal=physics::unit(best.normal);
            incident=narrow(std::fabs(double(next.velocity.x)*normal.x+double(next.velocity.y)*normal.y));
            next.velocity=physics::contact_velocity(next.velocity,best.normal,colliders[best_index].restitution);
        }
        const Vec2 normal=physics::unit(best.normal);
        const double offset=double(next.radius)+config_.separation_slop;
        next.position=Vec2(narrow(best.point.x+double(normal.x)*offset),
                           narrow(best.point.y+double(normal.y)*offset));
        sync_height(next);
        result.simulated_seconds+=travel;remaining-=travel;
        const MotionContact contact={best_index,result.simulated_seconds,best,best_moving,surface_velocity,boosted,false,incident,activated,false,incoming_speed};
        result.contacts.push_back(contact);
        if(best_moving && effective[best_index].reference_response_enabled() && !yielded[best_index]) {
            yielded[best_index]=true;
            const FlipperYield stop={best_index,result.simulated_seconds};
            result.flipper_yields.push_back(stop);
            effective[best_index]=effective[best_index].frozen_at(result.simulated_seconds);
        }
    }
    ball=next;return result;
}
PlungerConfig::PlungerConfig(Tick pull, Tick returning, Vec2 direction,
                             float minimum, float maximum, float threshold)
    : pull_duration(pull), return_duration(returning), launch_direction(direction),
      minimum_speed(minimum), maximum_speed(maximum), minimum_pull(threshold) {
    validate_plunger(*this);
}
LaunchEvent::LaunchEvent() : fired(false), pull(0), delta_velocity() {}
TimedPlunger::TimedPlunger(const PlungerConfig& config, Tick now)
    : config_(config), last_time_(now), phase_start_(now), phase_pull_(0),
      pull_(0), held_(false), phase_(PlungerPhase::Idle) {
    validate_plunger(config_);
    require_time(now);
    config_.launch_direction = physics::unit(config_.launch_direction);
}
void TimedPlunger::update(Tick now) {
    require_time(now);
    if (now < last_time_) throw std::invalid_argument("plunger clock moved backward");
    // Both operands are nonnegative and ordered: this subtraction cannot
    // overflow even at INT64_MAX. No deadline is formed by addition.
    const Tick elapsed = now - phase_start_;
    if (phase_ == PlungerPhase::Pulling) {
        pull_ = std::min(1.0, phase_pull_ + double(elapsed) / double(config_.pull_duration));
    } else if (phase_ == PlungerPhase::Returning) {
        pull_ = std::max(0.0, phase_pull_ - double(elapsed) / double(config_.return_duration));
        if (pull_ == 0) {
            phase_ = PlungerPhase::Idle;
            phase_start_ = now;
            phase_pull_ = 0;
        }
    }
    last_time_ = now;
}
LaunchEvent TimedPlunger::set_pressed(bool pressed, Tick now, bool ball_seated,
                                      runtime::TransitionCause cause) {
    update(now);
    LaunchEvent event;
    if (cause != runtime::TransitionCause::Physical) {
        cancel(now);
        return event;
    }
    if (pressed == held_) return event;
    held_ = pressed;
    if (pressed) {
        phase_start_ = now;
        phase_pull_ = pull_;
        phase_ = PlungerPhase::Pulling;
        return event;
    }
    const double speed = config_.minimum_speed +
        (double(config_.maximum_speed) - config_.minimum_speed) * pull_;
    if (ball_seated && pull_ >= config_.minimum_pull && speed > 0) {
        event.fired = true;
        event.pull = narrow(pull_);
        event.delta_velocity = Vec2(narrow(double(config_.launch_direction.x) * speed),
                                    narrow(double(config_.launch_direction.y) * speed));
    }
    phase_start_ = now;
    phase_pull_ = pull_;
    phase_ = pull_ > 0 ? PlungerPhase::Returning : PlungerPhase::Idle;
    return event;
}
void TimedPlunger::cancel(Tick now) {
    update(now);
    phase_start_ = now;
    phase_pull_ = 0;
    pull_ = 0;
    held_ = false;
    phase_ = PlungerPhase::Idle;
}
void TimedPlunger::reset(Tick now) {
    require_time(now);
    last_time_ = now;
    phase_start_ = now;
    phase_pull_ = 0;
    pull_ = 0;
    held_ = false;
    phase_ = PlungerPhase::Idle;
}
float TimedPlunger::pull() const { return static_cast<float>(pull_); }
bool TimedPlunger::held() const { return held_; }
PlungerPhase TimedPlunger::phase() const { return phase_; }
Tick TimedPlunger::time() const { return last_time_; }

} // namespace motion
} // namespace pinball
