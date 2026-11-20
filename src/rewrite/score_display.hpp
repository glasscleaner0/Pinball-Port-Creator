#ifndef PINBALL_REWRITE_SCORE_DISPLAY_HPP
#define PINBALL_REWRITE_SCORE_DISPLAY_HPP

#include "scene.hpp"
#include <array>
#include <cstdint>

namespace pinball {
namespace table { class TableModel; }
namespace hud {

struct HudState {
    std::uint64_t score; // Displayed part: 0..1000000000, inclusive; rules own wrap.
    std::uint32_t ball_number; // 0 blanks the ball box; otherwise one decimal digit.
    std::uint32_t player; // One decimal digit, 1..9. Current rules use player 1.
    bool game_over; // Frontend status only; no message font/style is invented here.
    bool visible;
    HudState();
};

struct HudLayout {
    scene::Rect score, ball, player, info;
    // The exact 1e9 value needs ten original digits, extending left of the
    // nine-digit score metadata box. This whole footprint is restored.
    scene::Rect score_restore;
    std::size_t digit_group;
};

// Loads the original numeric glyphs directly from PINBALL.DAT. Score metadata
// names font-base group + rectangle; source behavior identifies group+0..9 as
// decimal digits. No FONT.DAT, external text font or generated art is needed.
class ScoreDisplay {
public:
    explicit ScoreDisplay(const table::TableModel& model);
    const HudLayout& layout() const;
    const scene::Sprite& digit(unsigned value) const;
    // Call after board/ball/shake rendering. Restores only numeric sidebar
    // color regions from a distinct immutable background, then draws keyed,
    // right-aligned decimal digits with no gap/leading zeros/separators.
    // Depth and pixels outside sidebar/clip are preserved. Zero ball is blank;
    // hiding clears all numeric regions. Invalid state/storage throws before
    // changing the target. The score may use ten digits at exactly 1e9.
    void render(scene::Surface& target, const scene::Surface& background,
                const HudState& state, const scene::Rect& clip = scene::Rect()) const;
private:
    HudLayout layout_;
    scene::Rect sidebar_;
    std::array<scene::Sprite,10> digits_;
    void draw_number(scene::Surface& target, std::uint64_t value,
                     const scene::Rect& field, const scene::Rect& clip) const;
};

} // namespace hud
} // namespace pinball
#endif
