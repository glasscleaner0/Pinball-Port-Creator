#ifndef PINBALL_REWRITE_TABLE_PROGRESSION_HPP
#define PINBALL_REWRITE_TABLE_PROGRESSION_HPP
#include "runtime.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace pinball { namespace rules {

struct LaneProgressionConfig {
    runtime::Tick completion_flash,decay_period,flash_off,flash_on;
    LaneProgressionConfig(runtime::Tick flash,runtime::Tick decay,runtime::Tick off,runtime::Tick on);
};
struct LaneGroupSnapshot {
    std::array<bool,3> logical_on,lamp_visible;
    bool flashing;
    std::uint32_t level;
};
struct ProgressionSnapshot {std::array<LaneGroupSnapshot,2> groups;};
struct LaneHit {bool completed;std::uint32_t level_changed_mask;};

// New implementation of two three-lane bumper-upgrade groups. Geometry,
// rollover entry/exit latches, score and bumper image/material state remain
// caller-owned. Every eligible entry can award points, even during flashing.
// Pause freezes the caller clock; saved loss retains state. Contact/rotation
// precedes equal-time expiry. Unobservable periodic level-zero decay callbacks
// are omitted after reaching zero; a collection restarts the period normally.
class TableProgression {
public:
    explicit TableProgression(const std::array<LaneProgressionConfig,2>& config,runtime::Tick now=0);
    void reset(runtime::Tick now=0);
    LaneHit hit(std::size_t bank,std::size_t lane,runtime::Tick now);
    std::uint32_t rotate(int direction,runtime::Tick now);
    std::uint32_t advance(runtime::Tick now);
    std::uint32_t set_level(std::size_t bank,std::uint32_t level,runtime::Tick now);
    void ordinary_loss(runtime::Tick now);
    ProgressionSnapshot snapshot() const;
    runtime::Tick until_boundary() const;
    runtime::Tick time() const;
private:
    struct Group {
        std::array<bool,3> lamps;
        std::uint32_t level;
        bool flashing,decay_active;
        runtime::Tick flash_started,decay_started;
    };
    std::array<LaneProgressionConfig,2> config_;
    std::array<Group,2> groups_;
    runtime::Tick now_;
    void validate_now(runtime::Tick now) const;
    runtime::Tick next_boundary(unsigned& kind) const;
    std::uint32_t update(runtime::Tick now,bool inclusive);
};
} }
#endif

