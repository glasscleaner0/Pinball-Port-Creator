#ifndef PINBALL_REWRITE_BITMAP_FONT_HPP
#define PINBALL_REWRITE_BITMAP_FONT_HPP

#include "dat_reader.hpp"
#include "scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace pinball {
namespace font {

class FontError : public std::runtime_error {
public:
    explicit FontError(const std::string& message);
};

struct IndexedGlyph {
    std::uint16_t width;
    std::uint16_t height;
    // Owned tightly packed top-down palette indices, including zero. Source
    // colour/style meaning is retained instead of baked into RGBA glyphs.
    std::vector<std::uint8_t> indices;
    std::size_t source_offset;
};

// The local asset named PINBALL2.MID is this bitmap-font format, not MIDI.
// Its three LE uint16 fields precede 128 widths, then ASCII32..127 records,
// each a width byte followed by width*height raw indexed samples. Stored rows
// read top-down; this is an asset-appearance observation, not a DIB convention.
class BitmapFont {
public:
    static BitmapFont parse(std::vector<std::uint8_t> bytes);
    static BitmapFont load(const std::string& path);
    std::uint16_t gap() const;
    std::uint16_t unknown_header_value() const;
    std::uint16_t height() const;
    const std::array<std::uint8_t, 128>& widths() const;
    const std::vector<IndexedGlyph>& glyphs() const;
    bool contains(unsigned byte) const;
    const IndexedGlyph& glyph(unsigned byte) const;
private:
    BitmapFont();
    std::uint16_t gap_;
    std::uint16_t unknown_;
    std::uint16_t height_;
    std::array<std::uint8_t, 128> widths_;
    std::vector<IndexedGlyph> glyphs_;
};

struct AtlasGlyph {
    std::size_t group_index;
    std::size_t bitmap_field_index;
    data::BitmapHeader stored_header;
    IndexedGlyph glyph;
};

// Generic DAT image atlas in literal group/field order. FONT.DAT's ten glyphs
// visibly resemble digits0..9, but the asset has no explicit codepoint mapping;
// this API therefore exposes ordinals and preserves the raw metadata in dat.
struct GlyphAtlas {
    std::vector<AtlasGlyph> glyphs;
    std::vector<std::string> notices;
};
GlyphAtlas decode_atlas(const data::DatFile& dat);
GlyphAtlas load_atlas(const std::string& path);

typedef std::array<pinball360::Rgba8, 256> ColorLookup;
struct FontPaint {
    ColorLookup colors;
    scene::DrawOptions draw;
    // No original index styling is inferred: callers must supply colors.
    // Default colors have zero alpha. Index0 is keyed, depth ignored/preserved.
    FontPaint();
};

enum class UnsupportedCharacter { Reject, Skip, Replace };
struct TextOptions {
    UnsupportedCharacter unsupported;
    std::uint8_t replacement;
    std::uint32_t line_gap;
    TextOptions();
};
struct TextMetrics {
    std::uint64_t width;
    std::uint64_t height;
    std::size_t glyph_count;
    std::size_t line_count;
};

// Input is ASCII bytes, not UTF-8. LF and CRLF start a new line; a bare CR
// follows the unsupported-byte policy. Replacement must name an existing
// glyph, even if that glyph is blank in the asset (the supplied '?' is blank).
// The font's gap is between glyphs; width excludes a trailing gap. Nonempty
// text includes empty logical lines; empty text has zero lines and dimensions.
TextMetrics measure_text(const BitmapFont& font, const std::string& ascii,
                         const TextOptions& options = TextOptions());
void draw_glyph(scene::Surface& target, const IndexedGlyph& glyph, int x, int y,
                const FontPaint& paint);
void draw_text(scene::Surface& target, const BitmapFont& font,
               const std::string& ascii, int x, int y, const FontPaint& paint,
               const TextOptions& options = TextOptions());

} // namespace font
} // namespace pinball
#endif
