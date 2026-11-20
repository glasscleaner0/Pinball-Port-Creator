#ifndef PINBALL_REWRITE_FLAG_SPINNER_HPP
#define PINBALL_REWRITE_FLAG_SPINNER_HPP
#include "runtime.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace pinball { namespace rules {
struct SpinnerConfig {
 float minimum,maximum,scale,decay;unsigned frames;
 SpinnerConfig(float low,float high,float factor,float slowdown,unsigned images);
};
struct SpinnerEvents {std::uint32_t frames,wraps;SpinnerEvents():frames(0),wraps(0){}};
struct SpinnerSnapshot {unsigned frame;float speed;int direction;std::size_t pending;};
// Fresh owned discrete spinner. Source game-data constants determine speed,
// frame order and integer-ms scheduling. Retriggers retain prior callbacks.
// NewGame/reset clears every owned callback, unlike the source's stale-ID
// quirk. Exact original wall-clock callback budgeting remains unverified.
class FlagSpinner {
public:
 explicit FlagSpinner(const SpinnerConfig& config,runtime::Tick now=0);
 void reset(runtime::Tick now=0);
 SpinnerEvents hit(float incoming_speed,bool reverse,runtime::Tick now);
 SpinnerEvents advance(runtime::Tick now);
 SpinnerSnapshot snapshot() const;
 runtime::Tick until_boundary() const;
 runtime::Tick time() const;
private:
 SpinnerConfig config_;runtime::Tick now_;unsigned frame_;float speed_;int direction_;
 std::vector<runtime::Tick> pending_;
 void validate_now(runtime::Tick now) const;
 void frame(SpinnerEvents& events);
 SpinnerEvents update(runtime::Tick now,bool inclusive);
};
} }
#endif