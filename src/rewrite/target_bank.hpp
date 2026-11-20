#ifndef PINBALL_REWRITE_TARGET_BANK_HPP
#define PINBALL_REWRITE_TARGET_BANK_HPP

#include "runtime.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace pinball {
namespace rules {

// Caller-supplied values describe this bank; no geometry, scoring ledger or
// input backend is owned here. Source-observed default multiplier tiers are
// fixed at 1,2,3,5,10. The three targets are ordered by the caller's profile.
struct TargetBankConfig {
    runtime::Tick raise_delay,decay_duration,flash_duration,flash_off,flash_on;
    std::uint32_t hit_points,completion_points;
    bool medal;
    std::array<std::uint32_t,3> medal_completion_points;
    std::uint32_t extra_ball_level;
    TargetBankConfig(runtime::Tick raise,runtime::Tick decay,runtime::Tick flash,
        runtime::Tick off=100000000LL,runtime::Tick on=100000000LL,
        std::uint32_t hit=500U,std::uint32_t completion=1500U);
    TargetBankConfig(runtime::Tick raise,runtime::Tick decay,runtime::Tick flash,
        runtime::Tick off,runtime::Tick on,std::uint32_t hit,
        const std::array<std::uint32_t,3>& completions,std::uint32_t credit_level);
};

enum class BankLampMode { Off, Flashing, Steady };
struct TargetBankSnapshot {
    std::array<bool,3> raised;
    // Declared light-group order, not numeric resource-group order.
    std::array<BankLampMode,4> lamp_modes;
    std::array<bool,4> lamp_visible;
    std::uint32_t tier,multiplier;
    bool raise_pending;
};
struct TargetHit {
    bool accepted,completed,extra_ball;
    std::uint32_t base_points,multiplier_before,points;
    TargetHit();
};

// Fresh state machine for the isolated three-target multiplier bank.
// hit() receives an already eligible physical hit (the caller enforces the
// source's strict incident-speed threshold). It drops a raised target once.
// Multiplier completion scores under the OLD factor, then advances the tier.
// Medal policy has three lamps, reports unscaled ordinary points and emits
// one extra_ball event on credit-level completions; caller owns the ledger.
// Collision events precede equal-time timers: hit() processes only earlier
// deadlines; advance() dispatches deadlines through now, including equality.
// Pause freezes the caller's clock. No rendering timer or wall clock is used.
class TargetBank {
public:
    explicit TargetBank(const TargetBankConfig& config,runtime::Tick now=0);
    void reset(runtime::Tick now=0);
    TargetHit hit(std::size_t target,runtime::Tick now);
    void advance(runtime::Tick now);
    void set_tier(std::uint32_t tier,runtime::Tick now,bool restart_decay=true);
    void request_rearm(runtime::Tick now); // Preserves tier/lamps and already-raised targets.
    // A saved loss leaves this machine intact. Ordinary loss resets it here;
    // source counter cancellation happens during the inactive drain wait.
    void ordinary_loss(runtime::Tick now);
    TargetBankSnapshot snapshot() const;
    runtime::Tick time() const;
    // Nonnegative time to the next rearm, flash-end or decay boundary;
    // maxTick means none. Blink phase is derived directly by snapshot().
    runtime::Tick until_boundary() const;
private:
    TargetBankConfig config_;
    runtime::Tick now_,raise_started_,decay_started_;
    std::array<runtime::Tick,4> flash_started_;
    std::array<bool,3> raised_;
    std::array<bool,4> flashing_;
    std::uint32_t tier_;
    bool raise_pending_,decay_active_;
    void validate_now(runtime::Tick now) const;
    void update(runtime::Tick now,bool inclusive);
    runtime::Tick next_boundary(unsigned& kind) const;
};

} // namespace rules
} // namespace pinball
#endif



