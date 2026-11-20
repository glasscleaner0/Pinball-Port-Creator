#ifndef PINBALL_REWRITE_KICKBACK_HPP
#define PINBALL_REWRITE_KICKBACK_HPP
#include "runtime.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace pinball { namespace rules {
struct KickbackConfig {
    runtime::Tick arm_delay,pulse_period;
    float inactive_threshold,armed_threshold;
    int idle_frame,fired_frame,reset_frame;
    KickbackConfig(runtime::Tick arm,runtime::Tick pulse,float inactive,float armed,
                   int idle=0,int fired=1,int reset=-1);
};
struct KickbackSnapshot {
    bool kick_active,armed,tilted,visible;
    float threshold; // Effective threshold for the NEXT physical query.
    int frame;
    std::size_t pending_callbacks;
    runtime::Tick remaining;
};
struct KickbackEvents {
    bool contact_processed,started,kicked,gate_close,due_remaining;
    std::size_t callbacks,pulses;
    KickbackEvents();
};
// Fresh delayed kickback controller. Collision response remains caller-owned.
// ANY first contact starts a delay; subsequent strict activation consumes it.
// The queued callback then restores idle art and closes the associated gate.
// Retriggering does not erase an older pulse callback: it can arm the new
// attempt early, matching the shared-flag behavior of the source controller.
// Contacts precede equal-time callbacks. Caller can split at until_boundary.
// Late dispatch uses nominal deadlines with an explicit callback budget;
// source frame-timer catch-up parity is not asserted. Contact is deferred if
// earlier callbacks cannot be drained within that budget.
// Reset cancels ALL queued callbacks as a new-game/tilt safety boundary,
// rather than retaining untracked callbacks from the reference's reset quirk.
// Hiding remains conditional on a tracked pending timer, as in the source.
class Kickback {
public:
    explicit Kickback(const KickbackConfig& config,runtime::Tick now=0);
    KickbackEvents collision(bool strict_activated,runtime::Tick now,std::size_t budget=256);
    KickbackEvents advance(runtime::Tick now,std::size_t budget=256);
    void reset(runtime::Tick now=0);
    void set_tilted(bool tilted,runtime::Tick now);
    KickbackSnapshot snapshot() const;
    runtime::Tick until_boundary() const;
    runtime::Tick time() const;
private:
    struct Pending {std::uint64_t deadline,id;};
    KickbackConfig config_;
    runtime::Tick now_;
    bool active_,armed_,tilted_;
    int frame_;
    std::uint64_t sequence_,tracked_;
    std::vector<Pending> pending_;
    void validate_now(runtime::Tick now) const;
    void schedule(std::uint64_t base,runtime::Tick delay);
    std::size_t first_pending() const;
    KickbackEvents dispatch(runtime::Tick now,bool inclusive,std::size_t budget);
};
} }
#endif
