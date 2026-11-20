#ifndef PINBALL_REWRITE_MESSAGE_DISPLAY_HPP
#define PINBALL_REWRITE_MESSAGE_DISPLAY_HPP

#include "bitmap_font.hpp"
#include "scene.hpp"
#include <array>
#include <string>

namespace pinball {
namespace table { class TableModel; }
namespace hud {

struct MessageState {
    std::string info;
    std::string mission;
    bool visible;
    MessageState();
};
struct MessageLayout {
    scene::Rect sidebar;
    scene::Rect info;
    scene::Rect mission;
};

// Original indexed font and DAT text boxes; no message queue, duration, rule,
// localization, mission selection or system-font fallback is owned here.
// ASCII32..127 plus LF/CRLF are accepted; other bytes are rejected before any
// destination changes. Classic XP placement is left/top aligned, with whole
// font-height lines and word wrapping. Character advance includes the font gap.
class MessageDisplay {
public:
    MessageDisplay(const table::TableModel& model, const font::BitmapFont& font);
    MessageDisplay(const MessageLayout& layout, const font::BitmapFont& font,
                   const font::ColorLookup& colors);
    const MessageLayout& layout() const;
    const font::BitmapFont& bitmap_font() const;
    const scene::Sprite& glyph(unsigned character) const;
    // Restore only the two boxes' RGBA from a distinct immutable background,
    // then draw the supplied text. Depth and pixels outside the intersected
    // sidebar/box/canvas/caller clip are preserved. Empty/hidden text erases.
    void render(scene::Surface& target, const scene::Surface& background,
                const MessageState& state,
                const scene::Rect& clip = scene::Rect()) const;
private:
    MessageLayout layout_;
    font::BitmapFont font_;
    std::array<scene::Sprite, 96> glyphs_;
};

} // namespace hud
} // namespace pinball
#endif
