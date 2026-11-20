#include "reference_table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>

namespace pinball {
namespace reference {
namespace {
const float default_threshold = 8.9999999e10f;
float narrow(double value) {
    if (!std::isfinite(value) || std::fabs(value) > (std::numeric_limits<float>::max)())
        throw std::overflow_error("table profile result exceeds float range");
    return static_cast<float>(value);
}
void finite_record(const std::vector<float>& values) {
    for (std::size_t i=0;i<values.size();++i)
        if (!std::isfinite(values[i])) throw table::ModelError("nonfinite table physics attribute");
}
const std::vector<float>& record(const table::GroupInventory& group,float tag,std::size_t size=0) {
    const std::vector<float>* found=0;
    for (std::size_t i=0;i<group.float_records.size();++i) {
        const std::vector<float>& values=group.float_records[i].values;
        if (!values.empty() && values[0]==tag) {
            if (found) throw table::ModelError("duplicate table physics attribute");
            found=&values;
        }
    }
    if (!found || (size && found->size()!=size))
        throw table::ModelError("missing or malformed table physics attribute");
    finite_record(*found);
    return *found;
}
float optional_scalar(const table::GroupInventory& group,float tag,float fallback) {
    for (std::size_t i=0;i<group.float_records.size();++i) {
        const std::vector<float>& values=group.float_records[i].values;
        if (!values.empty() && values[0]==tag) return record(group,tag,2)[1];
    }
    return fallback;
}
std::uint32_t bit_mask(float value) {
    if (!std::isfinite(value) || value<0 || value>=32 || std::floor(value)!=value)
        throw table::ModelError("invalid ramp collision bit position");
    return std::uint32_t(1)<<static_cast<unsigned>(value);
}
std::size_t link(const table::TableModel& model,const table::GroupInventory& group,int key) {
    std::size_t found=model.groups().size();
    for (std::size_t r=0;r<group.short_records.size();++r) {
        const table::ShortRecord& rec=group.short_records[r];
        if (rec.field_type!=10) continue;
        if (rec.values.size()%2) throw table::ModelError("visual attributes must be pairs");
        for (std::size_t i=0;i<rec.values.size();i+=2) {
            if (rec.values[i]!=key) continue;
            const int index=rec.values[i+1];
            if (found!=model.groups().size() || index<0 ||
                static_cast<std::size_t>(index)>=model.groups().size())
                throw table::ModelError("duplicate or invalid material/kicker reference");
            found=static_cast<std::size_t>(index);
        }
    }
    return found;
}
void marker(const table::GroupInventory& group,int expected) {
    if (!group.has_record_marker || group.record_marker!=expected)
        throw table::ModelError("material/kicker resource has wrong marker");
}
void material_values(const table::GroupInventory& group,float& smoothness,float& elasticity) {
    marker(group,300);
    bool has_s=false,has_e=false;
    if (group.float_records.empty()) throw table::ModelError("empty material resource");
    for (std::size_t r=0;r<group.float_records.size();++r) {
        const std::vector<float>& values=group.float_records[r].values;
        finite_record(values);
        if (values.size()%2) throw table::ModelError("malformed material attributes");
        for (std::size_t i=0;i<values.size();i+=2) {
            if (values[i]==301) {
                if (has_s) throw table::ModelError("duplicate material smoothness");
                smoothness=values[i+1];has_s=true;
            } else if (values[i]==302) {
                if (has_e) throw table::ModelError("duplicate material elasticity");
                elasticity=values[i+1];has_e=true;
            } else if (values[i]!=304) {
                throw table::ModelError("unsupported material attribute");
            }
            // 304 is a sound reference, retained in TableModel but unused.
        }
    }
}
void kicker_values(const table::GroupInventory& group,float& threshold,float& boost) {
    marker(group,400);
    bool has_t=false,has_b=false;
    if (group.float_records.empty()) throw table::ModelError("empty kicker resource");
    for (std::size_t r=0;r<group.float_records.size();++r) {
        const std::vector<float>& values=group.float_records[r].values;
        finite_record(values);
        for (std::size_t i=0;i<values.size();) {
            const float key=values[i];
            std::size_t width=2;
            if (key==404) width=4;
            else if (key!=401 && key!=402 && key!=403 && key!=405 && key!=406)
                throw table::ModelError("unsupported kicker attribute");
            if (values.size()-i<width) throw table::ModelError("truncated kicker attribute");
            if (key==401) {
                if (has_t) throw table::ModelError("duplicate kicker threshold");
                threshold=values[i+1];has_t=true;
            } else if (key==402) {
                if (has_b) throw table::ModelError("duplicate kicker boost");
                boost=values[i+1];has_b=true;
            }
            // Throw direction/multipliers and sound are not static response.
            i+=width;
        }
    }
}
float radius(const table::GroupInventory& group) {
    const float value=record(group,500,2)[1];
    if (value<=0) throw table::ModelError("nonpositive resource ball radius");
    return value;
}
physics::Vec2 feed(const table::GroupInventory& group) {
    const std::vector<float>& values=record(group,601,5);
    // Source TPlunger consumes only the first two values after601.
    return physics::Vec2(values[1],values[2]);
}
const std::vector<float>& striker(const table::GroupInventory& group) {
    const std::vector<float>& v=record(group,600,7);
    if (v[1]!=2 || std::fabs(double(v[3])-v[5])>.00001 || v[2]==v[4])
        throw table::ModelError("profile requires a horizontal two-point plunger striker");
    return v;
}
physics::Vec2 seat(const table::GroupInventory& group,physics::Vec2 origin,float ball_radius) {
    const std::vector<float>& v=striker(group);
    const double lo=std::min(double(v[2]),double(v[4]));
    const double hi=std::max(double(v[2]),double(v[4]));
    if (origin.x<lo || origin.x>hi) throw table::ModelError("feed lies outside plunger striker");
    return physics::Vec2(origin.x,narrow(double(v[3])-ball_radius));
}
response::FieldConfig field(const table::GroupInventory& group,float ball_radius) {
    const std::vector<float>& g=record(group,305,4);
    const float drag=record(group,701,2)[1];
    if (g[1]<0) throw table::ModelError("negative gravity magnitude");
    const double projected=double(g[1])*std::sin(double(g[2]));
    const physics::Vec2 gravity(narrow(projected*std::cos(double(g[3]))),
                                narrow(projected*std::sin(double(g[3]))));
    // RandFloat-.5 maps to [-1,1] samples with half the damping coefficient.
    return response::FieldConfig(gravity,drag,narrow(double(drag)*.5),
                                 narrow(double(ball_radius)*200));
}
runtime::Tick duration(float seconds) {
    if (seconds<=0) throw table::ModelError("nonpositive flipper duration");
    const double exact=double(seconds)*runtime::ticks_per_second;
    const double high_bit=9223372036854775808.0;
    if (!std::isfinite(exact) || exact>=high_bit)
        throw std::overflow_error("flipper duration exceeds tick range");
    // Preserve source float precision when changing seconds into our ns clock:
    // stored.0399999991/.0799999982 become40/80ms, not39,999,999/79,999,998ns.
    const double ticks=std::floor(double(static_cast<float>(exact))+.5);
    if (ticks>=high_bit) throw std::overflow_error("rounded flipper duration exceeds tick range");
    if (ticks<1) throw table::ModelError("flipper duration rounds to zero ticks");
    return static_cast<runtime::Tick>(ticks);
}
flipper::FlipperConfig flip(const table::TableModel& model,std::size_t index) {
    const table::GroupInventory& group=model.group(index);
    const std::vector<float>& p=record(group,800,5);
    const std::vector<float>& r=record(group,801,5);
    const std::vector<float>& u=record(group,802,5);
    if (std::fabs(double(r[3])-u[3])>.000001)
        throw table::ModelError("flipper poses have different tip radii");
    const physics::Vec2 pivot(p[1],p[2]);
    const physics::Vec2 rest(narrow(double(r[1])-p[1]),narrow(double(r[2])-p[2]));
    const physics::Vec2 raised(narrow(double(u[1])-p[1]),narrow(double(u[2])-p[2]));
    const response::Material material=material_for_group(model,index);
    return flipper::FlipperConfig(pivot,rest,raised,p[3],r[3],
        duration(record(group,804,2)[1]),duration(record(group,805,2)[1]),
        material.elasticity,material.smoothness,record(group,803,2)[1],
        flipper::ResponseMode::ReferenceMaterial);
}
struct Ejection {
    physics::Vec2 direction;
    float angle, speed, variation;
};
Ejection ejection(const table::TableModel& model,std::size_t group) {
    const std::size_t kicker=link(model,model.group(group),400);
    if (kicker==model.groups().size()) throw table::ModelError("capture component has no kicker resource");
    const table::GroupInventory& properties=model.group(kicker);
    float threshold=default_threshold,boost=0;
    kicker_values(properties,threshold,boost);
    Ejection result={physics::Vec2(),0,boost,0};
    bool direction=false,angle=false,variation=false;
    for (std::size_t r=0;r<properties.float_records.size();++r) {
        const std::vector<float>& v=properties.float_records[r].values;
        for (std::size_t i=0;i<v.size();) {
            if (v[i]==403) {
                if (variation) throw table::ModelError("duplicate ejection speed variation");
                result.variation=narrow(double(v[i+1])*.01);variation=true;
            } else if (v[i]==404) {
                if (direction) throw table::ModelError("duplicate ejection direction");
                if (v[i+3]!=0) throw table::ModelError("nonplanar ejection unsupported");
                result.direction=physics::Vec2(v[i+1],v[i+2]);direction=true;
            } else if (v[i]==405) {
                if (angle) throw table::ModelError("duplicate ejection angle multiplier");
                result.angle=v[i+1];angle=true;
            }
            i+=v[i]==404?4:2;
        }
    }
    if (!direction || !angle || !variation || result.speed<=0 ||
        result.variation<0 || result.variation>1 ||
        (result.direction.x==0 && result.direction.y==0))
        throw table::ModelError("invalid ejection parameters");
    const double maximum=double(result.speed)*(1+double(result.variation));
    narrow(double(result.direction.x)*maximum);narrow(double(result.direction.y)*maximum);
    return result;
}
void registered(const table::TableModel& model,std::size_t group,int type) {
    for (std::size_t i=0;i<model.objects().size();++i)
        if (model.objects()[i].group_index==group && model.objects()[i].type==type) return;
    throw table::ModelError("named capture component has the wrong object type");
}
SinkProfile sink(const table::TableModel& model,std::size_t group,std::size_t collider_index) {
    registered(model,group,1017);
    const table::GroupInventory& resource=model.group(group);
    const std::vector<float>& point=record(resource,601,5);
    const Ejection launch=ejection(model,group);
    const SinkProfile result={group,collider_index,physics::Vec2(point[1],point[2]),
        launch.direction,launch.angle,launch.speed,launch.variation,duration(record(resource,407,2)[1])};
    return result;
}
KickoutProfile kickout(const table::TableModel& model,std::size_t group,std::size_t collider_index,bool gravity_well=false) {
    registered(model,group,gravity_well?1029:1012);
    if (!(collision_mask_for_group(model,group)&1u))
        throw table::ModelError("default black hole is not on the ground layer");
    const table::GroupInventory& resource=model.group(group);
    const std::vector<float>& shape=record(resource,600);
    if ((shape.size()!=6 && !(gravity_well && shape.size()==5)) || shape[1]!=1 || shape[4]<=0)
        throw table::ModelError("kickout requires a positive circular field");
    const float scale=record(resource,306,2)[1];
    const float strength=record(resource,305,2)[1];
    if (scale<0 || strength<0) throw table::ModelError("invalid kickout field/trigger parameters");
    float trigger=narrow(double(scale)*shape[4]);
    if (trigger==0) trigger=.001f; // Explicit source fallback, not ball-radius expansion.
    const Ejection launch=ejection(model,group);
    const KickoutProfile result={group,collider_index,physics::Vec2(shape[2],shape[3]),launch.direction,
        shape[4],trigger,strength,launch.angle,launch.speed,launch.variation,record(resource,408,4)[3],
        duration(1.5f),duration(.05f)};
    return result;
}
void append(TablePhysics& profile,motion::StaticCollider collider,
            const response::Material& material,std::size_t group,std::uint32_t mask) {
    collider.reference_response=true;
    collider.collision_mask=mask;
    collider.material=material;
    profile.static_colliders.push_back(collider);
    profile.collider_groups.push_back(group);
}
struct Connector {
    std::size_t plane, edge;
    std::uint32_t external_mask;
    bool entry_enabled;
    float z_offset;
};
double distance(physics::Vec2 a,physics::Vec2 b) {
    const double x=double(a.x)-b.x,y=double(a.y)-b.y;
    return std::sqrt(x*x+y*y);
}
Connector connector(const table::GroupInventory& group,float tag,
                    const std::vector<RampPlaneProfile>& planes) {
    const std::vector<float>& v=record(group,tag,9);
    if (v[3]!=2 || (v[2]!=0 && v[2]!=1)) throw table::ModelError("malformed ramp connector");
    const std::uint32_t external=bit_mask(v[1]);
    const physics::Vec2 a(v[4],v[5]),b(v[6],v[7]);
    std::size_t best_plane=0,best_edge=0;
    double best=(std::numeric_limits<double>::max)();
    // Attribute endpoints identify the closest directed mesh edge; retain
    // mesh coordinates for the event so adjacent surfaces share exact points.
    for (std::size_t p=0;p<planes.size();++p)
        for (std::size_t e=0;e<3;++e) {
            const double error=distance(a,planes[p].vertices[e])+distance(b,planes[p].vertices[(e+1)%3]);
            if (error<best) { best=error;best_plane=p;best_edge=e; }
        }
    const Connector result={best_plane,best_edge,external,v[2]!=0,v[8]};
    return result;
}
void transition(TablePhysics& profile,std::size_t group,physics::Vec2 a,physics::Vec2 b,
                std::uint32_t mask,const motion::SurfaceTransition& target) {
    const std::size_t index=profile.static_colliders.size();
    profile.static_colliders.push_back(motion::StaticCollider::transition_segment(b,a,mask,target));
    profile.collider_groups.push_back(group);
    profile.ramp_transition_indices.push_back(index);
}
void ramp(TablePhysics& profile,const table::TableModel& model,std::size_t group,float magnitude) {
    const table::GroupInventory& resource=model.group(group);
    const std::vector<float>& v=record(resource,1300);
    if (v.size()<2 || v[1]<1 || v[1]>65535 || std::floor(v[1])!=v[1])
        throw table::ModelError("invalid ramp plane count");
    const std::size_t count=static_cast<std::size_t>(v[1]);
    if (v.size()!=2+13*count) throw table::ModelError("malformed ramp plane stride");
    const float drag=optional_scalar(resource,701,.2f);
    const float reset=optional_scalar(resource,1305,0);
    if (drag<0 || (reset!=0 && reset!=1)) throw table::ModelError("invalid ramp drag/reset flag");
    const std::uint32_t layer=collision_mask_for_group(model,group);
    std::vector<RampPlaneProfile> planes;
    for (std::size_t p=0;p<count;++p) {
        const std::size_t at=2+13*p;
        RampPlaneProfile plane;
        plane.group=group;plane.local_index=p;plane.id=profile.ramp_planes.size()+p;
        for (std::size_t i=0;i<3;++i) plane.vertices[i]=physics::Vec2(v[at+3+2*i],v[at+4+2*i]);
        const double x1=double(plane.vertices[1].x)-plane.vertices[0].x;
        const double y1=double(plane.vertices[1].y)-plane.vertices[0].y;
        const double x2=double(plane.vertices[2].x)-plane.vertices[0].x;
        const double y2=double(plane.vertices[2].y)-plane.vertices[0].y;
        if (x1*y2-y1*x2==0) throw table::ModelError("degenerate ramp triangle");
        plane.surface.active=true;
        plane.surface.gradient=physics::Vec2(v[at],v[at+1]);plane.surface.offset=v[at+2];
        const double strength=double(magnitude)*std::sin(double(v[at+9]));
        plane.surface.field_force=physics::Vec2(narrow(strength*std::cos(double(v[at+10]))),
                                              narrow(strength*std::sin(double(v[at+10]))));
        plane.surface.drag=drag;plane.surface.id=plane.id;
        for (std::size_t i=0;i<3;++i)
            narrow(double(plane.surface.gradient.x)*plane.vertices[i].x+
                double(plane.surface.gradient.y)*plane.vertices[i].y+plane.surface.offset+profile.ball_radius);
        planes.push_back(plane);
    }
    const Connector ports[]={connector(resource,1301,planes),connector(resource,1302,planes)};
    if (ports[0].plane==ports[1].plane && ports[0].edge==ports[1].edge)
        throw table::ModelError("ramp connectors select the same edge");
    // 1303 is a score/sound-only event with no physical layer/height effect.
    // Validate its known shape but leave dispatch to the future rules system.
    const std::vector<float>& score=record(resource,1303,8);
    bit_mask(score[1]);
    if (score[2]!=2) throw table::ModelError("malformed ramp score line");
    profile.ramp_planes.insert(profile.ramp_planes.end(),planes.begin(),planes.end());
    for (std::size_t p=0;p<count;++p)
        for (std::size_t e=0;e<3;++e) {
            std::uint32_t listen=layer;bool enabled=true;
            for (std::size_t c=0;c<2;++c)
                if (ports[c].plane==p && ports[c].edge==e) {
                    listen=ports[c].external_mask;enabled=ports[c].entry_enabled;
                }
            if (!enabled) continue;
            motion::SurfaceTransition target;
            target.target_mask=layer;target.plane=planes[p].surface;
            transition(profile,group,planes[p].vertices[e],planes[p].vertices[(e+1)%3],listen,target);
        }
    // Opposite directed exit events always exist, even where external entry
    // is disabled. Inactive-plane exits keep Z unless 1305 requests a reset.
    for (std::size_t c=0;c<2;++c) {
        const Connector& port=ports[c];const RampPlaneProfile& plane=planes[port.plane];
        motion::SurfaceTransition target;target.target_mask=port.external_mask;
        target.set_height=reset!=0;target.height_offset=port.z_offset;
        transition(profile,group,plane.vertices[(port.edge+1)%3],plane.vertices[port.edge],layer,target);
    }
}
HoleProfile hole(const table::TableModel& model,std::size_t group,std::size_t index,float magnitude) {
    registered(model,group,1022);
    const table::GroupInventory& resource=model.group(group);
    const std::vector<float>& shape=record(resource,600,6);
    const float scale=record(resource,306,2)[1],pull=record(resource,305,2)[1];
    const float drag=optional_scalar(resource,701,.2f);
    if (shape[1]!=1 || shape[4]<=0 || scale<0 || pull<0 || drag<0)
        throw table::ModelError("invalid ramp hole field/trigger");
    float trigger=narrow(double(scale)*shape[4]);if (trigger==0) trigger=.001f;
    const std::vector<float>& landing=record(resource,408);
    const double mask_value=record(resource,1304,2)[1];
    if (landing.size()<4 || mask_value<1 || mask_value>4294967295.0 || std::floor(mask_value)!=mask_value)
        throw table::ModelError("invalid hole landing height/mask");
    const HoleProfile result={group,index,physics::Vec2(shape[2],shape[3]),shape[4],trigger,pull,
        collision_mask_for_group(model,group),static_cast<std::uint32_t>(mask_value),landing[3],
        narrow(double(magnitude)*drag),duration(.5f)};
    return result;}
void hit_components(TablePhysics& profile,const table::TableModel& model) {
    // Source-spec default level is zero. Extra paired bitmap levels are
    // retained by TableModel but require later rule progression.
    std::set<std::size_t> loaded;
    for (std::size_t i=0;i<model.objects().size();++i) {
        const table::ObjectEntry& object=model.objects()[i];
        if (object.type!=1005 || !loaded.insert(object.group_index).second) continue;
        const table::GroupInventory& group=model.group(object.group_index);
        const std::vector<float>& shape=record(group,600,6);
        if (shape[1]!=1 || shape[4]<=0)
            throw table::ModelError("bumper requires positive circular body geometry");
        const std::size_t images=model.image_states(object.group_index).size();
        if (images<2 || images%2)
            throw table::ModelError("bumper requires paired idle/fired images");
        const response::Material material=material_for_group(model,object.group_index);
        ComponentProfile component={object.group_index,std::vector<std::size_t>(),
            HitComponentKind::Bumper,0,1,duration(record(group,407,2)[1]),material};
        component.collider_indices.push_back(profile.static_colliders.size());
        motion::StaticCollider collider=motion::StaticCollider::disk(
            physics::Vec2(shape[2],shape[3]),shape[4],material.elasticity);
        collider.latch_response=true;
        append(profile,collider,material,object.group_index,collision_mask_for_group(model,object.group_index));
        profile.components.push_back(component);
    }
    // These four source TWall objects already supplied their ordinary masked
    // geometry. Associate their existing primitives instead of duplicating it.
    const char* names[]={"v_rebo1","v_rebo2","v_rebo3","v_rebo4"};
    for (std::size_t i=0;i<4;++i) {
        std::size_t group=0;
        if (!model.find_named_group(names[i],group)) continue;
        registered(model,group,1000);
        if (model.image_states(group).empty())
            throw table::ModelError("rebound requires a fired image");
        const response::Material material=material_for_group(model,group);
        ComponentProfile component={group,std::vector<std::size_t>(),
            HitComponentKind::Rebound,-1,0,duration(.1f),material};
        for (std::size_t c=0;c<profile.collider_groups.size();++c)
            if (profile.collider_groups[c]==group) component.collider_indices.push_back(c);
        if (component.collider_indices.empty())
            throw table::ModelError("rebound has no ordinary collision geometry");
        profile.components.push_back(component);
    }
}
motion::StaticCollider pass_gate_event(const table::TableModel& model,std::size_t group,float ball_radius) {
    registered(model,group,1016);
    const std::vector<float>& gate=record(model.group(group),600,7);
    if (gate[1]!=2 || (gate[2]==gate[4] && gate[3]==gate[5]))
        throw table::ModelError("rule gate requires a nonzero two-point segment");
    const double dx=double(gate[4])-gate[2],dy=double(gate[5])-gate[3];
    const double length=std::sqrt(dx*dx+dy*dy);
    const double offset=.8*double(ball_radius);
    // Source pass-through edge is B->A with negative .8R offset along its
    // clockwise normal. Our CCW edge is A->B, shifted by +.8R*CW(A->B).
    const double shift_x=offset*dy/length,shift_y=-offset*dx/length;
    return motion::StaticCollider::event_segment(
        physics::Vec2(narrow(double(gate[2])+shift_x),narrow(double(gate[3])+shift_y)),
        physics::Vec2(narrow(double(gate[4])+shift_x),narrow(double(gate[5])+shift_y)),
        collision_mask_for_group(model,group));
}
void rule_metadata(TablePhysics& profile,const table::TableModel& model) {
    RuleProfile& rules=profile.rules;
    const char* ground_bumpers[]={"a_bump1","a_bump2","a_bump3","a_bump4"};
    const char* raised_bumpers[]={"a_bump5","a_bump6","a_bump7"};
    for (std::size_t i=0;i<profile.components.size();++i) {
        const ComponentProfile& component=profile.components[i];
        std::uint32_t points=component.kind==HitComponentKind::Rebound?500u:0u;
        const std::string& name=model.group(component.group).name;
        for (std::size_t n=0;n<4;++n) if (name==ground_bumpers[n]) points=500;
        for (std::size_t n=0;n<3;++n) if (name==raised_bumpers[n]) points=1500;
        // Unbound generic bumper owners have no source control score table.
        rules.event_group_awards[component.group]=points;
    }
    for (std::size_t i=0;i<profile.sinks.size();++i)
        rules.event_group_awards[profile.sinks[i].group]=2500;
    for (std::size_t i=0;i<profile.kickouts.size();++i)
        rules.event_group_awards[profile.kickouts[i].group]=20000;

    rules.drain_group=model.require_named_group("drain");
    registered(model,rules.drain_group,1007);
    const table::GroupInventory& drain=model.group(rules.drain_group);
    const std::vector<float>& line=record(drain,600,7);
    if (line[1]!=2 || (line[2]==line[4] && line[3]==line[5]))
        throw table::ModelError("drain requires a nonzero two-point segment");
    rules.drain_delay=duration(record(drain,407,2)[1]);
    rules.drain_collider_index=profile.static_colliders.size();
    const response::Material drain_material=material_for_group(model,rules.drain_group);
    motion::StaticCollider capture=motion::StaticCollider::segment(
        physics::Vec2(line[4],line[5]),physics::Vec2(line[2],line[3]),drain_material.elasticity,false);
    capture.captures_ball=true;
    append(profile,capture,drain_material,rules.drain_group,collision_mask_for_group(model,rules.drain_group));

    rules.skill_gate_group=model.require_named_group("s_onewy1");
    motion::StaticCollider gate_event=pass_gate_event(model,rules.skill_gate_group,profile.ball_radius);
    rules.skill_gate_collider_index=profile.static_colliders.size();
    profile.static_colliders.push_back(gate_event);profile.collider_groups.push_back(rules.skill_gate_group);

    rules.ramp_score_group=model.require_named_group("ramp");
    registered(model,rules.ramp_score_group,1021);
    const std::vector<float>& score=record(model.group(rules.ramp_score_group),1303,8);
    if (score[2]!=2 || (score[3]==score[5] && score[4]==score[6]))
        throw table::ModelError("ramp score requires a nonzero two-point segment");
    // The source reverses these endpoints for its clockwise front; our CCW
    // center event therefore retains the raw order and has no radius offset.
    motion::StaticCollider score_event=motion::StaticCollider::event_segment(
        physics::Vec2(score[3],score[4]),physics::Vec2(score[5],score[6]),bit_mask(score[1]));
    rules.ramp_score_collider_index=profile.static_colliders.size();
    profile.static_colliders.push_back(score_event);profile.collider_groups.push_back(rules.ramp_score_group);
    rules.event_group_awards[rules.ramp_score_group]=5000;
    rules.skill_clear_group=model.require_named_group("s_onewy10");
    motion::StaticCollider clear_event=pass_gate_event(model,rules.skill_clear_group,profile.ball_radius);
    rules.skill_clear_collider_index=profile.static_colliders.size();
    profile.static_colliders.push_back(clear_event);profile.collider_groups.push_back(rules.skill_clear_group);

}
TargetProfile target_body(TablePhysics& profile,const table::TableModel& model,std::size_t group,bool solo=false) {
    registered(model,group,solo?1019:1006);
    const table::GroupInventory& resource=model.group(group);
    const std::vector<float>& shape=record(resource,600,13);
    if (shape[1]!=5 || shape[2]!=shape[10] || shape[3]!=shape[11])
        throw table::ModelError("popup target requires a closed four-edge body");
    if (model.image_states(group).size()!=(solo?2u:1u))
        throw table::ModelError("target has wrong idle/hit image count");
    physics::Vec2 vertices[4];
    for (std::size_t v=0;v<4;++v) vertices[v]=physics::Vec2(shape[2+2*v],shape[3+2*v]);
    double turn=0;
    for (std::size_t v=0;v<4;++v) {
        const physics::Vec2 a=vertices[v],b=vertices[(v+1)%4],c=vertices[(v+2)%4];
        const double abx=double(b.x)-a.x,aby=double(b.y)-a.y;
        const double bcx=double(c.x)-b.x,bcy=double(c.y)-b.y;
        const double cross=abx*bcy-aby*bcx;
        if (cross==0 || (turn!=0 && (cross>0)!=(turn>0)))
            throw table::ModelError("popup target body must be nondegenerate and convex");
        turn=cross;
    }
    const response::Material material=material_for_group(model,group);
    TargetProfile target={group,std::vector<std::size_t>(),material,0,solo?1:-1,
        solo?runtime::Tick(100000000):duration(record(resource,407,2)[1])};
    const std::uint32_t mask=collision_mask_for_group(model,group);
    for (std::size_t v=0;v<4;++v) {
        motion::StaticCollider edge=motion::StaticCollider::segment(
            vertices[(v+1)%4],vertices[v],material.elasticity,false);
        edge.response_owner=group;edge.deactivate_on_activation=true;
        target.collider_indices.push_back(profile.static_colliders.size());
        append(profile,edge,material,group,mask);
    }
    return target;
}
void load_multiplier_bank(TablePhysics& profile,const table::TableModel& model) {
    TargetBankProfile& bank=profile.multiplier_bank;
    const char* names[]={"a_targ7","a_targ8","a_targ9"};
    for (std::size_t i=0;i<3;++i) {
        bank.targets[i]=target_body(profile,model,model.require_named_group(names[i]));
        if (i && bank.targets[i].rearm_delay!=bank.targets[0].rearm_delay)
            throw table::ModelError("multiplier targets have differing rearm delays");
    }
    bank.light_group=model.require_named_group("top_target_lights");
    registered(model,bank.light_group,1026);
    const table::GroupInventory& lights=model.group(bank.light_group);
    const std::vector<std::int16_t>* members=0;
    for (std::size_t r=0;r<lights.short_records.size();++r) {
        const table::ShortRecord& rec=lights.short_records[r];
        if (rec.field_type!=10 || rec.values.empty() || rec.values[0]!=1027) continue;
        if (members) throw table::ModelError("duplicate multiplier light membership");
        members=&rec.values;
    }
    if (!members || members->size()!=5)
        throw table::ModelError("multiplier bank requires four ordered lights");
    std::set<std::size_t> visited;
    for (std::size_t i=0;i<4;++i) {
        const int value=(*members)[i+1];
        if (value<0 || static_cast<std::size_t>(value)>=model.groups().size() ||
            !visited.insert(static_cast<std::size_t>(value)).second)
            throw table::ModelError("invalid or duplicate multiplier light reference");
        const std::size_t group=static_cast<std::size_t>(value);
        registered(model,group,1002);
        if (model.image_states(group).size()!=1)
            throw table::ModelError("multiplier lamp requires one on image");
        const table::GroupInventory& light=model.group(group);
        const BankLampProfile lamp={group,0,duration(record(light,900,2)[1]),duration(record(light,901,2)[1])};
        if (i && (lamp.off_delay!=bank.lamps[0].off_delay || lamp.on_delay!=bank.lamps[0].on_delay))
            throw table::ModelError("multiplier lamps have differing flash intervals");
        bank.lamps[i]=lamp;
    }
}
std::vector<std::size_t> group_members(const table::TableModel& model,std::size_t group,
                                      std::size_t count) {
    const table::GroupInventory& resource=model.group(group);
    const std::vector<std::int16_t>* found=0;
    for (std::size_t r=0;r<resource.short_records.size();++r) {
        const table::ShortRecord& record=resource.short_records[r];
        if (record.field_type!=10 || record.values.empty() || record.values[0]!=1027) continue;
        if (found) throw table::ModelError("duplicate component membership");
        found=&record.values;
    }
    if (!found || found->size()!=count+1)
        throw table::ModelError("wrong component membership count");
    std::vector<std::size_t> members;
    std::set<std::size_t> unique;
    for (std::size_t i=1;i<found->size();++i) {
        const int value=(*found)[i];
        if (value<0 || static_cast<std::size_t>(value)>=model.groups().size() ||
            !unique.insert(static_cast<std::size_t>(value)).second)
            throw table::ModelError("invalid or duplicate component member");
        members.push_back(static_cast<std::size_t>(value));
    }
    return members;
}
BankLampProfile bank_lamp(const table::TableModel& model,std::size_t group) {
    registered(model,group,1002);
    if (model.image_states(group).size()!=1)
        throw table::ModelError("lane lamp requires one on image");
    const table::GroupInventory& resource=model.group(group);
    const BankLampProfile lamp={group,0,duration(record(resource,900,2)[1]),
        duration(record(resource,901,2)[1])};
    return lamp;
}
void rollover_edges(TablePhysics& profile,const table::TableModel& model,
                    std::size_t group,bool light_rollover=false,bool enabled=true) {
    registered(model,group,light_rollover?1020:1015);
    const table::GroupInventory& resource=model.group(group);
    const std::vector<float>& entry=record(resource,600);
    const std::vector<float>& exit=record(resource,603);
    if (entry.size()<3 || entry[1]<4 || entry[1]>65 || std::floor(entry[1])!=entry[1])
        throw table::ModelError("rollover region point count outside supported range");
    const std::size_t count=static_cast<std::size_t>(entry[1]),vertices_count=count-1;
    if (entry.size()!=2*count+3 || exit.size()!=entry.size() || exit[1]!=entry[1] ||
        entry[2]!=entry[2+2*vertices_count] || entry[3]!=entry[3+2*vertices_count] ||
        exit[2]!=exit[2+2*vertices_count] || exit[3]!=exit[3+2*vertices_count])
        throw table::ModelError("rollover requires paired closed polygon regions");
    std::vector<physics::Vec2> vertices;
    for (std::size_t v=0;v<vertices_count;++v) {
        vertices.push_back(physics::Vec2(entry[2+2*v],entry[3+2*v]));
        const std::size_t opposite=(vertices_count-v)%vertices_count;
        if (entry[2+2*v]!=exit[2+2*opposite] || entry[3+2*v]!=exit[3+2*opposite])
            throw table::ModelError("rollover exit must reverse its entry region");
    }
    for (std::size_t v=0;v<vertices_count;++v) {
        const physics::Vec2 a=vertices[v],b=vertices[(v+1)%vertices_count],c=vertices[(v+2)%vertices_count];
        const double cross=(double(b.x)-a.x)*(double(c.y)-b.y)-
                           (double(b.y)-a.y)*(double(c.x)-b.x);
        if (!(cross>0)) throw table::ModelError("rollover entry must be a convex outward-front region");
    }
    const std::size_t images=model.image_states(group).size();
    if (images>1 || (light_rollover && images!=1))
        throw table::ModelError("rollover has wrong optional image count");
    RolloverProfile region={group,std::vector<std::size_t>(),std::vector<std::size_t>(),
        light_rollover?-1:(images?0:-1),light_rollover?0:-1,100000000,
        light_rollover?duration(record(resource,407,2)[1]):runtime::Tick(0)};
    const std::uint32_t mask=collision_mask_for_group(model,group);
    const std::vector<float>* shapes[]={&entry,&exit};
    for (std::size_t phase=0;phase<2;++phase) {
        std::vector<std::size_t>& indices=phase?region.exit_indices:region.entry_indices;
        const std::vector<float>& shape=*shapes[phase];
        for (std::size_t v=0;v<vertices_count;++v) {
            indices.push_back(profile.static_colliders.size());
            motion::StaticCollider event=motion::StaticCollider::event_segment(
                physics::Vec2(shape[4+2*v],shape[5+2*v]),
                physics::Vec2(shape[2+2*v],shape[3+2*v]),mask);
            event.enabled=enabled;
            profile.static_colliders.push_back(event);profile.collider_groups.push_back(group);
        }
    }
    profile.rollovers.push_back(region);
}
void load_lane_groups(TablePhysics& profile,const table::TableModel& model) {
    // Reduced synthetic tables may omit this complete optional feature. A
    // partially present lane feature must still fail rather than load silently.
    const char* names[]={"a_roll1","a_roll2","a_roll3","a_roll110","a_roll111","a_roll112"};
    std::size_t present=0,unused=0;
    for (std::size_t i=0;i<6;++i) if (model.find_named_group(names[i],unused)) ++present;
    if (!present) return;
    if (present!=6) throw table::ModelError("incomplete lane rollover feature");
    for (std::size_t i=0;i<6;++i) rollover_edges(profile,model,model.require_named_group(names[i]));
    const char* light_names[]={"bmpr_inc_lights","ramp_bmpr_inc_lights"};
    const char* bumper_names[]={"attack_bumpers","launch_bumpers"};
    const char* roll_order[2][3]={{"a_roll3","a_roll2","a_roll1"},
                                {"a_roll112","a_roll111","a_roll110"}};
    const char* lamp_names[2][3]={{"lite8","lite9","lite10"},{"lite171","lite170","lite169"}};
    const std::uint32_t awards[2][4]={{500,1000,1500,2000},{1500,2500,3500,4500}};
    for (std::size_t bank=0;bank<2;++bank) {
        LaneGroupProfile& lane=profile.lane_groups[bank];
        lane.light_group=model.require_named_group(light_names[bank]);
        lane.bumper_group=model.require_named_group(bumper_names[bank]);
        registered(model,lane.light_group,1026);registered(model,lane.bumper_group,1028);
        const std::vector<std::size_t> lamps=group_members(model,lane.light_group,3);
        lane.bumper_groups=group_members(model,lane.bumper_group,bank?3:4);
        for (std::size_t i=0;i<3;++i) {
            bool matched=false;
            for (std::size_t role=0;role<3;++role)
                if (lamps[i]==model.require_named_group(lamp_names[bank][role])) {
                    lane.rollover_groups[i]=model.require_named_group(roll_order[bank][role]);matched=true;
                }
            if (!matched) throw table::ModelError("lane group contains an unrelated lamp");
            lane.lamps[i]=bank_lamp(model,lamps[i]);
            if (i && (lane.lamps[i].off_delay!=lane.lamps[0].off_delay ||
                      lane.lamps[i].on_delay!=lane.lamps[0].on_delay))
                throw table::ModelError("lane lamps have differing flash intervals");
        }
        for (std::size_t i=0;i<lane.bumper_groups.size();++i) {
            registered(model,lane.bumper_groups[i],1005);
            if (model.image_states(lane.bumper_groups[i]).size()!=8)
                throw table::ModelError("lane bumper progression requires four paired levels");
        }
        for (std::size_t i=0;i<4;++i) lane.bumper_scores[i]=awards[bank][i];
        lane.rollover_award=bank?500:2000;
    }
}
void load_medal_bank(TablePhysics& profile,const table::TableModel& model) {
    const char* names[]={"a_targ4","a_targ5","a_targ6"};
    std::size_t present=0,unused=0;
    for (std::size_t i=0;i<3;++i) if (model.find_named_group(names[i],unused)) ++present;
    if (!present) return;
    if (present!=3) throw table::ModelError("incomplete medal target feature");
    MedalBankProfile& bank=profile.medal_bank;
    for (std::size_t i=0;i<3;++i) {
        bank.targets[i]=target_body(profile,model,model.require_named_group(names[i]));
        if (i && bank.targets[i].rearm_delay!=bank.targets[0].rearm_delay)
            throw table::ModelError("medal targets have differing rearm delays");
    }
    bank.light_group=model.require_named_group("bumper_target_lights");
    registered(model,bank.light_group,1026);
    const std::vector<std::size_t> members=group_members(model,bank.light_group,3);
    for (std::size_t i=0;i<3;++i) {
        bank.lamps[i]=bank_lamp(model,members[i]);
        if (i && (bank.lamps[i].off_delay!=bank.lamps[0].off_delay ||
                  bank.lamps[i].on_delay!=bank.lamps[0].on_delay))
            throw table::ModelError("medal lamps have differing flash intervals");
    }
}
SoundReference sound_reference(const table::TableModel& model,const char* name) {
    SoundReference sound;sound.group=model.require_named_group(name);
    registered(model,sound.group,1031);
    sound.resource_group=link(model,model.group(sound.group),1100);
    if (sound.resource_group==model.groups().size()) throw table::ModelError("sound has no resource reference");
    marker(model.group(sound.resource_group),202);
    const std::vector<data::Field>& fields=model.dat().groups()[sound.resource_group].fields;
    bool found=false;
    for (std::size_t f=0;f<fields.size();++f) if (fields[f].type==9) {
        if (found) throw table::ModelError("duplicate sound filename");
        sound.filename=model.dat().string_value(fields[f]);found=true;
    }
    if (!found || sound.filename.empty()) throw table::ModelError("sound resource has no filename");
    return sound;
}
void load_advanced(TablePhysics& profile,const table::TableModel& model) {
    AdvancedTableProfile& advanced=profile.advanced;
    const std::size_t none=(std::numeric_limits<std::size_t>::max)();
    for (std::size_t number=1;number<=22;++number) {
        if (number>=4 && number<=9) continue;
        std::size_t group=0;
        if (!model.find_named_group("a_targ"+std::to_string(number),group)) continue;
        AdvancedTargetProfile target;target.number=number;
        target.body=target_body(profile,model,group,number>=10);
        target.lamp_group=none;
        if (number<4) {
            target.kind=AdvancedTargetKind::Booster;target.index=number-1;target.score=500;
        } else {
            const char* lamp_prefix="lite";std::size_t light_number=0;
            if (number<=12) {target.kind=AdvancedTargetKind::Fuel;target.index=number-10;target.score=750;light_number=70+target.index;}
            else if (number<=15) {target.kind=AdvancedTargetKind::Mission;target.index=number-13;target.score=1000;light_number=101+target.index;}
            else if (number<=18) {target.kind=AdvancedTargetKind::LeftHazard;target.index=number-16;target.score=750;light_number=104+target.index;}
            else if (number<=21) {target.kind=AdvancedTargetKind::RightHazard;target.index=number-19;target.score=750;light_number=107+target.index;}
            else {target.kind=AdvancedTargetKind::WormholeDestination;target.index=0;target.score=750;light_number=110;}
            target.lamp_group=model.require_named_group(std::string(lamp_prefix)+std::to_string(light_number));
            bank_lamp(model,target.lamp_group);
        }
        for (std::size_t e=0;e<target.body.collider_indices.size();++e)
            profile.static_colliders[target.body.collider_indices[e]].enabled=false;
        advanced.targets.push_back(target);
    }
    const std::size_t numbers[]={4,5,6,7,8,179,180,181,182,183,184,9};
    for (std::size_t i=0;i<12;++i) {
        const std::size_t number=numbers[i];std::size_t group=0;
        if (!model.find_named_group("a_roll"+std::to_string(number),group)) continue;
        AdvancedRolloverProfile rollover;rollover.region_index=profile.rollovers.size();rollover.number=number;
        rollover.lamp_group=none;rollover.score=0;rollover.lit_score=0;rollover.fuel_level=0;
        if (number==4 || number==8) {
            rollover.kind=AdvancedRolloverKind::Outlane;rollover.score=20000;rollover.lit_score=20000;
        } else if (number==5) {
            rollover.kind=AdvancedRolloverKind::Bonus;rollover.score=10000;
            rollover.lamp_group=model.require_named_group("lite16");
        } else if (number==6 || number==7) {
            rollover.kind=AdvancedRolloverKind::Return;rollover.score=5000;rollover.lit_score=25000;
            rollover.lamp_group=model.require_named_group(number==6?"lite27":"lite28");
        } else if (number>=179) {
            rollover.kind=AdvancedRolloverKind::Fuel;rollover.score=500;rollover.fuel_level=1+2*static_cast<std::uint32_t>(number-179);
            rollover.lamp_group=model.require_named_group("literoll"+std::to_string(number));
        } else rollover.kind=AdvancedRolloverKind::SpaceWarp;
        if (rollover.lamp_group!=none) bank_lamp(model,rollover.lamp_group);
        rollover_edges(profile,model,group,number==9,false);
        advanced.rollovers.push_back(rollover);
    }
    const std::size_t skill_lights[]={68,69,131,132,133};
    for (std::size_t i=0;i<5;++i) {
        std::size_t group=0;
        if (!model.find_named_group("s_trip"+std::to_string(i+1),group)) continue;
        registered(model,group,1024);
        const std::vector<float>& shape=record(model.group(group),600,7);
        if (shape[1]!=2 || (shape[2]==shape[4] && shape[3]==shape[5]))
            throw table::ModelError("skill tripwire requires a nonzero segment");
        const double dx=double(shape[4])-shape[2],dy=double(shape[5])-shape[3];
        const double length=std::sqrt(dx*dx+dy*dy);
        const double sx=double(profile.ball_radius)*dy/length,sy=-double(profile.ball_radius)*dx/length;
        motion::StaticCollider sensor=motion::StaticCollider::event_segment(
            physics::Vec2(narrow(double(shape[4])+sx),narrow(double(shape[5])+sy)),
            physics::Vec2(narrow(double(shape[2])+sx),narrow(double(shape[3])+sy)),
            collision_mask_for_group(model,group));
        sensor.enabled=false;
        const std::size_t lamp=model.require_named_group("lite"+std::to_string(skill_lights[i]));
        bank_lamp(model,lamp);
        const SkillTripwireProfile trip={group,profile.static_colliders.size(),lamp,i+1};
        profile.static_colliders.push_back(sensor);profile.collider_groups.push_back(group);
        advanced.skill_tripwires.push_back(trip);
    }
    std::size_t fuel_group=0;
    if (model.find_named_group("fuel_bargraph",fuel_group)) {
        registered(model,fuel_group,1030);advanced.fuel.present=true;advanced.fuel.group=fuel_group;
        const std::vector<std::size_t> members=group_members(model,fuel_group,6);
        const std::vector<float>& times=record(model.group(fuel_group),904,13);
        for (std::size_t i=0;i<6;++i) advanced.fuel.lamps[i]=bank_lamp(model,members[i]);
        for (std::size_t i=0;i<12;++i) advanced.fuel.level_durations[i]=duration(times[i+1]);
    }
    std::size_t hyperspace=0;
    if (model.find_named_group("a_kout2",hyperspace)) {
        advanced.has_hyperspace=true;
        advanced.hyperspace=kickout(model,hyperspace,profile.static_colliders.size());
        advanced.hyperspace.delay=0; // Controller selects a sound duration, not the generic 1.5 s default.
        motion::StaticCollider capture=motion::StaticCollider::center_capture(
            advanced.hyperspace.center,advanced.hyperspace.capture_radius);
        const response::Material material=material_for_group(model,hyperspace);
        append(profile,capture,material,hyperspace,collision_mask_for_group(model,hyperspace));
        profile.static_colliders.back().enabled=false;
        advanced.hyperspace_light_group=model.require_named_group("hyperspace_lights");
        registered(model,advanced.hyperspace_light_group,1026);
        const std::vector<std::size_t> members=group_members(model,advanced.hyperspace_light_group,4);
        for (std::size_t i=0;i<4;++i) advanced.hyperspace_lamps[i]=bank_lamp(model,members[i]);
        const char* sounds[]={"soundwave35","soundwave36","soundwave35","soundwave38","soundwave39"};
        for (std::size_t i=0;i<5;++i) advanced.hyperspace_sound_refs[i]=sound_reference(model,sounds[i]);
        advanced.hyperspace_reflex_sound=sound_reference(model,"soundwave21");
        advanced.hyperspace_combined_sound=sound_reference(model,"soundwave40");
        advanced.hyperspace_special_sound=sound_reference(model,"soundwave41");
    }
    std::size_t deployment=0;
    if (model.find_named_group("s_onewy4",deployment)) {
        motion::StaticCollider gate=pass_gate_event(model,deployment,profile.ball_radius);gate.enabled=false;
        const DeploymentGateProfile metadata={deployment,4,profile.static_colliders.size()};
        profile.static_colliders.push_back(gate);profile.collider_groups.push_back(deployment);
        advanced.deployment_gates.push_back(metadata);
    }
    const DeploymentGateProfile main_exit={profile.rules.skill_clear_group,10,profile.rules.skill_clear_collider_index};
    advanced.deployment_gates.push_back(main_exit);
    // Explicit dependency lights only. Default visibility remains Session-owned.
    const char* light_names[]={"lite16","lite17","lite18","lite19","lite20","lite25","lite26","lite27","lite28",
        "lite58","lite59","lite60","lite61","lite67","lite68","lite69","lite70","lite71","lite72",
        "lite101","lite102","lite103","lite104","lite105","lite106","lite107","lite108","lite109","lite110",
        "lite131","lite132","lite133","literoll179","literoll180","literoll181","literoll182","literoll183","literoll184"};
    for (std::size_t i=0;i<sizeof(light_names)/sizeof(light_names[0]);++i) {
        std::size_t group=0;if (model.find_named_group(light_names[i],group)) advanced.lights.push_back(bank_lamp(model,group));
    }
}
void load_moving_features(TablePhysics& profile,const table::TableModel& model) {
    AdvancedTableProfile& advanced=profile.advanced;
    const std::size_t none=(std::numeric_limits<std::size_t>::max)();
    for (std::size_t i=0;i<2;++i) {
        std::size_t group=0;
        if (!model.find_named_group("a_flag"+std::to_string(i+1),group)) continue;
        registered(model,group,1018);
        const table::GroupInventory& resource=model.group(group);
        const std::vector<float>& shape=record(resource,600,7);
        if (shape[1]!=2 || (shape[2]==shape[4] && shape[3]==shape[5]))
            throw table::ModelError("flag spinner requires a nonzero segment");
        const std::size_t images=model.image_states(group).size();
        if (images<2) throw table::ModelError("flag spinner requires animation frames");
        const float minimum=optional_scalar(resource,1201,5),maximum=optional_scalar(resource,1200,50000);
        const float decay=optional_scalar(resource,1202,.64999998f);
        if (minimum<=0 || maximum<minimum || decay<=0 || decay>=1)
            throw table::ModelError("invalid spinner speed or decay");
        FlagSpinnerProfile spinner={group,i+1,profile.static_colliders.size(),profile.static_colliders.size()+1,
            images,minimum,maximum,20,decay,500,2500,0,true};
        const std::uint32_t mask=collision_mask_for_group(model,group);
        motion::StaticCollider forward=motion::StaticCollider::event_segment(
            physics::Vec2(shape[2],shape[3]),physics::Vec2(shape[4],shape[5]),mask);
        motion::StaticCollider reverse=motion::StaticCollider::event_segment(
            physics::Vec2(shape[4],shape[5]),physics::Vec2(shape[2],shape[3]),mask);
        forward.enabled=reverse.enabled=false;
        profile.static_colliders.push_back(forward);profile.collider_groups.push_back(group);
        profile.static_colliders.push_back(reverse);profile.collider_groups.push_back(group);
        advanced.spinners.push_back(spinner);
    }
    const char* walls[]={"v_gate1","v_gate2","v_bloc1"};
    const char* lamp_names[3][2]={{"lite30","lite196"},{"lite29","lite195"},{"lite1",0}};
    for (std::size_t i=0;i<3;++i) {
        std::size_t group=0;if (!model.find_named_group(walls[i],group)) continue;
        const bool blocker=i==2;registered(model,group,blocker?1011:1013);
        if (model.image_states(group).size()!=1) throw table::ModelError("dynamic wall needs one closed image");
        const std::vector<float>& shape=record(model.group(group),600,blocker?6:7);
        if ((!blocker && (shape[1]!=2 || (shape[2]==shape[4] && shape[3]==shape[5]))) ||
            (blocker && (shape[1]!=1 || shape[4]<=0)))
            throw table::ModelError("invalid dynamic wall geometry");
        response::Material material=material_for_group(model,group);
        if (blocker) {material.threshold=1000000000.0f;material.boost=0;}
        DynamicWallProfile wall={group,std::vector<std::size_t>(),blocker?DynamicWallKind::Blocker:DynamicWallKind::Gate,
            0,-1,!blocker,material,{{none,none}},blocker?runtime::Tick(55000000000LL):runtime::Tick(0),
            blocker?runtime::Tick(5000000000LL):runtime::Tick(0)};
        for (std::size_t lamp=0;lamp<2;++lamp) if (lamp_names[i][lamp]) {
            wall.lamp_groups[lamp]=model.require_named_group(lamp_names[i][lamp]);
            bank_lamp(model,wall.lamp_groups[lamp]);
        }
        motion::StaticCollider collider=blocker?
            motion::StaticCollider::disk(physics::Vec2(shape[2],shape[3]),shape[4],material.elasticity):
            motion::StaticCollider::segment(physics::Vec2(shape[4],shape[5]),physics::Vec2(shape[2],shape[3]),material.elasticity,false);
        collider.enabled=false;
        wall.collider_indices.push_back(profile.static_colliders.size());
        append(profile,collider,material,group,collision_mask_for_group(model,group));
        advanced.dynamic_walls.push_back(wall);
    }
    std::size_t group=0;
    if (model.find_named_group("a_kout1",group)) {
        advanced.has_gravity_well=true;
        GravityWellProfile& well=advanced.gravity_well;
        well.capture=kickout(model,group,profile.static_colliders.size(),true);
        well.capture.delay=0;well.release_sound=sound_reference(model,"soundwave7");
        well.lamp_group=model.require_named_group("lite62");bank_lamp(model,well.lamp_group);
        motion::StaticCollider capture=motion::StaticCollider::center_capture(well.capture.center,well.capture.capture_radius);
        capture.enabled=false;
        append(profile,capture,material_for_group(model,group),group,collision_mask_for_group(model,group));
    }
    const char* dependencies[]={"lite1","lite30","lite196","lite29","lite195","lite62"};
    for (std::size_t i=0;i<sizeof(dependencies)/sizeof(dependencies[0]);++i) {
        std::size_t dependency_group=0;if (!model.find_named_group(dependencies[i],dependency_group)) continue;
        bool found=false;
        for (std::size_t existing=0;existing<advanced.lights.size();++existing)
            if (advanced.lights[existing].group==dependency_group) found=true;
        if (!found) advanced.lights.push_back(bank_lamp(model,dependency_group));
    }
}
void load_remaining_walls(TablePhysics& profile,const table::TableModel& model) {
    AdvancedTableProfile& advanced=profile.advanced;
    const std::size_t none=(std::numeric_limits<std::size_t>::max)();
    for (std::size_t i=0;i<2;++i) {
        std::size_t group=0;
        if (!model.find_named_group("a_kick"+std::to_string(i+1),group)) continue;
        registered(model,group,1014);
        const std::vector<float>& line=record(model.group(group),600,7);
        if (line[1]!=2 || (line[2]==line[4] && line[3]==line[5]))
            throw table::ModelError("kickback needs a nonzero segment");
        if (model.image_states(group).size()!=2) throw table::ModelError("kickback needs idle/fired images");
        const std::size_t gate=model.require_named_group("v_gate"+std::to_string(i+1));
        registered(model,gate,1013);
        response::Material material=material_for_group(model,group);material.threshold=1000000000.0f;
        const KickbackProfile kick={group,i+1,profile.static_colliders.size(),gate,material,0,
            700000000,100000000,0,1,-1,true};
        // Source offsets the center ray wall +R. Our primitive remains RAW:
        // the circle query adds R exactly once along this same source front.
        motion::StaticCollider solid=motion::StaticCollider::segment(
            physics::Vec2(line[4],line[5]),physics::Vec2(line[2],line[3]),material.elasticity,false);
        solid.enabled=false;solid.latch_response=true;solid.directional_approach_only=true;
        append(profile,solid,material,group,collision_mask_for_group(model,group));
        advanced.kickbacks.push_back(kick);
    }
    std::set<std::size_t> visited;
    const std::size_t numbers[]={1,4,7,8,9,10,11,12,13};
    for (std::size_t i=0;i<model.objects().size();++i) {
        const table::ObjectEntry& object=model.objects()[i];
        if (object.type!=1016 || !visited.insert(object.group_index).second) continue;
        const std::size_t group=object.group_index;
        const std::vector<float>& line=record(model.group(group),600,7);
        if (line[1]!=2 || (line[2]==line[4] && line[3]==line[5]))
            throw table::ModelError("one-way wall needs a nonzero segment");
        std::size_t number=0,pass=none;
        for (std::size_t known=0;known<sizeof(numbers)/sizeof(numbers[0]);++known)
            if (model.group(group).name=="s_onewy"+std::to_string(numbers[known])) number=numbers[known];
        if (group==profile.rules.skill_gate_group) pass=profile.rules.skill_gate_collider_index;
        if (group==profile.rules.skill_clear_group) pass=profile.rules.skill_clear_collider_index;
        for (std::size_t e=0;e<advanced.deployment_gates.size();++e)
            if (advanced.deployment_gates[e].group==group) pass=advanced.deployment_gates[e].collider_index;
        const response::Material material=material_for_group(model,group);
        const OneWayProfile oneway={group,number,profile.static_colliders.size(),pass,material,true};
        // Do not shift these stored endpoints: circle radius supplies source
        // +R. Pass-through events, when bound, keep their separate -.8R edge.
        motion::StaticCollider solid=motion::StaticCollider::segment(
            physics::Vec2(line[4],line[5]),physics::Vec2(line[2],line[3]),material.elasticity,false);
        solid.enabled=false;solid.directional_approach_only=true;
        append(profile,solid,material,group,collision_mask_for_group(model,group));
        advanced.one_ways.push_back(oneway);
    }
}
void load_escape_chute(TablePhysics& profile,const table::TableModel& model) {
    std::size_t group=0;if (!model.find_named_group("v_sink7",group)) return;
    const std::vector<float>& shape=record(model.group(group),600,7);
    if (shape[1]!=2 || (shape[2]==shape[4] && shape[3]==shape[5]))
        throw table::ModelError("escape chute needs a nonzero capture segment");
    profile.advanced.has_escape_chute=true;
    profile.advanced.escape_chute=sink(model,group,profile.static_colliders.size());
    const response::Material material=material_for_group(model,group);
    motion::StaticCollider capture=motion::StaticCollider::segment(
        physics::Vec2(shape[4],shape[5]),physics::Vec2(shape[2],shape[3]),material.elasticity,false);
    capture.enabled=false;capture.captures_ball=true;
    append(profile,capture,material,group,collision_mask_for_group(model,group));
    // EscapeChuteSinkControl requests this owner's default timer and no award.
    // Keep it separate from the three destination-routed wormhole owners.
}
}

response::Material material_for_group(const table::TableModel& model,std::size_t group_index) {
    const table::GroupInventory& group=model.group(group_index);
    const std::size_t no_link=model.groups().size();
    const std::size_t material=link(model,group,300),kicker=link(model,group,400);
    float smoothness=.95f,elasticity=.6f,threshold=default_threshold,boost=0;
    if (material!=no_link) material_values(model.group(material),smoothness,elasticity);
    if (kicker!=no_link) kicker_values(model.group(kicker),threshold,boost);
    return response::Material(elasticity,smoothness,threshold,boost);
}

std::uint32_t collision_mask_for_group(const table::TableModel& model,std::size_t group_index) {
    const table::GroupInventory& group=model.group(group_index);
    std::uint32_t mask=0;
    for (std::size_t r=0;r<group.short_records.size();++r) {
        const table::ShortRecord& rec=group.short_records[r];
        if (rec.field_type!=10) continue;
        if (rec.values.size()%2) throw table::ModelError("visual attributes must be pairs");
        for (std::size_t i=0;i<rec.values.size();i+=2) {
            if (rec.values[i]!=602) continue;
            const int bit=rec.values[i+1];
            if (bit<0 || bit>=32) throw table::ModelError("collision bit position outside unsigned32 range");
            mask|=std::uint32_t(1)<<static_cast<unsigned>(bit);
        }
    }
    return mask?mask:1u;
}

RuleProfile::RuleProfile()
    : event_group_awards(),drain_group(0),drain_collider_index(0),
      skill_gate_group(0),skill_gate_collider_index(0),ramp_score_group(0),ramp_score_collider_index(0),
      skill_clear_group(0),skill_clear_collider_index(0),
      drain_delay(0),feed_delay(960000000),shoot_again_on(5000000000LL),shoot_again_flash(5000000000LL),
      reflex_duration(5000000000LL),balls_per_game(3),initial_bonus(10000),subsequent_bonus(25000),
      score_wrap(1000000000),reflex_award(25000) {}

TargetBankProfile::TargetBankProfile()
    : targets(),lamps(),light_group(0),hit_award(500),complete_award(1500),
      multipliers{{1,2,3,5,10}},flash_duration(2000000000LL),decay_period(30000000000LL) {}

MedalBankProfile::MedalBankProfile()
    : targets(),lamps(),light_group(0),hit_award(1500),
      completion_awards{{10000,50000,0}},extra_ball_level(3),
      flash_duration(2000000000LL),decay_period(30000000000LL) {}

FuelProfile::FuelProfile() : present(false),group(0),lamps(),level_durations() {}
SoundReference::SoundReference() : group((std::numeric_limits<std::size_t>::max)()),
    resource_group((std::numeric_limits<std::size_t>::max)()),filename() {}
GravityWellProfile::GravityWellProfile()
    : capture(),release_sound(),lamp_group(0),default_enabled(false),reactivate_after_cooldown(false),score(50000) {}

AdvancedTableProfile::AdvancedTableProfile()
    : targets(),rollovers(),skill_tripwires(),deployment_gates(),fuel(),has_hyperspace(false),hyperspace(),
      hyperspace_light_group(0),hyperspace_lamps(),hyperspace_awards{{10000,0,20000,50000,150000}},
      hyperspace_sound_refs(),hyperspace_reflex_sound(),hyperspace_combined_sound(),hyperspace_special_sound(),lights(),spinners(),dynamic_walls(),
      has_gravity_well(false),gravity_well(),kickbacks(),one_ways(),has_escape_chute(false),escape_chute() {}

LaneGroupProfile::LaneGroupProfile()
    : light_group(0),bumper_group(0),rollover_groups(),lamps(),bumper_groups(),
      bumper_scores(),rollover_award(0),completion_flash(5000000000LL),decay_period(60000000000LL) {}

TablePhysics::TablePhysics(const table::TableModel& model)
    : table_group(model.require_named_group("table")),
      ball_group(model.require_named_group("ball")),
      plunger_group(model.require_named_group("plunger")),
      ball_radius(radius(model.group(ball_group))),
      feed_position(feed(model.group(plunger_group))),
      seat_position(seat(model.group(plunger_group),feed_position,ball_radius)),
      plunger_image_count(model.image_states(plunger_group).size()),
      field(reference::field(model.group(table_group),ball_radius)),
      static_colliders(),collider_groups(),ground_excluded_groups(),ground_excluded_collider_count(0),
      plunger_collider_index(0),
      flipper_groups{{model.require_named_group("a_flip1"),model.require_named_group("a_flip2")}},
      flippers{{flip(model,flipper_groups[0]),flip(model,flipper_groups[1])}},sinks(),kickouts(),ramp_planes(),ramp_transition_indices(),holes(),components(),rules(),multiplier_bank(),rollovers(),lane_groups(),medal_bank(),advanced() {
    if (!plunger_image_count) throw table::ModelError("plunger has no images");
    std::set<std::size_t> visited;
    for (std::size_t i=0;i<model.objects().size();++i) {
        const table::ObjectEntry& object=model.objects()[i];
        if ((object.type!=1000 && object.type!=1010) || !visited.insert(object.group_index).second) continue;
        const table::GroupInventory& group=model.group(object.group_index);
        const std::vector<float>& v=record(group,600);
        if (v.size()<2 || v[1]<1 || v[1]>65535 || std::floor(v[1])!=v[1])
            throw table::ModelError("unsupported static geometry point count");
        const std::size_t n=static_cast<std::size_t>(v[1]);
        if ((n==1 && v.size()!=6) || (n>1 && v.size()!=2*n+3))
            throw table::ModelError("malformed static geometry record");
        if (!(collision_mask_for_group(model,object.group_index)&1u)) {
            ground_excluded_groups.push_back(object.group_index);
            const std::size_t primitives=n==1?1:n-1;
            if (primitives>(std::numeric_limits<std::size_t>::max)()-ground_excluded_collider_count)
                throw std::overflow_error("excluded ground collider count overflow");
            ground_excluded_collider_count+=primitives;
        }
        const response::Material material=material_for_group(model,object.group_index);
        if (n==1) {
            append(*this,motion::StaticCollider::disk(physics::Vec2(v[2],v[3]),v[4],
                material.elasticity),material,object.group_index,collision_mask_for_group(model,object.group_index));
        } else {
            for (std::size_t p=1;p<n;++p)
                append(*this,motion::StaticCollider::segment(
                    physics::Vec2(v[2+2*p],v[3+2*p]),
                    physics::Vec2(v[2+2*(p-1)],v[3+2*(p-1)]),material.elasticity,false),material,object.group_index,collision_mask_for_group(model,object.group_index));
        }
    }
    // Source plunger overrides both coefficients and starts with boosting
    // disabled. Session updates this collider's threshold/boost on release.
    plunger_collider_index=static_colliders.size();
    const std::vector<float>& line=striker(model.group(plunger_group));
    const response::Material plunger_material(.5f,.5f,1000000000.0f,0);
    append(*this,motion::StaticCollider::segment(physics::Vec2(line[4],line[5]),
        physics::Vec2(line[2],line[3]),.5f,false),plunger_material,plunger_group,collision_mask_for_group(model,plunger_group));
    // Default unlit WormHoleControl route only: capture here and later eject
    // from this sink's601 point. Full light/mission/lock routing is not loaded.
    const char* sink_names[]={"v_sink1","v_sink2","v_sink3"};
    for (std::size_t i=0;i<3;++i) {
        const std::size_t group=model.require_named_group(sink_names[i]);
        const std::vector<float>& capture=record(model.group(group),600,7);
        if (capture[1]!=2 || (capture[2]==capture[4] && capture[3]==capture[5]))
            throw table::ModelError("wormhole capture requires a two-point segment");
        const SinkProfile profile=sink(model,group,static_colliders.size());
        const response::Material material=material_for_group(model,group);
        motion::StaticCollider collider=motion::StaticCollider::segment(
            physics::Vec2(capture[4],capture[5]),physics::Vec2(capture[2],capture[3]),
            material.elasticity,false);
        collider.captures_ball=true;
        append(*this,collider,material,group,collision_mask_for_group(model,group));
        sinks.push_back(profile);
    }
    // The default BlackHoleKickoutControl timer is 1.5 s; complete scoring and
    // mission behavior remain Session-level work, not profile metadata.
    const std::size_t black_hole=model.require_named_group("a_kout3");
    const KickoutProfile profile=kickout(model,black_hole,static_colliders.size());
    const response::Material material=material_for_group(model,black_hole);
    motion::StaticCollider trigger=motion::StaticCollider::center_capture(profile.center,profile.capture_radius);
    trigger.restitution=material.elasticity;
    append(*this,trigger,material,black_hole,collision_mask_for_group(model,black_hole));
    kickouts.push_back(profile);
    const float magnitude=record(model.group(table_group),305,4)[1];
    std::set<std::size_t> loaded_ramps;
    for (std::size_t i=0;i<model.objects().size();++i) {
        const table::ObjectEntry& object=model.objects()[i];
        if (object.type==1021 && loaded_ramps.insert(object.group_index).second)
            ramp(*this,model,object.group_index,magnitude);
    }
    const std::size_t ramp_hole=model.require_named_group("ramp_hole");
    const HoleProfile exit=hole(model,ramp_hole,static_colliders.size(),magnitude);
    motion::StaticCollider sensor=motion::StaticCollider::center_capture(exit.center,exit.capture_radius);
    append(*this,sensor,material_for_group(model,ramp_hole),ramp_hole,exit.collision_mask);
    holes.push_back(exit);
    // Append newly supported bumper bodies after existing descriptor indices.
    hit_components(*this,model);
    rule_metadata(*this,model);
    load_multiplier_bank(*this,model);
    load_lane_groups(*this,model);
    load_medal_bank(*this,model);
    load_advanced(*this,model);
    load_moving_features(*this,model);
    load_remaining_walls(*this,model);
    load_escape_chute(*this,model);
}

} // namespace reference
} // namespace pinball
