#include "collision2d.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pinball::physics;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void close(float actual, float expected, const char* message, float tolerance=0.00001f) {
    check(std::isfinite(actual) && std::fabs(actual-expected)<=tolerance, message);
}
void invalid(void (*operation)(), const char* message) {
    try { operation(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}
void negative_radius() { sweep_circle_circle(Vec2(),Vec2(),-1,Vec2(),0,1); }
void invalid_time() { sweep_circle_segment(Vec2(),Vec2(),1,Vec2(),Vec2(1,0),-1); }
void invalid_normal() { contact_velocity(Vec2(1,0),Vec2(),1); }
void invalid_restitution() { contact_velocity(Vec2(1,0),Vec2(1,0),1.01f); }
void not_finite() { sweep_circle_circle(Vec2(std::numeric_limits<float>::infinity(),0),Vec2(),1,Vec2(),1,1); }
void overflow(void (*operation)(), const char* message) {
    try { operation(); } catch (const std::overflow_error&) { return; }
    throw std::runtime_error(message);
}
void overflowing_penetration() {
    const float limit=std::numeric_limits<float>::max();
    sweep_circle_circle(Vec2(),Vec2(),limit,Vec2(),limit,0);
}
void overflowing_contact_point() {
    const float limit=std::numeric_limits<float>::max();
    sweep_circle_circle(Vec2(limit,0),Vec2(),1,Vec2(limit,0),limit,0);
}
void overflowing_response() {
    const float limit=std::numeric_limits<float>::max();
    contact_velocity(Vec2(-limit,-limit),Vec2(1,0.5f),1);
}
void overflowing_sum() { add(Vec2(std::numeric_limits<float>::max(),0),Vec2(std::numeric_limits<float>::max(),0)); }
void overflowing_difference() { subtract(Vec2(std::numeric_limits<float>::max(),0),Vec2(-std::numeric_limits<float>::max(),0)); }
void overflowing_product() { multiply(Vec2(std::numeric_limits<float>::max(),0),2); }
void overflowing_dot() { dot(Vec2(std::numeric_limits<float>::max(),0),Vec2(2,0)); }
void overflowing_length() { const float limit=std::numeric_limits<float>::max(); length(Vec2(limit,limit)); }
void finite_extremes() {
    const float limit=std::numeric_limits<float>::max();
    const Vec2 reflected=contact_velocity(Vec2(-limit,0),Vec2(1,0),1);
    check(std::isfinite(reflected.x) && reflected.x==limit && reflected.y==0,
        "finite elastic reflection overflowed its intermediate correction");
    const Vec2 direction=unit(Vec2(limit,limit));
    close(length(direction),1,"large finite normal could not be normalized");
    SweepHit hit=sweep_circle_circle(Vec2(limit,0),Vec2(-limit,0),1,Vec2(-limit,0),0,3);
    check(hit.hit && !hit.initial_overlap,"finite relative-coordinate overflow lost a valid collision");
    close(hit.time,2,"large relative-coordinate sweep time");
    check(hit.point.x==-limit && hit.normal.x==1 && hit.normal.y==0,
        "large relative-coordinate contact was not finite and stable");
    check(!sweep_circle_circle(Vec2(limit,0),Vec2(-limit,0),1,Vec2(-limit,0),0,1).hit,
        "large relative-coordinate hit escaped its time horizon");
    hit=sweep_circle_segment(Vec2(0,2),Vec2(0,-1),1,Vec2(-limit,0),Vec2(limit,0),2);
    check(hit.hit,"finite segment-axis overflow lost the face");
    close(hit.time,1,"large segment face time"); close(hit.point.x,0,"large segment face point");
    close(hit.normal.y,1,"large segment face normal");
    hit=sweep_circle_segment(Vec2(0,1),Vec2(0,-1),0.1f,Vec2(-limit,-limit),Vec2(limit,limit),2);
    check(hit.hit,"large diagonal segment lost a small perpendicular separation");
    close(hit.time,0.85857864f,"large diagonal face entry time");
    check(!sweep_circle_circle(Vec2(0,1),Vec2(-limit,-limit),0.1f,Vec2(-limit,-limit),0,2).hit,
        "large diagonal circle sweep lost a small perpendicular miss distance");
    check(!sweep_circle_circle(Vec2(-100000000.0f,1.0001f),Vec2(1,0),1,Vec2(),0,100000000.0f).hit,
        "distant near-miss became a false tangent through cancellation");
    hit=sweep_circle_circle(Vec2(-100000000.0f,1),Vec2(1,0),1,Vec2(),0,100000000.0f);
    check(hit.hit,"distant true tangent was rejected");
    close(hit.time,100000000.0f,"distant tangent time");close(hit.normal.y,1,"distant tangent normal");
    overflow(overflowing_penetration,"unrepresentable penetration did not throw");
    overflow(overflowing_contact_point,"unrepresentable contact point did not throw");
    overflow(overflowing_response,"unrepresentable response did not throw");
    overflow(overflowing_sum,"unrepresentable vector sum did not throw");
    overflow(overflowing_difference,"unrepresentable vector difference did not throw");
    overflow(overflowing_product,"unrepresentable scaled vector did not throw");
    overflow(overflowing_dot,"unrepresentable dot product did not throw");
    overflow(overflowing_length,"unrepresentable length did not throw");
}
void circle_contacts() {
    SweepHit hit=sweep_circle_circle(Vec2(-5,0),Vec2(10,0),1,Vec2(),1,1);
    check(hit.hit && !hit.initial_overlap,"circle entry missing");
    close(hit.time,0.3f,"circle entry time"); close(hit.point.x,-1,"circle contact position");
    close(hit.normal.x,-1,"circle contact normal");
    check(!sweep_circle_circle(Vec2(-5,0),Vec2(10,0),1,Vec2(),1,0.2f).hit,"hit beyond time horizon");
    check(!sweep_circle_circle(Vec2(-5,0),Vec2(-10,0),1,Vec2(),1,1).hit,"receding circle collided");
    hit=sweep_circle_circle(Vec2(-5,2),Vec2(10,0),1,Vec2(),1,1);
    check(hit.hit,"grazing contact missing"); close(hit.time,0.5f,"grazing time"); close(hit.normal.y,1,"grazing normal");
    check(!sweep_circle_circle(Vec2(-5,2.01f),Vec2(10,0),1,Vec2(),1,1).hit,"near miss collided");
    hit=sweep_circle_circle(Vec2(0,0),Vec2(0,0),1,Vec2(),1,0);
    check(hit.hit && hit.initial_overlap,"stationary penetration missing"); close(hit.penetration,2,"penetration depth");
    close(length(hit.normal),1,"coincident normal invalid");
    check(!sweep_circle_circle(Vec2(-2,0),Vec2(-1,0),1,Vec2(),1,1).hit,"outgoing touching circle blocked");
    check(!sweep_circle_circle(Vec2(-2,0),Vec2(),1,Vec2(),1,1).hit,"resting touch repeatedly hit");
    check(sweep_circle_circle(Vec2(-2,0),Vec2(1,0),1,Vec2(),1,0).hit,"incoming touch missing");
    hit=sweep_circle_circle(Vec2(-1000,0),Vec2(200000,0),0.1f,Vec2(),0.1f,0.01f);
    check(hit.hit,"fast circle tunneled"); close(hit.time,0.004999f,"fast circle entry time",0.0000001f);
}
void segment_contacts() {
    const Vec2 a(-2,0),b(2,0);
    SweepHit hit=sweep_circle_segment(Vec2(0,5),Vec2(0,-10),1,a,b,1);
    check(hit.hit,"segment face entry missing"); close(hit.time,0.4f,"segment face time");
    close(hit.point.y,0,"segment contact point"); close(hit.normal.y,1,"segment normal");
    hit=sweep_circle_segment(Vec2(0,-5),Vec2(0,10),1,a,b,1);
    check(hit.hit,"segment reverse face missing"); close(hit.normal.y,-1,"reverse face normal");
    check(!sweep_circle_segment(Vec2(0,-5),Vec2(0,10),1,a,b,1,false).hit,"one-sided wall blocked its back");
    hit=sweep_circle_segment(Vec2(3,5),Vec2(0,-10),1,a,b,1);
    check(hit.hit,"rounded endpoint contact missing"); close(hit.time,0.5f,"rounded endpoint time");
    close(hit.point.x,2,"rounded endpoint point");
    check(!sweep_circle_segment(Vec2(3.01f,5),Vec2(0,-10),1,a,b,1).hit,"past endpoint collided");
    hit=sweep_circle_segment(Vec2(0,0.5f),Vec2(),1,a,b,0);
    check(hit.hit && hit.initial_overlap,"segment penetration missing"); close(hit.penetration,0.5f,"segment penetration depth");
    hit=sweep_circle_segment(Vec2(-5,0),Vec2(10,0),1,Vec2(),Vec2(),1);
    check(hit.hit,"degenerate endpoint missing"); close(hit.time,0.4f,"degenerate endpoint time");
    const SweepHit translated=sweep_circle_segment(Vec2(100,-2),Vec2(0,-10),1,Vec2(98,-7),Vec2(102,-7),1);
    close(translated.time,0.4f,"translation changed contact time"); close(translated.point.x,100,"translated contact x");
    const SweepHit reversed=sweep_circle_segment(Vec2(0,5),Vec2(0,-10),1,b,a,1);
    close(reversed.time,0.4f,"two-sided endpoint order changed contact");
    check(!sweep_circle_segment(Vec2(0,2),Vec2(10,0),1,a,b,1).hit,"parallel near miss collided");
    hit=sweep_circle_segment(Vec2(0,5),Vec2(0,-10),1,a,b,1,false);
    check(hit.hit,"one-sided front face rejected incoming disk");close(hit.time,0.4f,"one-sided front time");
    check(!sweep_circle_segment(Vec2(0,5),Vec2(0,-10),1,b,a,1,false).hit,
        "reversing one-sided endpoints did not reverse its front");
    check(!sweep_circle_segment(Vec2(0,-0.5f),Vec2(),1,a,b,0,false).hit,
        "one-sided back penetration was reported as front contact");
    hit=sweep_circle_segment(Vec2(2.6f,2),Vec2(0,-1),1,a,b,3,false);
    check(hit.hit,"one-sided rounded front endpoint was missed");close(hit.time,1.2f,"rounded front endpoint time");
    close(hit.normal.x,0.6f,"rounded front endpoint normal x");close(hit.normal.y,0.8f,"rounded front endpoint normal y");
    check(!sweep_circle_segment(Vec2(2.6f,-2),Vec2(0,1),1,a,b,3,false).hit,
        "one-sided rounded endpoint blocked a back-facing normal");
    check(!sweep_circle_segment(Vec2(3,0),Vec2(1,0),1,a,b,1).hit,
        "outgoing endpoint touch was blocked");
    check(!sweep_circle_segment(Vec2(3,0),Vec2(0,1),1,a,b,1).hit,
        "already touching endpoint generated a tangent repeat");
    hit=sweep_circle_segment(Vec2(-5,0),Vec2(10,0),1,Vec2(),Vec2(),1,false);
    check(hit.hit,"one-sided zero-length segment lost endpoint behavior");
}
void response_invariants() {
    Vec2 result=contact_velocity(Vec2(3,-4),Vec2(0,2),1);
    close(result.x,3,"response altered tangent");close(result.y,4,"elastic normal reflection");
    result=contact_velocity(Vec2(3,-4),Vec2(0,1),0);
    close(result.x,3,"inelastic tangent");close(result.y,0,"inelastic normal not stopped");
    result=contact_velocity(Vec2(3,4),Vec2(0,1),1);
    close(result.y,4,"response changed separating velocity");
    for(int angle=0;angle<24;++angle) {
        const float theta=float(angle)*0.261799388f;
        const Vec2 normal(std::cos(theta),std::sin(theta));
        for(int x=-3;x<=3;++x) for(int y=-3;y<=3;++y) {
            const Vec2 incoming{static_cast<float>(x),static_cast<float>(y)};
            const Vec2 outgoing=contact_velocity(incoming,normal,1);
            close(dot(outgoing,outgoing),dot(incoming,incoming),"elastic energy changed",0.0001f);
            check(dot(outgoing,normal)>=-0.00001f,"resolved velocity still incoming");
        }
    }
}
}
int main() {
    try {
        circle_contacts(); segment_contacts(); response_invariants(); finite_extremes();
        invalid(negative_radius,"negative radius accepted"); invalid(invalid_time,"negative horizon accepted");
        invalid(invalid_normal,"zero normal accepted"); invalid(invalid_restitution,"energy-adding restitution accepted");
        invalid(not_finite,"nonfinite geometry accepted");
        std::cout<<"collision geometry checks passed; reference-game equivalence is not asserted\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

