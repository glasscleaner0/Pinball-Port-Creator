#ifndef PINBALL_REWRITE_MISSION_ENGINE_HPP
#define PINBALL_REWRITE_MISSION_ENGINE_HPP
#include <cstdint>
#include <vector>
#include <string>
namespace pinball { namespace rules {
// Objective names/requirements are game-data facts, independently implemented
// as a declarative objective evaluator. There are no source controller ports.
enum class MissionId : unsigned { None=0, Practice=2, LaunchTraining=3,
 ReentryTraining=4, Science=5, StrayComet=6, BlackHoleThreat=7,
 SpaceRadiation=8, BugHunt=9, AlienMenace=10, Rescue=11, Satellite=12,
 Reconnaissance=13, Doomsday=14, CosmicPlague=15, Secret=16,
 TimeWarp=17, Maelstrom=18 };
enum class MissionPhase { Waiting, Selecting, Active, GameOver };
enum class MissionFeature { LaunchExit, Ramp, Bumper, Target, Rollover,
 Rebound, Flag, Sink, Hyperspace, BlackHole, FuelTop, LevelChange };
struct MissionEvent {
 MissionFeature feature; unsigned index;
 MissionEvent(MissionFeature kind,unsigned number=0):feature(kind),index(number){}
};
struct MissionContext {
 unsigned attack_level,launch_level; bool booster_lit;
 MissionContext():attack_level(0),launch_level(0),booster_lit(false){}
};
struct MissionSnapshot {
 MissionPhase phase; MissionId selected,active;
 unsigned stage,remaining,rank,rank_progress,completed,selection_mask;
 bool fuel_available;
};
struct MissionActions {
 std::uint32_t special_points;
 bool started,stage_changed,completed,failed,rank_changed;
 bool reset_attack,reset_launch,rearm_popups,clear_spots;
 int rank_delta;
 std::uint32_t clear_spot_families; // Fuel1, mission2, left4, right8.
 MissionActions();
};
class MissionEngine {
public:
 MissionEngine();
 void reset();
 void ordinary_loss(); // Rank/progress survive; an in-progress mission does not.
 void game_over();
 bool promote_rank();
 MissionActions fuel_available(bool available);
 MissionActions event(const MissionEvent& event,const MissionContext& context=MissionContext());
 MissionSnapshot snapshot() const;
 std::string objective_text() const;
 std::vector<unsigned> guidance_lights(const MissionContext& context=MissionContext()) const;
 static const char* name(MissionId id);
 // Explicit action entry for a future cheat/menu; has no controller binding.
 MissionActions select(MissionId id);
 MissionActions complete_for_test();
private:
 MissionSnapshot state_;
 unsigned objective_mask_;
 MissionActions begin(MissionId id);
 MissionActions advance_objective(bool rank_reward=true);
 void set_stage(unsigned stage,MissionActions& actions);
 void add_rank(unsigned amount,MissionActions& actions);
};
} }
#endif