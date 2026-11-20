#include "playfield_test.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <sstream>
namespace pinball {
namespace development {
namespace {
const runtime::Tick physics_step=runtime::ticks_per_second/120;
rules::GameRulesConfig rules_config(const reference::RuleProfile& p) {
    return rules::GameRulesConfig(p.balls_per_game,p.drain_delay,p.feed_delay,
        p.shoot_again_on,p.shoot_again_flash,p.initial_bonus,p.subsequent_bonus,
        p.score_wrap,p.reflex_duration,p.reflex_award);
}
rules::TargetBankConfig target_config(const reference::TargetBankProfile& p) {
    static const std::uint32_t factors[5]={1,2,3,5,10};
    for(unsigned i=0;i<5;++i)if(p.multipliers[i]!=factors[i])
        throw std::runtime_error("unsupported multiplier bank factors");
    for(unsigned i=1;i<3;++i)if(p.targets[i].rearm_delay!=p.targets[0].rearm_delay)
        throw std::runtime_error("multiplier targets require equal rearm durations");
    for(unsigned i=1;i<4;++i)if(p.lamps[i].off_delay!=p.lamps[0].off_delay||
                              p.lamps[i].on_delay!=p.lamps[0].on_delay)
        throw std::runtime_error("multiplier lamps require equal flash cadence");
    return rules::TargetBankConfig(p.targets[0].rearm_delay,p.decay_period,p.flash_duration,
        p.lamps[0].off_delay,p.lamps[0].on_delay,p.hit_award,p.complete_award);
}
rules::TargetBankConfig medal_config(const reference::MedalBankProfile& p) {
    return rules::TargetBankConfig(p.targets[0].rearm_delay,p.decay_period,p.flash_duration,
        p.lamps[0].off_delay,p.lamps[0].on_delay,p.hit_award,p.completion_awards,p.extra_ball_level);
}
std::array<rules::LaneProgressionConfig,2> progression_config(const std::array<reference::LaneGroupProfile,2>& p) {
    return {{rules::LaneProgressionConfig(p[0].completion_flash,p[0].decay_period,p[0].lamps[0].off_delay,p[0].lamps[0].on_delay),
             rules::LaneProgressionConfig(p[1].completion_flash,p[1].decay_period,p[1].lamps[0].off_delay,p[1].lamps[0].on_delay)}};
}
rules::AdvancedRulesConfig advanced_config(const reference::AdvancedTableProfile& p) {
    rules::AdvancedRulesConfig c;
    if(p.fuel.present)c.fuel_durations=p.fuel.level_durations;
    c.hyperspace_awards=p.hyperspace_awards;
    // Durations are exact frame-count/sample-rate facts from the supplied WAVs.
    // Audio playback can remain absent without guessing capture hold lengths.
    const std::uint64_t frames[5]={19358,33707,19358,12884,28141};
    for(unsigned i=0;i<5;++i)c.hyperspace_delays[i]=static_cast<runtime::Tick>(frames[i]*1000000000ULL/11025ULL);
    c.hyperspace_reflex_delay=9054LL*1000000000LL/11025LL;
    c.hyperspace_combined_delay=8509LL*1000000000LL/11025LL;
    c.hyperspace_special_delay=10224LL*1000000000LL/11025LL;
    return c;
}
pinball360::Palette256 palette(const table::TableModel& model) {
    const data::Field* found=0;
    for(std::size_t g=0;g<model.dat().groups().size();++g)
        for(std::size_t f=0;f<model.dat().groups()[g].fields.size();++f) {
            const data::Field& value=model.dat().groups()[g].fields[f];
            if(value.type==5){if(found)throw std::runtime_error("ambiguous palette");found=&value;}
        }
    if(!found)throw std::runtime_error("missing palette");
    const data::ByteView bytes=model.dat().payload(*found);
    return pinball360::decode_palette(bytes.data,bytes.size);
}
void count(std::uint32_t& value,std::size_t amount=1) {
    const std::uint32_t limit=(std::numeric_limits<std::uint32_t>::max)();
    value=amount>limit-value?limit:value+static_cast<std::uint32_t>(amount);
}
float drain_level(const table::TableModel& model,float radius) {
    const table::GroupInventory& group=model.group(model.require_named_group("table"));
    for(std::size_t i=0;i<group.float_records.size();++i) {
        const std::vector<float>& v=group.float_records[i].values;
        if(v.size()>3 && v[0]==600) {
            if(!std::isfinite(v[1]) || v[1]<2 || v[1]>65535 || std::floor(v[1])!=v[1])
                throw std::runtime_error("invalid table boundary count");
            const std::size_t n=static_cast<std::size_t>(v[1]);
            if(n<2 || v.size()!=n*2+3)throw std::runtime_error("invalid table boundary");
            float result=-std::numeric_limits<float>::max();
            for(std::size_t p=0;p<n;++p) {
                if(!std::isfinite(v[2+2*p]) || !std::isfinite(v[3+2*p]))
                    throw std::runtime_error("invalid table boundary coordinate");
                result=(std::max)(result,v[3+2*p]);
            }
            return result-2*radius;
        }
    }
    throw std::runtime_error("missing table boundary");
}
runtime::Tick validated_time(runtime::Tick start,runtime::Tick duration,double seconds) {
    const double ns=(std::max)(0.0,(std::min)(double(duration),seconds*1e9));
    return start+static_cast<runtime::Tick>(ns);
}
}
PlayfieldTest::PlayfieldTest(const table::TableModel& model):PlayfieldTest(model,SessionMode::FiniteRound) {}
PlayfieldTest::PlayfieldTest(const table::TableModel& model,SessionMode mode):
    profile_(model),camera_(visual::camera_from_table(model)),
    art_(visual::BallSpriteCache::from_resources(model.dat(),
        visual::ordered_image_resources(model,profile_.ball_group),palette(model))),
    score_display_(model),art_depth_(),colliders_(profile_.static_colliders),flippers_(),kickout_cooldowns_(profile_.kickouts.size(),0),
    component_states_(profile_.components.size()),component_for_collider_(colliders_.size(),(std::numeric_limits<std::size_t>::max)()),
    target_for_collider_(colliders_.size(),(std::numeric_limits<std::size_t>::max)()),
    advanced_target_for_collider_(colliders_.size(),(std::numeric_limits<std::size_t>::max)()),advanced_light_numbers_(),
    rollover_states_(profile_.rollovers.size()),rollover_for_collider_(colliders_.size(),(std::numeric_limits<std::size_t>::max)()),
    rollover_exit_(colliders_.size(),false),ball_(profile_.feed_position,physics::Vec2(),profile_.ball_radius),
    stepper_(motion::MotionConfig(physics::Vec2(),physics_step,24,.0001f,profile_.field.maximum_speed)),
    plunger_(reference::PlungerConfig(profile_.plunger_image_count)),nudge_(),
    hole_drop_(reference::HoleDropConfig(profile_.holes.at(0).delay,profile_.holes.at(0).fall_acceleration)),
    game_rules_(rules_config(profile_.rules)),target_bank_(target_config(profile_.multiplier_bank)),medal_bank_(medal_config(profile_.medal_bank)),
    progression_(progression_config(profile_.lane_groups)),mission_(),advanced_(advanced_config(profile_.advanced)),spinners_(),kickbacks_(),tilt_counter_(),tilt_nudge_active_(false),next_serve_saved_(false),unlimited_balls_(false),cheats_used_(false),rank_flash_started_(0),rank_flash_duration_(0),rank_flash_index_(0),
    hyperspace_kickout_((std::numeric_limits<std::size_t>::max)()),gravity_kickout_((std::numeric_limits<std::size_t>::max)()),gravity_active_(false),
    mission_events_(),mission_started_(0),finite_round_(mode==SessionMode::FiniteRound),
    time_(0),sink_wait_(0),captured_index_(0),seated_(false),suspended_(false),captured_(false),captured_kickout_(false),captured_hole_(false),hidden_test_(false),test_eject_remaining_(0),test_stick_x_(0),test_stick_y_(0),random_state_(0x95c0ffeeU),
    launches_(0),returns_(0),contacts_(0),exhausted_(0),flipper_contacts_(0),nudges_(0),moving_exhausted_(0),sink_captures_(0),sink_ejections_(0),kickout_captures_(0),kickout_ejections_(0),hole_captures_(0),hole_landings_(0),ramp_transitions_(0),
    bumper_hits_(0),rebound_hits_(0),target_hits_(0),bank_completions_(0),lane_hits_(0),lane_completions_(0),medal_hits_(0),medal_completions_(0),captured_height_(profile_.ball_radius),captured_mask_(1),
    drain_y_(drain_level(model,profile_.ball_radius)),table_width_(0),table_height_(0),sound_cues_(),sound_drops_(0),
    rank_sound_((std::numeric_limits<std::size_t>::max)()),mission_sound_((std::numeric_limits<std::size_t>::max)()),drain_sound_((std::numeric_limits<std::size_t>::max)()),event_sounds_() {
    sound_cues_.reserve(256);model.find_named_group("soundwave10",rank_sound_);model.find_named_group("soundwave9",mission_sound_);model.find_named_group("soundwave27",drain_sound_);
    const unsigned sound_numbers[]={3,7,14,21,23,24,25,26,28,30,35,36,38,39,40,41,45,46,47,48,49,50,52,59};
    for(unsigned number:sound_numbers){std::size_t group=0;if(model.find_named_group("soundwave"+std::to_string(number)+(number==49?"D":""),group))event_sounds_.insert(std::make_pair(number,group));}
    for(std::size_t i=0;i<2;++i)flippers_.push_back(flipper::TimedFlipper(profile_.flippers[i]));
    for(std::size_t i=0;i<profile_.components.size();++i)
        for(std::size_t j=0;j<profile_.components[i].collider_indices.size();++j) {
            const std::size_t index=profile_.components[i].collider_indices[j];
            if(index>=colliders_.size()||component_for_collider_[index]!=(std::numeric_limits<std::size_t>::max)())
                throw std::runtime_error("invalid component collider ownership");
            component_for_collider_[index]=i;
        }
    const std::vector<table::ImageState>& frames=model.image_states(profile_.ball_group);
    for(std::size_t i=0;i<frames.size();++i) {
        const table::GroupInventory& group=model.group(frames[i].group_index);
        bool found=false;
        for(std::size_t r=0;r<group.float_records.size();++r) {
            const std::vector<float>& v=group.float_records[r].values;
            if(v.size()==4 && v[0]==501){art_depth_.push_back(visual::project(camera_,v[1],v[2],v[3]).depth);found=true;}
        }
        if(!found)throw std::runtime_error("missing ball size sample");
    }
    if(art_depth_.size()!=art_.size())throw std::runtime_error("ball size sample count");
    if(!std::is_sorted(art_depth_.begin(),art_depth_.end(),std::greater<double>()))throw std::runtime_error("ball depth thresholds out of order");
    for(std::size_t i=0;i<profile_.multiplier_bank.targets.size();++i)
        for(std::size_t j=0;j<profile_.multiplier_bank.targets[i].collider_indices.size();++j) {
            const std::size_t index=profile_.multiplier_bank.targets[i].collider_indices[j];
            if(index>=colliders_.size()||target_for_collider_[index]!=(std::numeric_limits<std::size_t>::max)()||
               component_for_collider_[index]!=(std::numeric_limits<std::size_t>::max)())
                throw std::runtime_error("invalid target collider ownership");
            target_for_collider_[index]=i;
        }
    for(std::size_t i=0;i<profile_.medal_bank.targets.size();++i)
        for(std::size_t j=0;j<profile_.medal_bank.targets[i].collider_indices.size();++j) {
            const std::size_t index=profile_.medal_bank.targets[i].collider_indices[j];
            if(index>=colliders_.size()||target_for_collider_[index]!=(std::numeric_limits<std::size_t>::max)())
                throw std::runtime_error("invalid medal target ownership");
            target_for_collider_[index]=i+3;
        }
    for(std::size_t r=0;r<profile_.rollovers.size();++r)for(unsigned side=0;side<2;++side) {
        const std::vector<std::size_t>& list=side?profile_.rollovers[r].exit_indices:profile_.rollovers[r].entry_indices;
        for(std::size_t j=0;j<list.size();++j) {
            const std::size_t index=list[j];
            if(index>=colliders_.size()||rollover_for_collider_[index]!=(std::numeric_limits<std::size_t>::max)())
                throw std::runtime_error("invalid rollover ownership");
            rollover_for_collider_[index]=r;rollover_exit_[index]=side!=0;
        }
    }
    for(std::size_t t=0;t<profile_.advanced.targets.size();++t)
        for(std::size_t index:profile_.advanced.targets[t].body.collider_indices){
            if(index>=colliders_.size())throw std::runtime_error("advanced target ownership out of range");
            advanced_target_for_collider_[index]=t;
        }
    if(profile_.advanced.has_hyperspace){
        hyperspace_kickout_=profile_.kickouts.size();profile_.kickouts.push_back(profile_.advanced.hyperspace);
        profile_.kickouts.back().delay=advanced_config(profile_.advanced).hyperspace_delays[0];kickout_cooldowns_.push_back(0);
    }
    if(profile_.advanced.has_gravity_well){
        gravity_kickout_=profile_.kickouts.size();profile_.kickouts.push_back(profile_.advanced.gravity_well.capture);
        profile_.kickouts.back().delay=26302LL*1000000000LL/11025LL;kickout_cooldowns_.push_back(0);
    }
    if(profile_.advanced.has_escape_chute)profile_.sinks.push_back(profile_.advanced.escape_chute);
    for(const reference::BankLampProfile& light:profile_.advanced.lights){
        const std::string& name=model.group(light.group).name;
        if(name.size()>4&&name.substr(0,4)=="lite"){
            std::uint32_t number=0;bool valid=true;
            for(std::size_t n=4;n<name.size();++n){if(name[n]<'0'||name[n]>'9'){valid=false;break;}number=number*10+static_cast<unsigned>(name[n]-'0');}
            if(valid)advanced_light_numbers_.insert(std::make_pair(light.group,number));
        }
    }
    for(unsigned number: {2U,3U,4U,5U,6U,7U,21U,22U,23U,24U}){
        std::ostringstream name;name<<"lite"<<number;
        advanced_light_numbers_.insert(std::make_pair(model.require_named_group(name.str()),number));
    }
    for(const reference::FlagSpinnerProfile& spinner:profile_.advanced.spinners)
        spinners_.push_back(rules::FlagSpinner(rules::SpinnerConfig(spinner.minimum_speed,spinner.maximum_speed,spinner.speed_scale,spinner.speed_decay,static_cast<unsigned>(spinner.image_count))));
    for(const reference::KickbackProfile& kick:profile_.advanced.kickbacks)
        kickbacks_.push_back(rules::Kickback(rules::KickbackConfig(kick.arm_delay,kick.pulse_period,kick.material.threshold,kick.armed_threshold,kick.idle_frame,kick.fired_frame,kick.reset_frame)));
    struct Family {const char* prefix;rules::MissionFeature feature;unsigned first,last;};
    const Family families[]={{"a_bump",rules::MissionFeature::Bumper,1,7},
        {"v_rebo",rules::MissionFeature::Rebound,1,4},{"a_targ",rules::MissionFeature::Target,1,22},
        {"a_roll",rules::MissionFeature::Rollover,1,9},{"a_roll",rules::MissionFeature::Rollover,110,112},
        {"v_sink",rules::MissionFeature::Sink,1,3}};
    for(const Family& family:families)for(unsigned n=family.first;n<=family.last;++n){
        std::ostringstream tag;tag<<family.prefix<<n;
        mission_events_.insert(std::make_pair(model.require_named_group(tag.str()),rules::MissionEvent(family.feature,n)));
    }
    mission_events_.insert(std::make_pair(model.require_named_group("a_kout2"),rules::MissionEvent(rules::MissionFeature::Hyperspace)));
    mission_events_.insert(std::make_pair(model.require_named_group("a_kout3"),rules::MissionEvent(rules::MissionFeature::BlackHole)));
    const std::vector<table::ImageState>& board=model.image_states(profile_.table_group);
    if(board.size()!=1)throw std::runtime_error("ambiguous board extent");
    table_width_=board[0].bitmap.width;table_height_=board[0].bitmap.height;
    tilt_counter_.tilt_no_more(0);advanced_actions(advanced_.serve(false,0),0);sync_advanced_colliders();if(finite_round_)cue(profile_.table_group,audio::SoundRole::TableStart,0);cue(profile_.plunger_group,audio::SoundRole::PlungerFeed,0);
}
PlayfieldTest::PlayfieldTest(const table::TableModel& model,physics::Vec2 position,physics::Vec2 velocity):PlayfieldTest(model,SessionMode::PhysicsTest) {
    ball_=motion::BallState(position,velocity,profile_.ball_radius);seated_=false;
}
PlayfieldTest::PlayfieldTest(const table::TableModel& model,const motion::BallState& state):
    PlayfieldTest(model,state,SessionMode::PhysicsTest) {}
PlayfieldTest::PlayfieldTest(const table::TableModel& model,const motion::BallState& state,SessionMode mode):PlayfieldTest(model,mode) {
    if(std::fabs(state.radius-profile_.ball_radius)>.000001f)throw std::invalid_argument("replay radius differs from table ball");
    motion::BallState validated=state;stepper_.step(validated,0,colliders_);ball_=validated;seated_=false;
}
void PlayfieldTest::set_replay_ball(const motion::BallState& state) {
    if(captured_||(finite_round_&&!game_rules_.snapshot().ball_active))
        throw std::logic_error("replay positioning requires an active unowned ball");
    if(std::fabs(state.radius-profile_.ball_radius)>.000001f)
        throw std::invalid_argument("replay radius differs from table ball");
    motion::BallState validated=state;stepper_.step(validated,0,colliders_);
    plunger_.cancel(time_);nudge_.cancel(time_);
    test_stick_x_=test_stick_y_=0;test_eject_remaining_=0;ball_=validated;seated_=false;
}
// Independent fixed-seed generator gives reproducible development runs.
// The reference uses CRT rand; sequence parity has not been established.
float PlayfieldTest::random_unit() {
    random_state_^=random_state_<<13;
    random_state_^=random_state_>>17;
    random_state_^=random_state_<<5;
    return static_cast<float>(random_state_>>8)/16777215.0f;
}
void PlayfieldTest::feed_ball() {
    unlimited_balls_=false;
    cue(profile_.plunger_group,audio::SoundRole::PlungerFeed,time_);
    ball_=motion::BallState(profile_.feed_position,physics::Vec2(),profile_.ball_radius);
    seated_=false;captured_=false;captured_kickout_=false;captured_hole_=false;sink_wait_=0;
}
physics::Vec2 PlayfieldTest::throw_velocity(physics::Vec2 direction,float angle,float scalar_speed,float variation) {
    const double a=double(angle)*(1-2*double(random_unit()));
    const double c=std::cos(a),s=std::sin(a);
    // Closed-form reference throw transform; current captures use zero angle.
    const double x=c*direction.x-s*direction.y;
    const double y=s*c*direction.x+(c-s*s)*direction.y;
    const double speed=double(scalar_speed)*(1+(1-2*double(random_unit()))*variation);
    return physics::Vec2(static_cast<float>(x*speed),static_cast<float>(y*speed));
}
void PlayfieldTest::eject_capture() {
    if(captured_kickout_) {
        cue(profile_.kickouts.at(captured_index_).group,audio::SoundRole::KickoutRelease,time_);
        const reference::KickoutProfile& kick=profile_.kickouts.at(captured_index_);
        ball_=motion::BallState(kick.center,throw_velocity(kick.direction,kick.angle_multiplier,kick.speed,kick.speed_variation),ball_.radius);
        ball_.height=captured_height_;ball_.collision_mask=captured_mask_;
        kickout_cooldowns_[captured_index_]=kick.cooldown;count(kickout_ejections_);
    } else {
        const reference::SinkProfile& sink=profile_.sinks.at(captured_index_);
        cue(sink.group,audio::SoundRole::SinkRelease,time_);
        ball_=motion::BallState(sink.position,throw_velocity(sink.direction,sink.angle_multiplier,sink.speed,sink.speed_variation),ball_.radius);
        count(sink_ejections_);
    }
    captured_=false;captured_kickout_=false;captured_hole_=false;sink_wait_=0;seated_=false;
    test_eject_remaining_=hidden_test_?physics_step:0;
}
void PlayfieldTest::reset() {
    sound_cues_.clear();sound_drops_=0;unlimited_balls_=cheats_used_=false;rank_flash_started_=rank_flash_duration_=0;rank_flash_index_=0;
    suspended_=false;time_=0;plunger_.reset();nudge_.reset();hole_drop_.reset();random_state_=0x95c0ffeeU;
    hidden_test_=false;test_eject_remaining_=0;test_stick_x_=test_stick_y_=0;game_rules_.reset();target_bank_.reset();medal_bank_.reset();progression_.reset();mission_.reset();advanced_.reset();mission_started_=0;next_serve_saved_=false;gravity_active_=false;tilt_counter_.reset();tilt_counter_.tilt_no_more(0);tilt_nudge_active_=false;
    for(std::size_t s=0;s<spinners_.size();++s)spinners_[s].reset();
    for(std::size_t k=0;k<kickbacks_.size();++k)kickbacks_[k].reset();
    for(std::size_t i=0;i<flippers_.size();++i)flippers_[i].reset();
    launches_=returns_=contacts_=exhausted_=flipper_contacts_=nudges_=moving_exhausted_=sink_captures_=sink_ejections_=kickout_captures_=kickout_ejections_=hole_captures_=hole_landings_=ramp_transitions_=0;
    std::fill(kickout_cooldowns_.begin(),kickout_cooldowns_.end(),runtime::Tick(0));
    bumper_hits_=rebound_hits_=target_hits_=bank_completions_=lane_hits_=lane_completions_=medal_hits_=medal_completions_=0;
    rollover_states_.assign(profile_.rollovers.size(),RolloverState());
    sync_target_colliders();
    component_states_.assign(profile_.components.size(),ComponentState());
    for(std::size_t i=0;i<profile_.components.size();++i)
        for(std::size_t j=0;j<profile_.components[i].collider_indices.size();++j)
            colliders_[profile_.components[i].collider_indices[j]].material=profile_.components[i].material;
    for(std::size_t i=0;i<profile_.kickouts.size();++i)colliders_[profile_.kickouts[i].collider_index].enabled=true;
    advanced_actions(advanced_.serve(false,0),0);sync_advanced_colliders();if(finite_round_)cue(profile_.table_group,audio::SoundRole::TableStart,0);feed_ball();
}
void PlayfieldTest::apply_nudge(const reference::NudgeDelta& delta) {
    // Settled shooter balls stand in for the reference's collision-ineligible
    // balls; other ordinary balls receive each contribution/inverse immediately.
    if(delta.changed && !seated_ && !captured_ && !hidden_test_ &&
       (!finite_round_||game_rules_.snapshot().ball_active))
        ball_.velocity=physics::add(ball_.velocity,physics::Vec2(delta.world_dx,delta.world_dy));
}
void PlayfieldTest::set_suspended(bool value) {
    if(value)set_test_stick(0,0);
    if(value && !suspended_) {
        plunger_.cancel(time_);apply_nudge(nudge_.cancel(time_));
        for(std::size_t i=0;i<flippers_.size();++i)flippers_[i].cancel(time_);
    }
    if(!value && suspended_)for(std::size_t i=0;i<flippers_.size();++i)flippers_[i].resume_released(time_);
    suspended_=value;
}
void PlayfieldTest::input(const runtime::InputTransition& edge) {
    if(edge.action==runtime::Action::NewGame){if(edge.pressed&&edge.cause==runtime::TransitionCause::Physical)reset();return;}
    if(edge.action==runtime::Action::HiddenTest) {
        if(edge.pressed&&edge.cause==runtime::TransitionCause::Physical) {
            plunger_.cancel(time_);apply_nudge(nudge_.cancel(time_));
            hidden_test_=!hidden_test_;if(hidden_test_)cheats_used_=true;test_stick_x_=test_stick_y_=0;test_eject_remaining_=0;
            if(!captured_)ball_.velocity=physics::Vec2();
            seated_=false;
        }
        return;
    }
    if(suspended_||(finite_round_&&game_rules_.snapshot().game_over))return;
    if(game_rules_.snapshot().tilted&&edge.pressed)return;
    if(edge.action==runtime::Action::LeftFlipper || edge.action==runtime::Action::RightFlipper) {
        const std::size_t i=edge.action==runtime::Action::LeftFlipper?0:1;
        flippers_[i].set_pressed(edge.pressed,time_,edge.cause);
        if(edge.cause==runtime::TransitionCause::Physical&&!game_rules_.snapshot().tilted)
            cue(profile_.flipper_groups[i],edge.pressed?audio::SoundRole::FlipperExtend:audio::SoundRole::FlipperRetract,time_);
        if(edge.pressed&&edge.cause==runtime::TransitionCause::Physical)
            fire_bumper_groups(progression_.rotate(i==0?1:-1,time_),time_);
        if(!edge.pressed && edge.cause!=runtime::TransitionCause::Physical)flippers_[i].resume_released(time_);
        return;
    }
    const runtime::Action actions[]={runtime::Action::NudgeLeft,runtime::Action::NudgeRight,runtime::Action::NudgeUp};
    const reference::NudgeSide sides[]={reference::NudgeSide::LeftButton,reference::NudgeSide::RightButton,reference::NudgeSide::Up};
    for(std::size_t i=0;i<3;++i)if(edge.action==actions[i]) {
        const bool before=nudge_.active(sides[i]);
        nudge_.set_pressed(sides[i],edge.pressed,time_,edge.cause);
        if(!before && nudge_.active(sides[i]))count(nudges_);
        apply_nudge(nudge_.advance(time_));return;
    }
    if(edge.action==runtime::Action::Plunger){
        const bool held=plunger_.held();plunger_.set_pressed(edge.pressed,time_,edge.cause);
        if(edge.cause==runtime::TransitionCause::Physical&&(edge.pressed||held))
            cue(profile_.plunger_group,edge.pressed?audio::SoundRole::PlungerPull:audio::SoundRole::PlungerRelease,time_);
    }
}
void PlayfieldTest::set_test_stick(float x,float y) {
    if(!std::isfinite(x)||!std::isfinite(y)||x<-1||x>1||y<-1||y>1)
        throw std::invalid_argument("test stick must be finite and normalized");
    if(!hidden_test_||suspended_||(finite_round_&&!game_rules_.snapshot().ball_active)){test_stick_x_=test_stick_y_=0;return;}
    test_stick_x_=x;test_stick_y_=y;
    if(x==0&&y==0&&!captured_&&test_eject_remaining_==0)ball_.velocity=physics::Vec2();
}
physics::Vec2 PlayfieldTest::test_velocity(runtime::Tick duration) const {
    if(test_stick_x_==0&&test_stick_y_==0)return physics::Vec2();
    const double seconds=double(duration)/runtime::ticks_per_second;
    double x=test_stick_x_,y=test_stick_y_;
    const double magnitude=std::sqrt(x*x+y*y);
    if(magnitude>1){x/=magnitude;y/=magnitude;}
    const visual::ProjectedPoint screen=visual::project(camera_,ball_.position.x,ball_.position.y,ball_.height);
    double world_x=0,world_y=0;
    if(!screen.in_front||!visual::unproject_plane(camera_,screen.x+x*200*seconds,
        screen.y-y*200*seconds,ball_.height,world_x,world_y))return physics::Vec2();
    return physics::Vec2(static_cast<float>((world_x-ball_.position.x)/seconds),
                         static_cast<float>((world_y-ball_.position.y)/seconds));
}
void PlayfieldTest::expire_components(runtime::Tick now) {
    for(std::size_t i=0;i<component_states_.size();++i) {
        ComponentState& state=component_states_[i];
        const reference::ComponentProfile& profile=profile_.components[i];
        std::size_t expired=0;
        while(expired<state.fired_at.size() && now-state.fired_at[expired]>=profile.duration)++expired;
        if(expired!=0) {
            state.active=false;
            state.fired_at.erase(state.fired_at.begin(),state.fired_at.begin()+expired);
            if(profile.kind==reference::HitComponentKind::Bumper)
                for(std::size_t j=0;j<profile.collider_indices.size();++j)
                    colliders_[profile.collider_indices[j]].material.threshold=profile.material.threshold;
        }
    }
}
runtime::Tick PlayfieldTest::component_boundary(runtime::Tick now) const {
    runtime::Tick boundary=(std::numeric_limits<runtime::Tick>::max)();
    for(std::size_t i=0;i<component_states_.size();++i) {
        // Also keep newly armed timers longer than any individual solve.
        boundary=(std::min)(boundary,profile_.components[i].duration);
        if(!component_states_[i].fired_at.empty()) {
            const runtime::Tick elapsed=now-component_states_[i].fired_at[0];
            boundary=(std::min)(boundary,(std::max)(runtime::Tick(0),profile_.components[i].duration-elapsed));
        }
    }
    return boundary;
}
void PlayfieldTest::component_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration) {
    if(game_rules_.snapshot().tilted)return;
    if(contact.moving||contact.transitioned||!contact.activated||contact.collider_index>=component_for_collider_.size())return;
    const std::size_t owner=component_for_collider_[contact.collider_index];
    if(owner==(std::numeric_limits<std::size_t>::max)())return;
    ComponentState& state=component_states_[owner];
    const reference::ComponentProfile& profile=profile_.components[owner];
    const runtime::Tick hit=validated_time(start,duration,contact.seconds_from_start);
    if(profile.kind==reference::HitComponentKind::Bumper){
        if(state.active)return;
        state.fired_at.clear();
    }
    state.active=true;
    // Identical deadlines have the same idempotent hide action.
    if(state.fired_at.empty()||state.fired_at.back()!=hit)state.fired_at.push_back(hit);
    if(profile.kind==reference::HitComponentKind::Bumper) {
        count(bumper_hits_);
        for(std::size_t j=0;j<profile.collider_indices.size();++j)
            colliders_[profile.collider_indices[j]].material.threshold=1000000000.0f;
    } else count(rebound_hits_);
    const std::size_t bank=bumper_bank(profile.group);
    if(profile.kind==reference::HitComponentKind::Bumper&&bank<2)
        ordinary_points(profile_.lane_groups[bank].bumper_scores[progression_.snapshot().groups[bank].level]);
    else award_group(profile.group);
    mission_group(profile.group,hit);
}
void PlayfieldTest::award_group(std::size_t group) {
    const std::map<std::size_t,std::uint32_t>::const_iterator found=profile_.rules.event_group_awards.find(group);
    if(found!=profile_.rules.event_group_awards.end())ordinary_points(found->second);
}
void PlayfieldTest::sync_target_colliders() {
    const rules::TargetBankSnapshot bank=target_bank_.snapshot();
    for(std::size_t i=0;i<profile_.multiplier_bank.targets.size();++i)
        for(std::size_t j=0;j<profile_.multiplier_bank.targets[i].collider_indices.size();++j)
            colliders_[profile_.multiplier_bank.targets[i].collider_indices[j]].enabled=bank.raised[i];
    const rules::TargetBankSnapshot medal=medal_bank_.snapshot();
    for(std::size_t i=0;i<profile_.medal_bank.targets.size();++i)
        for(std::size_t j=0;j<profile_.medal_bank.targets[i].collider_indices.size();++j)
            colliders_[profile_.medal_bank.targets[i].collider_indices[j]].enabled=medal.raised[i];
}
void PlayfieldTest::target_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration) {
    if(game_rules_.snapshot().tilted)return;
    if(contact.moving||contact.triggered||!contact.activated||contact.collider_index>=target_for_collider_.size())return;
    const std::size_t owner=target_for_collider_[contact.collider_index];
    if(owner==(std::numeric_limits<std::size_t>::max)())return;
    const bool medal=owner>=3;
    rules::TargetBank& bank=medal?medal_bank_:target_bank_;
    const rules::TargetHit hit=bank.hit(medal?owner-3:owner,validated_time(start,duration,contact.seconds_from_start));
    if(!hit.accepted)return;
    if(medal){
        ordinary_points(hit.base_points);count(medal_hits_);if(hit.completed)count(medal_completions_);
        if(hit.extra_ball&&game_rules_.award_extra_ball())control_cue(28,validated_time(start,duration,contact.seconds_from_start));
    }else{
        if(game_rules_.award_special(hit.points))advanced_.record_ordinary(hit.base_points);count(target_hits_);if(hit.completed)count(bank_completions_);
    }
    game_rules_.set_score_multiplier(target_bank_.snapshot().multiplier);sync_target_colliders();
    mission_group(medal?profile_.medal_bank.targets[owner-3].group:profile_.multiplier_bank.targets[owner].group,validated_time(start,duration,contact.seconds_from_start));
}
std::size_t PlayfieldTest::bumper_bank(std::size_t group) const {
    for(std::size_t b=0;b<2;++b)
        if(std::find(profile_.lane_groups[b].bumper_groups.begin(),profile_.lane_groups[b].bumper_groups.end(),group)!=
           profile_.lane_groups[b].bumper_groups.end())return b;
    return (std::numeric_limits<std::size_t>::max)();
}
void PlayfieldTest::fire_bumper_groups(std::uint32_t mask,runtime::Tick now) {
    for(std::size_t c=0;c<profile_.components.size();++c) {
        const std::size_t bank=bumper_bank(profile_.components[c].group);
        if(bank>=2||(mask&(1U<<bank))==0)continue;
        ComponentState& state=component_states_[c];state.active=true;state.fired_at.clear();state.fired_at.push_back(now);
        for(std::size_t j=0;j<profile_.components[c].collider_indices.size();++j)
            colliders_[profile_.components[c].collider_indices[j]].material.threshold=1000000000.0f;
    }
    for(unsigned bank=0;bank<2;++bank)if(mask&(1U<<bank))mission_event(rules::MissionEvent(rules::MissionFeature::LevelChange,bank),now);
}
void PlayfieldTest::expire_rollovers(runtime::Tick now) {
    for(std::size_t i=0;i<rollover_states_.size();++i) {
        RolloverState& s=rollover_states_[i];
        if(s.cooldown&&now-s.exited_at>=profile_.rollovers[i].rearm_delay)s.cooldown=false;
        if(s.delayed_hide&&now-s.exited_at>=profile_.rollovers[i].exit_hide_delay)s.delayed_hide=false;
    }
}
runtime::Tick PlayfieldTest::rollover_boundary(runtime::Tick now) const {
    runtime::Tick result=(std::numeric_limits<runtime::Tick>::max)();
    for(std::size_t i=0;i<rollover_states_.size();++i)if(rollover_states_[i].cooldown)
        result=(std::min)(result,(std::max)(runtime::Tick(0),profile_.rollovers[i].rearm_delay-(now-rollover_states_[i].exited_at)));
    for(std::size_t i=0;i<rollover_states_.size();++i)if(rollover_states_[i].delayed_hide)
        result=(std::min)(result,(std::max)(runtime::Tick(0),profile_.rollovers[i].exit_hide_delay-(now-rollover_states_[i].exited_at)));
    return result;
}
void PlayfieldTest::rollover_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration) {
    if(game_rules_.snapshot().tilted)return;
    if(contact.moving||!contact.triggered||contact.collider_index>=rollover_for_collider_.size())return;
    const std::size_t owner=rollover_for_collider_[contact.collider_index];
    if(owner==(std::numeric_limits<std::size_t>::max)())return;
    const runtime::Tick now=validated_time(start,duration,contact.seconds_from_start);
    RolloverState& state=rollover_states_[owner];const reference::RolloverProfile& region=profile_.rollovers[owner];
    if(rollover_exit_[contact.collider_index]){
        if(state.inside){state.inside=false;state.cooldown=true;state.delayed_hide=region.exit_hide_delay>0;state.exited_at=now;}
        return;
    }
    if(state.inside||(state.cooldown&&now-state.exited_at<=region.rearm_delay))return;
    state.inside=true;state.delayed_hide=false;cue(region.group,audio::SoundRole::SoftImpact,now);mission_group(region.group,now);
    for(std::size_t bank=0;bank<2;++bank)for(std::size_t lane=0;lane<3;++lane)
        if(profile_.lane_groups[bank].rollover_groups[lane]==region.group){
            count(lane_hits_);const rules::LaneHit event=progression_.hit(bank,lane,now);
            ordinary_points(profile_.lane_groups[bank].rollover_award);
            if(event.completed)count(lane_completions_);fire_bumper_groups(event.level_changed_mask,now);return;
        }
    for(const reference::AdvancedRolloverProfile& role:profile_.advanced.rollovers)if(role.region_index==owner){
        advanced_actions(advanced_.rollover(role.number,now),now);
        if(role.number==184)mission_event(rules::MissionEvent(rules::MissionFeature::FuelTop),now);
        return;
    }
}
void PlayfieldTest::reset_progression(runtime::Tick now) {
    progression_.ordinary_loss(now);
    rollover_states_.assign(profile_.rollovers.size(),RolloverState());
    for(std::size_t i=0;i<profile_.components.size();++i)if(bumper_bank(profile_.components[i].group)<2) {
        component_states_[i]=ComponentState();
        for(std::size_t j=0;j<profile_.components[i].collider_indices.size();++j)
            colliders_[profile_.components[i].collider_indices[j]].material=profile_.components[i].material;
    }
}
void PlayfieldTest::cue(std::size_t group,audio::SoundRole role,runtime::Tick now) {
    if(group==(std::numeric_limits<std::size_t>::max)())return;
    if(sound_cues_.size()<256)sound_cues_.push_back(audio::SoundCue(group,role,now));else count(sound_drops_);
}
void PlayfieldTest::control_cue(unsigned number,runtime::Tick now) {
    const std::map<unsigned,std::size_t>::const_iterator found=event_sounds_.find(number);
    if(found!=event_sounds_.end())cue(found->second,audio::SoundRole::Resource,now);
}
void PlayfieldTest::drain_sounds(std::vector<audio::SoundCue>& destination) {destination.clear();destination.swap(sound_cues_);}
std::uint32_t PlayfieldTest::sound_cue_drops() const {return sound_drops_;}
void PlayfieldTest::ordinary_points(std::uint32_t base) {
    if(game_rules_.award(base))advanced_.record_ordinary(base);
}
std::uint32_t PlayfieldTest::source_mission_state() const {
    const rules::MissionSnapshot state=mission_.snapshot();
    if(state.phase==rules::MissionPhase::Waiting)return 0;
    if(state.phase==rules::MissionPhase::Selecting)return 1;
    if(state.phase==rules::MissionPhase::GameOver)return 32;
    if(state.active==rules::MissionId::Secret&&state.stage)return state.stage==1?22:23;
    if(state.active==rules::MissionId::AlienMenace&&state.stage)return 20;
    if(state.active==rules::MissionId::CosmicPlague&&state.stage)return 21;
    if(state.active==rules::MissionId::TimeWarp&&state.stage)return 24;
    if(state.active==rules::MissionId::Maelstrom&&state.stage)return 24+state.stage;
    return static_cast<std::uint32_t>(state.active);
}
void PlayfieldTest::advanced_actions(const rules::AdvancedActions& action,runtime::Tick now) {
    if(action.sound_control)control_cue(action.sound_control,now);
    if(action.ordinary_points){if(action.ordinary_recorded)game_rules_.award(action.ordinary_points);else ordinary_points(action.ordinary_points);}
    if(action.special_points)game_rules_.award_special(action.special_points);
    if(action.extra_ball&&game_rules_.award_extra_ball())control_cue(28,now);
    if(action.replay)game_rules_.award_replay();
    if(action.enable_gravity_well)gravity_active_=true;
    if(action.set_multiplier_max){target_bank_.set_tier(4,now);game_rules_.set_score_multiplier(10);}
    if(action.set_medals_max)medal_bank_.set_tier(3,now,false);
    if(finite_round_){
        const rules::MissionPhase previous=mission_.snapshot().phase;
        const rules::MissionActions fuel=mission_.fuel_available(!advanced_.snapshot().fuel_empty);
        if(fuel.failed||previous!=mission_.snapshot().phase){advanced_.clear_spots(2);advanced_.mission_flags(false);advanced_.mission_hyperspace(false,false);mission_started_=now;}
        for(unsigned flag=0;flag<2;++flag)if(action.flag_hits&(1U<<flag))mission_event(rules::MissionEvent(rules::MissionFeature::Flag,flag+1),now);
    }
    advanced_.set_mission_state(source_mission_state());sync_target_colliders();sync_advanced_colliders();
}
void PlayfieldTest::sync_advanced_colliders() {
    const rules::AdvancedSnapshot state=advanced_.snapshot();
    for(const reference::AdvancedTargetProfile& target:profile_.advanced.targets)
        for(std::size_t index:target.body.collider_indices)colliders_.at(index).enabled=state.target_enabled.at(target.number);
    for(const reference::AdvancedRolloverProfile& rollover:profile_.advanced.rollovers){
        const reference::RolloverProfile& region=profile_.rollovers.at(rollover.region_index);
        for(std::size_t index:region.entry_indices)colliders_.at(index).enabled=true;
        for(std::size_t index:region.exit_indices)colliders_.at(index).enabled=true;
    }
    for(const reference::SkillTripwireProfile& trip:profile_.advanced.skill_tripwires)colliders_.at(trip.collider_index).enabled=true;
    for(const reference::DeploymentGateProfile& gate:profile_.advanced.deployment_gates)colliders_.at(gate.collider_index).enabled=true;
    for(const reference::FlagSpinnerProfile& spinner:profile_.advanced.spinners){colliders_.at(spinner.forward_index).enabled=spinner.default_enabled;colliders_.at(spinner.reverse_index).enabled=spinner.default_enabled;}
    for(const reference::OneWayProfile& one_way:profile_.advanced.one_ways)colliders_.at(one_way.solid_index).enabled=one_way.default_enabled;
    if(profile_.advanced.has_escape_chute)colliders_.at(profile_.advanced.escape_chute.collider_index).enabled=true;
    std::size_t gate=0;
    for(const reference::DynamicWallProfile& wall:profile_.advanced.dynamic_walls){
        const bool enabled=wall.kind==reference::DynamicWallKind::Gate?!state.gates_open.at(gate++):state.blocker_on;
        for(std::size_t index:wall.collider_indices)colliders_.at(index).enabled=enabled;
    }
    for(std::size_t k=0;k<kickbacks_.size();++k){
        const reference::KickbackProfile& kick=profile_.advanced.kickbacks[k];
        colliders_.at(kick.collider_index).enabled=kick.default_enabled;
        colliders_.at(kick.collider_index).material.threshold=kickbacks_[k].snapshot().threshold;
    }
    for(std::size_t i=0;i<profile_.kickouts.size();++i){
        const bool held=captured_&&captured_kickout_&&captured_index_==i;
        colliders_.at(profile_.kickouts[i].collider_index).enabled=!held&&kickout_cooldowns_[i]==0&&(i!=gravity_kickout_||gravity_active_);
    }
}
void PlayfieldTest::advanced_target_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration) {
    if(game_rules_.snapshot().tilted)return;
    if(contact.moving||contact.triggered||!contact.activated||contact.collider_index>=advanced_target_for_collider_.size())return;
    const std::size_t owner=advanced_target_for_collider_[contact.collider_index];
    if(owner==(std::numeric_limits<std::size_t>::max)())return;
    const runtime::Tick now=validated_time(start,duration,contact.seconds_from_start);
    const reference::AdvancedTargetProfile& target=profile_.advanced.targets[owner];
    const rules::AdvancedActions action=advanced_.target(target.number,now);advanced_actions(action,now);
    if(action.accepted)mission_event(rules::MissionEvent(rules::MissionFeature::Target,static_cast<unsigned>(target.number)),now);
}
void PlayfieldTest::advanced_sensor_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration) {
    if(contact.moving||!contact.triggered)return;
    const runtime::Tick now=validated_time(start,duration,contact.seconds_from_start);
    if(game_rules_.snapshot().tilted){
        for(std::size_t s=0;s<spinners_.size();++s){const reference::FlagSpinnerProfile& spin=profile_.advanced.spinners[s];
            if(contact.collider_index==spin.forward_index||contact.collider_index==spin.reverse_index)
                spinners_[s].hit(contact.incoming_speed,contact.collider_index==spin.reverse_index,now);
        }
        return;
    }
    for(const reference::SkillTripwireProfile& trip:profile_.advanced.skill_tripwires)if(trip.collider_index==contact.collider_index){if(advanced_.snapshot().skill_armed)control_cue(14,now);advanced_actions(advanced_.skill_tripwire(trip.number+1,now),now);return;}
    for(const reference::DeploymentGateProfile& gate:profile_.advanced.deployment_gates)if(gate.collider_index==contact.collider_index){
        if(gate.number==4){const rules::AdvancedActions action=advanced_.skill_award(now);if(action.ordinary_points)control_cue(3,now);advanced_actions(action,now);}
        mission_event(rules::MissionEvent(rules::MissionFeature::LaunchExit),now);return;
    }
    for(std::size_t s=0;s<spinners_.size();++s){
        const reference::FlagSpinnerProfile& spinner=profile_.advanced.spinners[s];
        if(contact.collider_index==spinner.forward_index||contact.collider_index==spinner.reverse_index){
            spinner_events(s,spinners_[s].hit(contact.incoming_speed,contact.collider_index==spinner.reverse_index,now),now);return;
        }
    }
}
void PlayfieldTest::spinner_events(std::size_t spinner,const rules::SpinnerEvents& events,runtime::Tick now) {
    if(events.frames==0||game_rules_.snapshot().tilted)return;
    const rules::SpinnerSnapshot state=spinners_.at(spinner).snapshot();
    const unsigned frames=static_cast<unsigned>(profile_.advanced.spinners.at(spinner).image_count);
    unsigned frame=state.direction>0?(state.frame+frames-events.frames%frames)%frames:(state.frame+events.frames%frames)%frames;
    for(std::uint32_t i=0;i<events.frames;++i){
        frame=state.direction>0?(frame+1)%frames:(frame+frames-1)%frames;
        cue(profile_.advanced.spinners[spinner].group,audio::SoundRole::SoftImpact,now);
        advanced_actions(advanced_.flag_hit(spinner,frame==0,now),now);
    }
}
rules::MissionContext PlayfieldTest::mission_context() const {
    rules::MissionContext context;const rules::ProgressionSnapshot p=progression_.snapshot();
    context.attack_level=p.groups[0].level;context.launch_level=p.groups[1].level;context.booster_lit=advanced_.snapshot().flags_active;
    return context;
}
void PlayfieldTest::mission_group(std::size_t group,runtime::Tick now) {
    const std::map<std::size_t,rules::MissionEvent>::const_iterator found=mission_events_.find(group);
    if(found!=mission_events_.end())mission_event(found->second,now);
}
void PlayfieldTest::mission_event(const rules::MissionEvent& event,runtime::Tick now) {
    if(!finite_round_||game_rules_.snapshot().tilted)return;
    const rules::MissionSnapshot previous_state=mission_.snapshot();
    const rules::MissionPhase previous_phase=previous_state.phase;
    const rules::MissionActions action=mission_.event(event,mission_context());
    if(action.special_points)game_rules_.award_special(action.special_points);
    if(action.rank_changed)cue(rank_sound_,audio::SoundRole::Resource,now);
    else if(action.completed&&!(previous_state.rank==9&&mission_.snapshot().rank_progress<previous_state.rank_progress))cue(mission_sound_,audio::SoundRole::Resource,now);
    if(action.stage_changed)mission_started_=now;
    if(action.started&&mission_.snapshot().active==rules::MissionId::Rescue)advanced_.mission_flags(false);
    std::uint32_t changed=0;
    if(action.reset_attack)changed|=progression_.set_level(0,0,now);
    if(action.reset_launch)changed|=progression_.set_level(1,0,now);
    if(changed)fire_bumper_groups(changed,now);
    if(action.rearm_popups){target_bank_.request_rearm(now);medal_bank_.request_rearm(now);advanced_.request_booster_rearm(now);}
    if(action.clear_spot_families)advanced_.clear_spots(action.clear_spot_families);
    if(previous_phase==rules::MissionPhase::Active&&mission_.snapshot().phase==rules::MissionPhase::Selecting)advanced_.clear_spots(2);
    const rules::MissionSnapshot next=mission_.snapshot();
    const auto flag_stage=[](const rules::MissionSnapshot& state){return state.phase==rules::MissionPhase::Active&&
        ((state.active==rules::MissionId::CosmicPlague&&state.stage==0)||(state.active==rules::MissionId::Maelstrom&&state.stage==5));};
    if(flag_stage(previous_state)!=flag_stage(next))advanced_.mission_flags(flag_stage(next));
    if(action.stage_changed||action.completed){
        advanced_.mission_hyperspace(next.phase==rules::MissionPhase::Active&&next.active==rules::MissionId::TimeWarp&&next.stage==1,
            next.phase==rules::MissionPhase::Active&&next.active==rules::MissionId::Maelstrom&&next.stage==7);
    }
    if(event.feature==rules::MissionFeature::Target&&event.index>=13&&event.index<=15&&next.phase==rules::MissionPhase::Selecting&&next.selection_mask==0)
        advanced_.consume_mission_selection();
    advanced_.set_mission_state(source_mission_state());sync_advanced_colliders();
}
void PlayfieldTest::kickback_events(std::size_t owner,const rules::KickbackEvents& events,runtime::Tick now) {
    if(events.due_remaining)throw std::runtime_error("kickback callback budget exhausted");
    for(std::size_t pulse=0;pulse<events.pulses;++pulse)cue(profile_.advanced.kickbacks.at(owner).group,audio::SoundRole::HardImpact,now);
    if(events.gate_close){
        std::size_t gate=0;
        for(const reference::DynamicWallProfile& wall:profile_.advanced.dynamic_walls)if(wall.kind==reference::DynamicWallKind::Gate){
            if(wall.group==profile_.advanced.kickbacks.at(owner).gate_group){advanced_actions(advanced_.close_gate(gate,now),now);break;}
            ++gate;
        }
    }
    sync_advanced_colliders();
}
void PlayfieldTest::kickback_hit(const motion::MotionContact& contact,runtime::Tick start,runtime::Tick duration) {
    if(contact.moving||contact.triggered)return;
    const runtime::Tick now=validated_time(start,duration,contact.seconds_from_start);
    for(std::size_t k=0;k<kickbacks_.size();++k)if(contact.collider_index==profile_.advanced.kickbacks[k].collider_index){
        kickback_events(k,kickbacks_[k].collision(contact.activated,now),now);return;
    }
}
void PlayfieldTest::settle_drain(runtime::Tick now,runtime::Tick inactive) {
    if(finite_round_&&unlimited_balls_){
        captured_=true;captured_kickout_=captured_hole_=false;captured_index_=2;seated_=false;
        ball_.velocity=physics::Vec2();ball_.collision_mask=1;ball_.height=ball_.radius;ball_.plane=motion::SurfacePlane();
        plunger_.cancel(now);apply_nudge(nudge_.cancel(now));tilt_nudge_active_=false;
        sink_wait_=(std::max)(runtime::Tick(0),profile_.sinks[2].delay-inactive);return;
    }
    captured_=captured_kickout_=captured_hole_=false;seated_=false;sink_wait_=0;
    ball_.velocity=physics::Vec2();plunger_.cancel(now);nudge_.cancel(now);tilt_nudge_active_=false;
    test_stick_x_=test_stick_y_=0;test_eject_remaining_=0;count(returns_);
    if(finite_round_){
        if(!game_rules_.snapshot().tilted)game_rules_.set_bonus(advanced_.snapshot().bonus_pool);
        const rules::DrainResult loss=game_rules_.drain(now);next_serve_saved_=loss.saved;
        if(loss.accepted){if(!loss.extra_ball_used)cue(drain_sound_,audio::SoundRole::Resource,now);if(loss.saved||loss.extra_ball_used)control_cue(59,now);}
        if(loss.accepted&&!loss.saved){
            target_bank_.ordinary_loss(now);medal_bank_.ordinary_loss(now);reset_progression(now);mission_.ordinary_loss();
            advanced_actions(advanced_.ordinary_loss(now),now);gravity_active_=false;
            for(std::size_t s=0;s<spinners_.size();++s)spinners_[s].reset(now);
            for(std::size_t k=0;k<kickbacks_.size();++k)kickbacks_[k].reset(now);
            game_rules_.clear_tilt(now);tilt_counter_.clear_lock(now);
            for(std::size_t i=0;i<colliders_.size();++i)colliders_[i].material=profile_.static_colliders[i].material;
            game_rules_.set_score_multiplier(1);sync_target_colliders();sync_advanced_colliders();
        }
    }else {cue(drain_sound_,audio::SoundRole::Resource,now);feed_ball();}
}
void PlayfieldTest::update_tilt(runtime::Tick now) {
    const rules::TiltEvents event=tilt_counter_.advance(now,tilt_nudge_active_,finite_round_&&game_rules_.snapshot().ball_active);
    if(event.tilted){
        cue(profile_.table_group,audio::SoundRole::TableTilt,now);
        unlimited_balls_=false;game_rules_.tilt(now);gravity_active_=false;plunger_.cancel(now);
        for(std::size_t f=0;f<flippers_.size();++f)flippers_[f].set_pressed(false,now,runtime::TransitionCause::Physical);
        for(std::size_t k=0;k<kickbacks_.size();++k)kickbacks_[k].set_tilted(true,now);
    }
    if(event.force_drain&&finite_round_&&game_rules_.snapshot().ball_active)settle_drain(now);
}
void PlayfieldTest::update_rules(runtime::Tick now) {
    target_bank_.advance(now);medal_bank_.advance(now);
    advanced_actions(advanced_.advance(now),now);
    for(std::size_t s=0;s<spinners_.size();++s)spinner_events(s,spinners_[s].advance(now),now);
    for(std::size_t k=0;k<kickbacks_.size();++k)kickback_events(k,kickbacks_[k].advance(now),now);
    fire_bumper_groups(progression_.advance(now),now);expire_rollovers(now);
    game_rules_.set_score_multiplier(target_bank_.snapshot().multiplier);
    sync_target_colliders();
    const rules::RulesEvents events=game_rules_.advance(now);
    if(finite_round_&&events.serve_ball){advanced_actions(advanced_.serve(next_serve_saved_,now),now);next_serve_saved_=false;game_rules_.clear_tilt(now);tilt_counter_.tilt_no_more(now);feed_ball();}
    if(finite_round_&&events.game_over){mission_.game_over();cue(profile_.table_group,audio::SoundRole::TableGameOver,now);}
    update_tilt(now);
}
void PlayfieldTest::step(runtime::Tick duration) {
    const runtime::Tick start=time_,end=start+duration;
    expire_components(start);update_rules(start);
    apply_nudge(nudge_.advance(start));tilt_nudge_active_=nudge_.active();
    if(finite_round_&&!game_rules_.snapshot().ball_active) {
        for(std::size_t i=0;i<flippers_.size();++i)flippers_[i].update(end);
        for(std::size_t i=0;i<kickout_cooldowns_.size();++i)
            kickout_cooldowns_[i]=kickout_cooldowns_[i]>duration?kickout_cooldowns_[i]-duration:0;
        expire_components(end);time_=end;plunger_.advance(end);nudge_.advance(end);update_rules(end);
        return;
    }
    if(captured_) {
        if(captured_hole_) {
            const bool landed=hole_drop_.advance(end);ball_.height=hole_drop_.height();
            if(landed) {
                const reference::HoleProfile& hole=profile_.holes[captured_index_];
                ball_.collision_mask=hole.landing_mask;ball_.plane=motion::SurfacePlane();ball_.velocity=physics::Vec2();
                captured_=false;captured_hole_=false;count(hole_landings_);cue(hole.group,audio::SoundRole::HoleLanding,end);game_rules_.hole_release(end);
            }
        } else {
            if(duration>sink_wait_)throw std::logic_error("capture deadline was not split");
            sink_wait_-=duration;
        }
        for(std::size_t i=0;i<kickout_cooldowns_.size();++i)
            kickout_cooldowns_[i]=kickout_cooldowns_[i]>duration?kickout_cooldowns_[i]-duration:0;
        for(std::size_t i=0;i<flippers_.size();++i)flippers_[i].update(end);
        expire_components(end);
        time_=end;plunger_.advance(end);apply_nudge(nudge_.advance(end));
        if(captured_&&!captured_hole_&&sink_wait_==0)eject_capture();
        update_rules(end);return;
    }
    const runtime::Tick window=plunger_.window_remaining(start);
    motion::StaticCollider& striker=colliders_[profile_.plunger_collider_index];
    striker.material.threshold=window?0.0f:1e9f;
    striker.material.boost=plunger_.boost(start,window?random_unit():0.0f);
    colliders_[profile_.rules.drain_collider_index].enabled=!hidden_test_;
    for(std::size_t i=0;i<profile_.kickouts.size();++i)
        colliders_[profile_.kickouts[i].collider_index].enabled=kickout_cooldowns_[i]==0&&(i!=gravity_kickout_||gravity_active_);
    if(hidden_test_) {
        // Velocity control still passes through the real collision solver.
        // Carry a component throw for one free step so it clears its sensor.
        if(test_eject_remaining_>0)test_eject_remaining_=(std::max)(runtime::Tick(0),test_eject_remaining_-duration);
        else ball_.velocity=test_velocity(duration);
    } else {
    response::FieldConfig field=profile_.field;
    if(ball_.plane.active) {
        field.gravity=physics::add(field.gravity,ball_.plane.field_force);
        field.drag_per_second+=ball_.plane.drag;
    }
    for(std::size_t i=0;i<profile_.kickouts.size();++i) {
        const reference::KickoutProfile& kick=profile_.kickouts[i];
        const bool active=kickout_cooldowns_[i]==0&&(i!=gravity_kickout_||gravity_active_);
        const physics::Vec2 delta=physics::subtract(kick.center,ball_.position);const float distance=physics::length(delta);
        if(active && (ball_.collision_mask&colliders_[kick.collider_index].collision_mask) && distance<=kick.field_radius) {
            if(distance>0)field.gravity=physics::add(field.gravity,physics::multiply(delta,kick.field_strength/distance));
            field.drag_per_second+=1;
        }
    }
    for(std::size_t i=0;i<profile_.holes.size();++i) {
        const reference::HoleProfile& hole=profile_.holes[i];
        const physics::Vec2 delta=physics::subtract(hole.center,ball_.position);const float distance=physics::length(delta);
        if((ball_.collision_mask&hole.collision_mask) && distance<=hole.field_radius) {
            if(distance>0)field.gravity=physics::add(field.gravity,physics::multiply(delta,hole.field_strength/distance));
            field.drag_per_second+=1;
        }
    }
    ball_.velocity=response::advance_field(ball_.velocity,double(duration)/runtime::ticks_per_second,field,2*random_unit()-1);
    }
    std::vector<flipper::FlipperMotion> moving;
    for(std::size_t i=0;i<flippers_.size();++i)moving.push_back(flippers_[i].motion());
    if(game_rules_.snapshot().tilted)for(std::size_t i=0;i<colliders_.size();++i)if(colliders_[i].reference_response){
        bool kick=false;for(const reference::KickbackProfile& owner:profile_.advanced.kickbacks)if(owner.collider_index==i)kick=true;
        if(!kick){colliders_[i].material.threshold=1000000000.0f;colliders_[i].material.boost=0;}
    }
    const motion::MotionStep result=stepper_.step(ball_,duration,colliders_,moving,
        flipper::MovingSweepConfig(512,32768,.00002,.00000001));
    count(contacts_,result.contacts.size());
    for(std::size_t i=0;i<result.contacts.size();++i) {
        const motion::MotionContact& c=result.contacts[i];
        const runtime::Tick sound_time=validated_time(start,duration,c.seconds_from_start);
        if(!c.moving&&!c.triggered&&c.collider_index!=profile_.plunger_collider_index&&c.incident_normal_speed>.2f&&!game_rules_.snapshot().tilted)
            cue(profile_.collider_groups.at(c.collider_index),c.activated?audio::SoundRole::HardImpact:audio::SoundRole::SoftImpact,sound_time);
        if(c.triggered&&!c.moving&&!game_rules_.snapshot().tilted){
            bool gate=c.collider_index==profile_.rules.skill_gate_collider_index;
            for(const reference::DeploymentGateProfile& d:profile_.advanced.deployment_gates)if(d.collider_index==c.collider_index)gate=true;
            if(gate)cue(profile_.collider_groups.at(c.collider_index),audio::SoundRole::HardImpact,sound_time);
        }
        component_hit(c,start,duration);target_hit(c,start,duration);advanced_target_hit(c,start,duration);rollover_hit(c,start,duration);advanced_sensor_hit(c,start,duration);kickback_hit(c,start,duration);
        if(c.triggered&&!c.moving&&!game_rules_.snapshot().tilted) {
            const runtime::Tick hit=validated_time(start,duration,c.seconds_from_start);
            if(c.collider_index==profile_.rules.skill_gate_collider_index){if(advanced_.snapshot().skill_armed)control_cue(14,hit);game_rules_.skill_gate(hit);advanced_actions(advanced_.skill_gate(hit),hit);}
            else if(c.collider_index==profile_.rules.skill_clear_collider_index){game_rules_.skill_clear(hit);advanced_actions(advanced_.skill_clear(hit),hit);}
            else if(c.collider_index==profile_.rules.ramp_score_collider_index) {
                const bool reflex=game_rules_.snapshot().reflex_active;
                const std::uint32_t base=profile_.rules.event_group_awards.at(profile_.rules.ramp_score_group);
                const std::uint32_t awarded=game_rules_.award_ramp(base,hit);
                if(awarded&&!reflex)advanced_.record_ordinary(base);
                const rules::MissionSnapshot mission_audio=mission_.snapshot();
                const bool ramp_combo=mission_audio.active==rules::MissionId::TimeWarp&&mission_audio.stage==1;
                control_cue(mission_audio.selected!=rules::MissionId::None?24U:ramp_combo?23U:reflex?21U:30U,hit);
                mission_event(rules::MissionEvent(rules::MissionFeature::Ramp),hit);
            }
        }
        if(c.transitioned)count(ramp_transitions_);
        else if(c.moving)count(flipper_contacts_);
        else if(c.collider_index==profile_.plunger_collider_index && c.boost_applied)count(launches_);
    }
    if(result.captured) {
        bool found=false;const runtime::Tick hit_time=validated_time(start,duration,result.simulated_seconds);
        const runtime::Tick inactive=end-hit_time;
        bool tilted_sink=false;
        if(game_rules_.snapshot().tilted)for(const reference::SinkProfile& sink:profile_.sinks)
            if(sink.collider_index==result.capture_collider_index)tilted_sink=true;
        if(result.capture_collider_index==profile_.rules.drain_collider_index||tilted_sink){settle_drain(hit_time,inactive);found=true;}
        if(!found)for(std::size_t i=0;i<profile_.sinks.size();++i)if(profile_.sinks[i].collider_index==result.capture_collider_index) {
            captured_=true;captured_kickout_=false;captured_hole_=false;captured_index_=i;seated_=false;count(sink_captures_);
            cue(profile_.sinks[i].group,audio::SoundRole::SinkCapture,hit_time);
            if(i<3){
                const rules::AdvancedActions routing=advanced_.wormhole(i,hit_time);advanced_actions(routing,hit_time);
                mission_group(profile_.sinks[i].group,hit_time);
                if(routing.wormhole_destination>=0)captured_index_=static_cast<std::size_t>(routing.wormhole_destination);
            }
            sink_wait_=(std::max)(runtime::Tick(0),profile_.sinks[i].delay-inactive);found=true;break;
        }
        if(!found)for(std::size_t i=0;i<profile_.kickouts.size();++i)if(profile_.kickouts[i].collider_index==result.capture_collider_index) {
            captured_=true;captured_kickout_=true;captured_hole_=false;captured_index_=i;seated_=false;count(kickout_captures_);
            if(!game_rules_.snapshot().tilted)cue(profile_.kickouts[i].group,audio::SoundRole::KickoutCapture,hit_time);
            runtime::Tick capture_delay=profile_.kickouts[i].delay;
            if(game_rules_.snapshot().tilted)capture_delay=100000000;
            else if(i==hyperspace_kickout_){
                const rules::AdvancedActions action=advanced_.hyperspace(hit_time);advanced_actions(action,hit_time);
                capture_delay=action.hyperspace_delay;
            }else if(i==gravity_kickout_){control_cue(7,hit_time);ordinary_points(profile_.advanced.gravity_well.score);gravity_active_=false;}
            else award_group(profile_.kickouts[i].group);
            mission_group(profile_.kickouts[i].group,hit_time);
            captured_height_=ball_.height;captured_mask_=ball_.collision_mask;ball_.position=profile_.kickouts[i].center;
            ball_.height=profile_.kickouts[i].held_z;
            sink_wait_=(std::max)(runtime::Tick(0),capture_delay-inactive);
            colliders_[result.capture_collider_index].enabled=false;found=true;break;
        }
        if(!found)for(std::size_t i=0;i<profile_.holes.size();++i)if(profile_.holes[i].collider_index==result.capture_collider_index) {
            captured_=true;captured_kickout_=false;captured_hole_=true;captured_index_=i;seated_=false;count(hole_captures_);
            if(!game_rules_.snapshot().tilted)cue(profile_.holes[i].group,audio::SoundRole::HoleCapture,hit_time);
            ball_.position=profile_.holes[i].center;ball_.plane=motion::SurfacePlane();
            hole_drop_.capture(ball_.height,profile_.holes[i].landing_z,hit_time);
            hole_drop_.advance(end);ball_.height=hole_drop_.height();found=true;break;
        }
        if(!found)throw std::logic_error("unmapped capture component");
    }
    if(result.contact_budget_exhausted || result.moving_work_exhausted) {
        count(exhausted_);if(result.moving_work_exhausted)count(moving_exhausted_);
        const runtime::Tick verified=validated_time(start,duration,result.simulated_seconds);
        for(std::size_t i=0;i<flippers_.size();++i) {
            runtime::Tick stop=verified;
            for(std::size_t y=0;y<result.flipper_yields.size();++y)if(result.flipper_yields[y].collider_index==i)
                stop=(std::min)(stop,validated_time(start,duration,result.flipper_yields[y].seconds_from_start));
            flippers_[i].cancel(stop);
        }
    } else {
        for(std::size_t i=0;i<flippers_.size();++i) {
            bool yielded=false;
            for(std::size_t y=0;y<result.flipper_yields.size();++y)if(result.flipper_yields[y].collider_index==i) {
                flippers_[i].commit_yield(validated_time(start,duration,result.flipper_yields[y].seconds_from_start),end);yielded=true;break;
            }
            if(!yielded)flippers_[i].update(end);
        }
    }
    for(std::size_t i=0;i<flippers_.size();++i)flippers_[i].update(end);
    for(std::size_t i=0;i<kickout_cooldowns_.size();++i)
        kickout_cooldowns_[i]=kickout_cooldowns_[i]>duration?kickout_cooldowns_[i]-duration:0;
    expire_components(end);
    time_=end;plunger_.advance(end);apply_nudge(nudge_.advance(end));
    if(captured_&&!captured_hole_&&sink_wait_==0)eject_capture();
    seated_=!hidden_test_ && !captured_ && (ball_.collision_mask&1u) && !ball_.plane.active &&
        std::fabs(ball_.position.y-profile_.seat_position.y)<.02f && std::fabs(ball_.position.x-profile_.seat_position.x)<.7f && physics::length(ball_.velocity)<.5f;
    if(!finite_round_ && !hidden_test_ && !captured_ && (ball_.collision_mask&1u) && ball_.position.y>drain_y_){count(returns_);feed_ball();}
    update_rules(end);
}
void PlayfieldTest::advance(runtime::Tick duration) {
    if((static_cast<std::uint64_t>(duration)>>63)!=0 || duration>runtime::ticks_per_second)
        throw std::invalid_argument("playfield duration must be within one second");
    if(suspended_)return;
    if(time_>(std::numeric_limits<runtime::Tick>::max)()-duration)
        throw std::overflow_error("playfield clock overflow");
    while(duration>0) {
        expire_components(time_);update_rules(time_);
        runtime::Tick part=(std::min)(duration,physics_step);
        part=(std::min)(part,component_boundary(time_));
        part=(std::min)(part,target_bank_.until_boundary());
        part=(std::min)(part,medal_bank_.until_boundary());
        part=(std::min)(part,progression_.until_boundary());
        part=(std::min)(part,advanced_.until_boundary());
        part=(std::min)(part,tilt_counter_.until_boundary());
        for(std::size_t s=0;s<spinners_.size();++s)part=(std::min)(part,spinners_[s].until_boundary());
        for(std::size_t k=0;k<kickbacks_.size();++k)part=(std::min)(part,kickbacks_[k].until_boundary());
        part=(std::min)(part,rollover_boundary(time_));
        const runtime::Tick rules_boundary=game_rules_.until_boundary();
        if(rules_boundary<=0)throw std::logic_error("unprocessed rules deadline");
        part=(std::min)(part,rules_boundary);
        if(hidden_test_&&test_eject_remaining_>0)part=(std::min)(part,test_eject_remaining_);
        const runtime::Tick window=plunger_.window_remaining(time_);
        if(window>0)part=(std::min)(part,window);
        if(captured_) {
            const runtime::Tick boundary=captured_hole_?hole_drop_.until_boundary():sink_wait_;
            if(boundary<=0)throw std::logic_error("inactive capture clock");
            part=(std::min)(part,boundary);
        }
        for(std::size_t i=0;i<kickout_cooldowns_.size();++i)
            if(kickout_cooldowns_[i]>0)part=(std::min)(part,kickout_cooldowns_[i]);
        step(part);duration-=part;
    }
}
ComponentVisualState PlayfieldTest::component_visual(std::size_t group) const {
    for(std::size_t i=0;i<profile_.components.size();++i)if(profile_.components[i].group==group) {
        const reference::ComponentProfile& profile=profile_.components[i];
        const ComponentState& state=component_states_[i];
        const std::size_t bank=bumper_bank(group);
        const std::size_t base=bank<2?2*progression_.snapshot().groups[bank].level:0;
        const std::size_t frame=bank<2?base+(state.active?1U:0U):state.active?profile.fired_frame:
            (profile.idle_frame<0?profile.fired_frame:static_cast<std::size_t>(profile.idle_frame));
        const runtime::Tick remaining=state.fired_at.empty()?0:
            (std::max)(runtime::Tick(0),profile.duration-(time_-state.fired_at[0]));
        const ComponentVisualState result={state.active,state.active||profile.idle_frame>=0,frame,remaining};
        return result;
    }
    throw std::out_of_range("unknown hit component");
}
MotionSnapshot PlayfieldTest::snapshot() const {
    const reference::NudgeOffset offset=nudge_.offset();
    std::uint32_t flashes=0;for(std::size_t i=0;i<component_states_.size();++i)if(component_states_[i].active)count(flashes);
    const rules::GameSnapshot game=game_rules_.snapshot();
    const MotionSnapshot result={time_,ball_,plunger_.pull(),seated_,suspended_,launches_,returns_,contacts_,exhausted_,
        flippers_[0].pose(),flippers_[1].pose(),flipper_contacts_,nudges_,moving_exhausted_,offset.dx,offset.dy,
        plunger_.charge(),!plunger_.held() && plunger_.charge()>0,captured_,sink_captures_,sink_ejections_,captured_&&captured_kickout_,kickout_captures_,kickout_ejections_,
        kickout_cooldowns_.empty()?0:kickout_cooldowns_[0],captured_&&captured_hole_,hole_captures_,hole_landings_,ramp_transitions_,hidden_test_,bumper_hits_,rebound_hits_,flashes,
        game.score,game.total_score,game.player,game.ball_number,game.balls_remaining,
        game.game_over,game.ball_active,game.serve_pending,game.shoot_again,game.drains,game.saved_drains,game.serves,
        target_hits_,bank_completions_,game.score_multiplier,target_bank_.snapshot().tier,
        lane_hits_,lane_completions_,progression_.snapshot().groups[0].level,progression_.snapshot().groups[1].level,
        medal_hits_,medal_completions_,medal_bank_.snapshot().tier,game.extra_balls_pending,game.extra_balls_used,game.replay_armed,
        static_cast<std::uint32_t>(mission_.snapshot().active),static_cast<std::uint32_t>(mission_.snapshot().selected),
        mission_.snapshot().stage,mission_.snapshot().remaining,mission_.snapshot().rank,mission_.snapshot().rank_progress,mission_.snapshot().completed,game.tilted,tilt_counter_.snapshot().pressure,unlimited_balls_,cheats_used_,gravity_active_};
    return result;
}
rules::TargetBankSnapshot PlayfieldTest::target_bank_snapshot() const {return target_bank_.snapshot();}
rules::TargetBankSnapshot PlayfieldTest::medal_bank_snapshot() const {return medal_bank_.snapshot();}
rules::ProgressionSnapshot PlayfieldTest::progression_snapshot() const {return progression_.snapshot();}
ComponentVisualState PlayfieldTest::rollover_visual(std::size_t group) const {
    for(std::size_t i=0;i<profile_.rollovers.size();++i)if(profile_.rollovers[i].group==group) {
        const RolloverState& s=rollover_states_[i];
        const runtime::Tick left=s.cooldown?(std::max)(runtime::Tick(0),profile_.rollovers[i].rearm_delay-(time_-s.exited_at)):0;
        const int frame=(s.inside||s.delayed_hide)?profile_.rollovers[i].active_frame:profile_.rollovers[i].idle_frame;
        const ComponentVisualState result={s.inside,frame>=0,static_cast<std::size_t>(frame<0?0:frame),left};return result;
    }
    throw std::out_of_range("unknown rollover");
}
bool PlayfieldTest::activate_cheat(rules::CheatAction action) {
    if(!finite_round_||game_rules_.snapshot().game_over||!game_rules_.snapshot().ball_active)return false;
    switch(action){
    case rules::CheatAction::HiddenTest:
        if(!hidden_test_){const runtime::InputTransition edge={1,0x43484541U,runtime::Action::HiddenTest,true,runtime::TransitionCause::Physical};input(edge);}break;
    case rules::CheatAction::ExtraBall:if(!game_rules_.award_extra_ball(true))return false;control_cue(28,time_);break;
    case rules::CheatAction::UnlimitedBalls:unlimited_balls_=true;break;
    case rules::CheatAction::GravityWell:gravity_active_=true;sync_advanced_colliders();break;
    case rules::CheatAction::RankUp:
        if(mission_.promote_rank()){rank_flash_index_=mission_.snapshot().rank-1;rank_flash_started_=time_;rank_flash_duration_=2000000000LL;cue(rank_sound_,audio::SoundRole::Resource,time_);}break;
    default:throw std::invalid_argument("unknown gameplay cheat action");
    }
    cheats_used_=true;return true;
}
rules::KickbackSnapshot PlayfieldTest::kickback_snapshot(std::size_t group) const {
    for(std::size_t k=0;k<kickbacks_.size();++k)if(profile_.advanced.kickbacks[k].group==group)return kickbacks_[k].snapshot();
    throw std::out_of_range("unknown kickback");
}
rules::TiltSnapshot PlayfieldTest::tilt_snapshot() const {return tilt_counter_.snapshot();}
rules::AdvancedSnapshot PlayfieldTest::advanced_snapshot() const {return advanced_.snapshot();}
ComponentVisualState PlayfieldTest::advanced_target_visual(std::size_t group) const {
    const rules::AdvancedSnapshot state=advanced_.snapshot();
    for(const reference::AdvancedTargetProfile& target:profile_.advanced.targets)if(target.body.group==group){
        const std::size_t n=target.number;const int frame=state.target_fired[n]?target.body.down_frame:target.body.idle_frame;
        const ComponentVisualState result={state.target_fired[n],state.target_visible[n],static_cast<std::size_t>(frame<0?0:frame),state.target_remaining[n]};return result;
    }
    throw std::out_of_range("unknown advanced target");
}
rules::MissionSnapshot PlayfieldTest::mission_snapshot() const {return mission_.snapshot();}
std::string PlayfieldTest::mission_text() const {return finite_round_?mission_.objective_text():std::string();}
std::size_t PlayfieldTest::collider_count() const{return colliders_.size();}
runtime::Tick PlayfieldTest::full_pull_time() const {return plunger_.full_pull_time();}
runtime::Tick PlayfieldTest::flipper_raise_time() const{return profile_.flippers[0].raise_duration;}
runtime::Tick PlayfieldTest::flipper_return_time() const{return profile_.flippers[0].return_duration;}
void PlayfieldTest::render(scene::ResourceScene& scene,const runtime::InputState& input,bool ball_visible) const {
    (void)input;
    for(std::size_t i=0;i<scene.instances.size();++i) {
        const scene::SpriteInstance& instance=scene.instances[i];
        if(instance.state_sprite_indices.empty())continue;
        bool bank_sprite=false;const rules::TargetBankSnapshot bank=target_bank_.snapshot();
        for(std::size_t t=0;t<profile_.multiplier_bank.targets.size();++t)
            if(instance.resource_group==profile_.multiplier_bank.targets[t].group) {
                const int frame=bank.raised[t]?profile_.multiplier_bank.targets[t].idle_frame:
                    profile_.multiplier_bank.targets[t].down_frame;
                const std::size_t image=frame<0?0:static_cast<std::size_t>(frame);
                if(image>=instance.state_sprite_indices.size())throw std::runtime_error("target frame out of range");
                scene.set_state(i,instance.state_sprite_indices[image],image);scene.set_visible(i,frame>=0);
                bank_sprite=true;break;
            }
        for(std::size_t l=0;!bank_sprite&&l<profile_.multiplier_bank.lamps.size();++l)
            if(instance.resource_group==profile_.multiplier_bank.lamps[l].group) {
                const std::size_t image=profile_.multiplier_bank.lamps[l].on_frame;
                if(image>=instance.state_sprite_indices.size())throw std::runtime_error("multiplier lamp frame out of range");
                scene.set_state(i,instance.state_sprite_indices[image],image);scene.set_visible(i,bank.lamp_visible[l]);
                bank_sprite=true;break;
            }
        const rules::TargetBankSnapshot medal=medal_bank_.snapshot();
        for(std::size_t t=0;!bank_sprite&&t<3;++t)if(instance.resource_group==profile_.medal_bank.targets[t].group) {
            scene.set_state(i,instance.state_sprite_indices[0],0);scene.set_visible(i,medal.raised[t]);bank_sprite=true;
        }
        for(std::size_t l=0;!bank_sprite&&l<3;++l)if(instance.resource_group==profile_.medal_bank.lamps[l].group) {
            scene.set_state(i,instance.state_sprite_indices[0],0);scene.set_visible(i,medal.lamp_visible[l]);bank_sprite=true;
        }
        for(std::size_t b=0;!bank_sprite&&b<2;++b)for(std::size_t l=0;!bank_sprite&&l<3;++l)
            if(instance.resource_group==profile_.lane_groups[b].lamps[l].group) {
                scene.set_state(i,instance.state_sprite_indices[0],0);
                scene.set_visible(i,progression_.snapshot().groups[b].lamp_visible[l]);bank_sprite=true;
            }
        for(std::size_t r=0;!bank_sprite&&r<profile_.rollovers.size();++r)
            if(instance.resource_group==profile_.rollovers[r].group) {
                const ComponentVisualState visual=rollover_visual(instance.resource_group);
                scene.set_state(i,instance.state_sprite_indices[visual.frame],visual.frame);scene.set_visible(i,visual.visible);bank_sprite=true;
            }
        for(const reference::AdvancedTargetProfile& target:profile_.advanced.targets)if(instance.resource_group==target.body.group){
            const ComponentVisualState state=advanced_target_visual(instance.resource_group);
            scene.set_state(i,instance.state_sprite_indices.at(state.frame),state.frame);scene.set_visible(i,state.visible);bank_sprite=true;break;
        }
        const std::map<std::size_t,std::uint32_t>::const_iterator light=advanced_light_numbers_.find(instance.resource_group);
        if(light!=advanced_light_numbers_.end()){
            const rules::AdvancedLightState state=advanced_.light(light->second);
            scene.set_state(i,instance.state_sprite_indices.at(state.frame),state.frame);scene.set_visible(i,state.visible);bank_sprite=true;
        }
        for(unsigned fuel=0;fuel<6;++fuel)if(instance.resource_group==profile_.advanced.fuel.lamps[fuel].group){
            scene.set_state(i,instance.state_sprite_indices[0],0);scene.set_visible(i,advanced_.snapshot().fuel_lamps[fuel]);bank_sprite=true;
        }
        if(finite_round_&&mission_.snapshot().active==rules::MissionId::Secret){
            const unsigned chosen[]={2,0,1};const unsigned hole=chosen[mission_.snapshot().stage];
            const std::size_t arrows[]={116,119,122},lamps[]={125,126,127};
            if(instance.resource_group==lamps[hole]){scene.set_state(i,instance.state_sprite_indices[0],0);scene.set_visible(i,true);bank_sprite=true;}
            if(instance.resource_group==arrows[hole]){const unsigned frame=2-hole;scene.set_state(i,instance.state_sprite_indices.at(frame),frame);
                scene.set_visible(i,((static_cast<std::uint64_t>(time_-mission_started_)/100000000ULL)&1U)!=0);bank_sprite=true;}
        }
        for(std::size_t s=0;s<spinners_.size();++s)if(instance.resource_group==profile_.advanced.spinners[s].group){
            const unsigned frame=spinners_[s].snapshot().frame;scene.set_state(i,instance.state_sprite_indices.at(frame),frame);scene.set_visible(i,true);bank_sprite=true;
        }
        for(std::size_t k=0;k<kickbacks_.size();++k)if(instance.resource_group==profile_.advanced.kickbacks[k].group){
            const rules::KickbackSnapshot kick=kickbacks_[k].snapshot();
            const std::size_t frame=kick.frame<0?0:static_cast<std::size_t>(kick.frame);
            scene.set_state(i,instance.state_sprite_indices.at(frame),frame);scene.set_visible(i,kick.visible);bank_sprite=true;
        }
        for(const reference::DynamicWallProfile& wall:profile_.advanced.dynamic_walls)if(instance.resource_group==wall.group){
            const bool enabled=colliders_.at(wall.collider_indices.at(0)).enabled;
            const int frame=enabled?wall.closed_frame:wall.open_frame;
            scene.set_state(i,instance.state_sprite_indices.at(frame<0?0:static_cast<std::size_t>(frame)),frame<0?0:static_cast<std::size_t>(frame));scene.set_visible(i,frame>=0);bank_sprite=true;
        }
        if(finite_round_) {
            const rules::MissionSnapshot mission=mission_.snapshot();
            bool owned=false,on=false;const std::size_t group=instance.resource_group;
            if(group>=205&&group<=213){
                owned=true;on=group-205<mission.rank;
                const std::uint64_t age=static_cast<std::uint64_t>(time_-rank_flash_started_);
                if(rank_flash_duration_!=0&&group-205==rank_flash_index_&&(age>>32U)==0&&static_cast<std::uint32_t>(age)<2000000000U)
                    on=((age/100000000ULL)&1U)!=0;
            }
            const std::size_t progress[]={196,160,197,159,198,158,199,157,200,156,201,155,202,154,203,153,204,152};
            for(unsigned p=0;p<18;++p)if(group==progress[p]){owned=true;on=p<mission.rank_progress;}
            if(group>=232&&group<=254){
                owned=true;const std::vector<unsigned> goals=mission_.guidance_lights(mission_context());
                const bool flashing=((static_cast<std::uint64_t>(time_-mission_started_)/100000000ULL)&1U)!=0;
                on=flashing&&std::find(goals.begin(),goals.end(),static_cast<unsigned>(group-232+300))!=goals.end();
            }
            if(group==168){owned=true;on=gravity_active_;}
            if(group==229){owned=true;on=mission.phase==rules::MissionPhase::Active||
                (mission.selected!=rules::MissionId::None&&((static_cast<std::uint64_t>(time_-mission_started_)/100000000ULL)&1U)!=0);}
            if(group==163){owned=true;on=mission.selected!=rules::MissionId::None;}
            if(group==230){owned=true;on=game_rules_.snapshot().replay_armed;}
            if(group==231){owned=true;const rules::GameSnapshot game=game_rules_.snapshot();on=game.shoot_again;
                if(game.save_phase==rules::SavePhase::Flashing)on=((static_cast<std::uint64_t>(time_)/100000000ULL)&1U)!=0;}
            if(group==162){owned=true;on=mission.active==rules::MissionId::TimeWarp&&mission.stage==1&&((static_cast<std::uint64_t>(time_-mission_started_)/100000000ULL)&1U)!=0;}
            if(group==161){owned=true;on=game_rules_.snapshot().reflex_active&&((static_cast<std::uint64_t>(time_)/100000000ULL)&1U)!=0;}
            if(owned){scene.set_state(i,instance.state_sprite_indices[0],0);scene.set_visible(i,on);bank_sprite=true;}
        }
        if(bank_sprite)continue;
        bool reaction=false;
        for(std::size_t r=0;r<profile_.components.size();++r)if(profile_.components[r].group==instance.resource_group) {
            const ComponentVisualState visual=component_visual(instance.resource_group);
            if(visual.frame>=instance.state_sprite_indices.size())throw std::runtime_error("component reaction frame out of range");
            scene.set_state(i,instance.state_sprite_indices[visual.frame],visual.frame);
            scene.set_visible(i,visual.visible);reaction=true;break;
        }
        if(reaction)continue;
        std::size_t state=0;
        if(instance.object_type==1001)state=plunger_.image_frame();
        else if(instance.object_type==1003)state=visual::plunger_pose_index(instance.state_sprite_indices.size(),flippers_[0].pose(),visual::PullDirection::FirstToLast);
        else if(instance.object_type==1004)state=visual::plunger_pose_index(instance.state_sprite_indices.size(),flippers_[1].pose(),visual::PullDirection::FirstToLast);
        else continue;
        if(state!=instance.state_index)scene.set_state(i,instance.state_sprite_indices[state],state);
    }
    if(game_rules_.snapshot().tilted)for(std::size_t i=0;i<scene.instances.size();++i)if(scene.instances[i].object_type==1002)
        scene.set_visible(i,scene.instances[i].resource_group==179&&((static_cast<std::uint64_t>(time_)/100000000ULL)&1U)!=0);
    scene.render();
    const double z=ball_.height;
    const visual::ProjectedPoint p=visual::project(camera_,ball_.position.x,ball_.position.y,z);
    if(ball_visible && p.in_front && (!finite_round_||game_rules_.snapshot().ball_active) && (!captured_ || captured_kickout_ || captured_hole_)) {
        const std::vector<double>::const_iterator sample=std::lower_bound(art_depth_.begin(),art_depth_.end(),p.depth,std::greater<double>());
        const std::size_t variant=(std::min)(static_cast<std::size_t>(sample-art_depth_.begin()),art_depth_.size()-1);
        const visual::BallSpriteInfo info=art_.info(variant);
        visual::BallDrawOptions options(static_cast<std::uint32_t>((std::max)(0.0,(std::min)(65535.0,p.depth))),true);
        options.depth_test=scene::DepthTest::LessEqual;
        const double x=p.x+scene.layout.table_x,y=p.y+scene.layout.table_y;
        if(x>(std::numeric_limits<int>::min)()+1.0 && x<(std::numeric_limits<int>::max)()-1.0 &&
           y>(std::numeric_limits<int>::min)()+1.0 && y<(std::numeric_limits<int>::max)()-1.0)
            visual::draw_ball(scene.surface,art_,variant,static_cast<int>(std::floor(x+.5)),static_cast<int>(std::floor(y+.5)),
                visual::AnchorOffset(info.width/2,info.height/2),options);
    }
    const reference::NudgeOffset offset=nudge_.offset();
    if(offset.dx || offset.dy) {
        const std::int64_t x=std::int64_t(scene.layout.table_x)-2,y=std::int64_t(scene.layout.table_y)-3;
        if(x<(std::numeric_limits<int>::min)() || y<(std::numeric_limits<int>::min)())
            throw std::overflow_error("table shake region overflow");
        visual::shift_region(scene.surface,scene::Rect(static_cast<int>(x),static_cast<int>(y),
            table_width_+4,table_height_+6),offset.dx,offset.dy);
    }
    if(finite_round_) {
        const rules::GameSnapshot game=game_rules_.snapshot();
        hud::HudState state;state.score=game.score;state.ball_number=game.ball_number;
        state.player=game.player;state.game_over=game.game_over;
        score_display_.render(scene.surface,scene.background,state);
    }
}
}
}




