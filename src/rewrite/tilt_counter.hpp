#ifndef PINBALL_REWRITE_TILT_COUNTER_HPP
#define PINBALL_REWRITE_TILT_COUNTER_HPP
#include "runtime.hpp"
namespace pinball { namespace rules {
struct TiltCounterConfig {
    double active_rate,decay_rate,warning_threshold,tilt_threshold,after_clear;
    runtime::Tick drain_timeout;
    TiltCounterConfig(double rise=4,double decay=1,double warning=.5,double tilt=1,
                      runtime::Tick timeout=30000000000LL,double clear_value=-2);
};
struct TiltEvents {
    bool warning,tilted,force_drain;
    TiltEvents():warning(false),tilted(false),force_drain(false) {}
};
struct TiltSnapshot {
    double pressure;
    bool tilt_lock,timeout_pending;
    runtime::Tick remaining;
};
// New source-behavior counter, integrated once per caller simulation frame.
// nudge_active is OR of the left/right/up nudge button-active flags, not
// the resultant force vector. Warning and tilt thresholds are STRICT.
// Double arithmetic is an explicit portable implementation policy; no
// bit-for-bit equivalence with the source float/frame cadence is asserted.
// A thirty-second timeout requests a drain ONCE; it does not clear tilt.
// clear_lock matches drain cleanup; tilt_no_more matches the later feed's
// counter reset to -2. An inactive subsequent frame clamps that value to0.
// Caller disables may_tilt while a ball is in the drain; warnings may remain.
// Pause freezes the supplied clock.
class TiltCounter {
public:
    explicit TiltCounter(const TiltCounterConfig& config=TiltCounterConfig(),runtime::Tick now=0);
    TiltEvents advance(runtime::Tick now,bool nudge_active,bool may_tilt=true);
    void clear_lock(runtime::Tick now);
    void tilt_no_more(runtime::Tick now);
    void reset(runtime::Tick now=0);
    TiltSnapshot snapshot() const;
    runtime::Tick until_boundary() const;
    runtime::Tick time() const;
private:
    TiltCounterConfig config_;
    runtime::Tick now_,tilted_at_;
    double pressure_;
    bool locked_,timeout_pending_;
    void validate_now(runtime::Tick now) const;
};
} }
#endif
