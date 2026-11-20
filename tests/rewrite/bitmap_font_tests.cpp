#include "bitmap_font.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>

namespace {
using namespace pinball::font;
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation, const std::string& fragment)
{
    try { operation(); }
    catch (const FontError& error) {
        require(std::string(error.what()).find(fragment) != std::string::npos,
            "wrong font failure: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("expected font error containing " + fragment);
}
void set16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value)
{
    bytes[at] = static_cast<std::uint8_t>(value & 255U);
    bytes[at + 1U] = static_cast<std::uint8_t>(value >> 8);
}
std::vector<std::uint8_t> synthetic_font()
{
    // Generated glyph samples only; no source font asset bytes embedded.
    std::vector<std::uint8_t> bytes(134, 0);
    set16(bytes, 0, 2); set16(bytes, 2, 0xabcdU); set16(bytes, 4, 3);
    bytes[6] = 99; // Unused control-byte width is preserved, not given semantics.
    for (unsigned code = 32; code < 128; ++code) {
        const unsigned width = code == 32 ? 0 : code == 'A' ? 3 : code == 'B' ? 1 : 2;
        bytes[6 + code] = static_cast<std::uint8_t>(width);
        bytes.push_back(static_cast<std::uint8_t>(width));
        for (unsigned y = 0; y < 3; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                static const std::uint8_t pattern[9] = {80,0,105, 105,80,0, 0,105,80};
                std::uint8_t value = pattern[y * 3U + x];
                if (code == 'B') value = y == 0 ? 105 : y == 1 ? 80 : 0;
                if (code == '@' && x == 0 && y == 0) value = 240;
                bytes.push_back(value);
            }
        }
    }
    return bytes;
}
FontPaint diagnostic_paint()
{
    FontPaint result;
    const pinball360::Rgba8 white = {250,240,230,255}, gray = {100,110,120,255};
    const pinball360::Rgba8 forbidden_zero = {255,0,255,255};
    result.colors[80] = white;
    result.colors[105] = gray;
    result.colors[0] = forbidden_zero; // Zero must remain keyed despite paint.
    return result;
}

void header_bounds_and_owned_indices()
{
    std::vector<std::uint8_t> bytes = synthetic_font();
    const BitmapFont font = BitmapFont::parse(bytes);
    require(font.gap() == 2 && font.height() == 3 && font.unknown_header_value() == 0xabcdU,
        "LE header metrics or unknown field were not preserved");
    require(font.glyphs().size() == 96 && font.widths()[0] == 99 && font.glyph('A').width == 3,
        "width table and 96-record range mismatch");
    require(font.glyph('@').indices[0] == 240, "opaque palette indices must remain unclassified");
    require(font.glyph('A').indices[0] == 80 && font.glyph('A').indices[3] == 105 &&
        font.glyph('A').indices[8] == 80, "raw font rows must remain top-down");
    bytes.assign(bytes.size(), 0);
    require(font.glyph('A').indices[3] == 105, "parsed font must own its glyph storage");
    bytes = synthetic_font();
    set16(bytes, 0, 0x0102U);
    require(BitmapFont::parse(bytes).gap() == 258, "gap field used native or reversed byte order");
    rejects([&] { BitmapFont::parse(std::vector<std::uint8_t>(133, 0)); }, "truncated");
    bytes = synthetic_font(); set16(bytes, 4, 0);
    rejects([&] { BitmapFont::parse(bytes); }, "height");
    bytes = synthetic_font(); bytes[font.glyph('A').source_offset - 1U] = 2;
    rejects([&] { BitmapFont::parse(bytes); }, "width");
    bytes = synthetic_font(); bytes[6 + 'A'] = 4;
    rejects([&] { BitmapFont::parse(bytes); }, "width");
    bytes = synthetic_font(); bytes.pop_back();
    rejects([&] { BitmapFont::parse(bytes); }, "truncated");
    bytes = synthetic_font(); bytes.push_back(0);
    rejects([&] { BitmapFont::parse(bytes); }, "trailing");
    require(!font.contains(31) && !font.contains(128) && font.contains(32) && font.contains(127),
        "supported ASCII endpoints mismatch");
    rejects([&] { font.glyph(128); }, "no font glyph");
}

void clipping_style_spacing_and_lines()
{
    const BitmapFont font = BitmapFont::parse(synthetic_font());
    TextOptions options;
    options.line_gap = 1;
    const TextMetrics metrics = measure_text(font, "AB\nBA", options);
    require(metrics.width == 6 && metrics.height == 7 && metrics.glyph_count == 4 && metrics.line_count == 2,
        "gap spacing, multiline height or trailing-gap handling mismatch");
    require(measure_text(font, "A").width == 3, "single glyph must not include a trailing gap");
    const TextMetrics empty = measure_text(font, "");
    require(empty.width == 0 && empty.height == 0 && empty.line_count == 0, "empty text metrics mismatch");
    require(measure_text(font, "A\n").line_count == 2 && measure_text(font, "A\n").height == 6,
        "a trailing newline must retain its empty logical line");
    require(measure_text(font, "AB\r\nBA", options).height == 7, "CRLF must be one newline");
    pinball::scene::Surface target(6, 7);
    const pinball360::Rgba8 background = {10,20,30,255};
    target.clear(background, 48000);
    FontPaint paint = diagnostic_paint();
    paint.draw.clip = pinball::scene::Rect(0,0,4,6);
    draw_text(target, font, "AB\nBA", -1, -1, paint, options);
    require(target.pixels[0] == 250 && target.pixels[1] == 240,
        "negative origin must advance to the correct source row/column");
    require(target.pixels[4] == 10, "zero-index sample must preserve destination pixels");
    require(target.pixels[(3U * 6U + 2U) * 4U] == 250, "newline origin or glyph gap is wrong");
    require(target.pixels[(4U * 6U + 2U) * 4U] == 100 && target.pixels[(4U * 6U + 3U) * 4U] == 250,
        "caller styling must preserve the distinction between indices80 and105");
    require(target.pixels[(5U * 6U + 3U) * 4U] == 100,
        "last visible glyph row must remain top-down");
    require(target.pixels[(4U * 6U + 4U) * 4U] == 10, "explicit clip right boundary was ignored");
    require(static_cast<std::size_t>(std::count(target.depth.begin(), target.depth.end(), 48000U)) == target.depth.size(),
        "font overlays must preserve table depth by default");
    const std::vector<std::uint8_t> before = target.pixels;
    draw_text(target, font, "AB", (std::numeric_limits<int>::max)(), 0, paint);
    draw_text(target, font, "AB", (std::numeric_limits<int>::min)(), 0, paint);
    require(target.pixels == before, "extreme signed text origins must clip safely");
    FontPaint unstyled;
    draw_text(target, font, "AB", 0, 0, unstyled);
    require(target.pixels == before, "no original colors may be silently assigned by default");
    IndexedGlyph invalid = font.glyph('A'); invalid.indices.pop_back();
    rejects([&] { draw_glyph(target, invalid, 0, 0, paint); }, "storage");
}

void rgba_extent_guards_before_source_storage()
{
    pinball::scene::Surface target(1, 1);
    const std::vector<std::uint8_t> original_pixels = target.pixels;
    const std::vector<std::uint16_t> original_depth = target.depth;
    const FontPaint paint = diagnostic_paint();
    const std::uint64_t size_limit = (std::numeric_limits<std::size_t>::max)();
    const std::uint64_t vector_limit = std::vector<std::uint8_t>().max_size();
    const unsigned dimensions[][2] = {{65535,65535}, {32768,16384}};
    for (std::size_t i = 0; i < 2; ++i) {
        IndexedGlyph glyph;
        glyph.width = static_cast<std::uint16_t>(dimensions[i][0]);
        glyph.height = static_cast<std::uint16_t>(dimensions[i][1]);
        glyph.source_offset = 0;
        // Deliberately empty: geometry must be checked before the source-length
        // mismatch, so the 32-bit overflow/capacity paths need no huge fixture.
        const std::uint64_t samples = std::uint64_t(glyph.width) * glyph.height;
        const std::uint64_t rgba_size = samples * 4U;
        if (samples > size_limit)
            rejects([&] { draw_glyph(target, glyph, 0, 0, paint); }, "sample extent overflows");
        else if (rgba_size > size_limit)
            rejects([&] { draw_glyph(target, glyph, 0, 0, paint); }, "RGBA extent overflows");
        else if (rgba_size > vector_limit)
            rejects([&] { draw_glyph(target, glyph, 0, 0, paint); }, "vector capacity");
        else
            rejects([&] { draw_glyph(target, glyph, 0, 0, paint); }, "storage");
    }
    require(target.pixels == original_pixels && target.depth == original_depth,
        "invalid glyph extents must leave both destination buffers untouched");
    if (sizeof(std::size_t) == 4)
        std::cout << "32-bit glyph RGBA extent guards exercised without allocating oversized source indices.\n";
}

void unsupported_bytes_are_explicit()
{
    const BitmapFont font = BitmapFont::parse(synthetic_font());
    const std::string invalid = std::string("A") + static_cast<char>(0xff) + "B";
    rejects([&] { measure_text(font, invalid); }, "unsupported");
    rejects([&] { measure_text(font, "A\rB"); }, "unsupported");
    pinball::scene::Surface target(10,3);
    const std::vector<std::uint8_t> original = target.pixels;
    const FontPaint paint = diagnostic_paint();
    rejects([&] { draw_text(target, font, invalid, 0, 0, paint); }, "unsupported");
    require(target.pixels == original, "unsupported byte rejection must not leave a partial draw");
    TextOptions options;
    options.unsupported = UnsupportedCharacter::Skip;
    require(measure_text(font, invalid, options).width == 6 &&
        measure_text(font, invalid, options).glyph_count == 2, "Skip must not insert an extra gap");
    options.unsupported = UnsupportedCharacter::Replace;
    options.replacement = 'B';
    require(measure_text(font, std::string("A") + static_cast<char>(0xff), options).width == 6,
        "replacement must use the chosen glyph's actual advance");
    options.replacement = 0;
    rejects([&] { measure_text(font, "A", options); }, "replacement");
}

std::uint64_t hash(const std::vector<std::uint8_t>& values)
{
    std::uint64_t result = 14695981039346656037ULL;
    for (std::size_t i = 0; i < values.size(); ++i) { result ^= values[i]; result *= 1099511628211ULL; }
    return result;
}
void actual_font(const std::string& path)
{
    const BitmapFont font = BitmapFont::load(path);
    require(font.gap() == 2 && font.unknown_header_value() == 15 && font.height() == 23,
        "reference font header changed");
    require(font.glyphs().size() == 96, "reference font must have exactly96 glyphs");
    const std::vector<std::uint8_t> widths(font.widths().begin(), font.widths().end());
    require(hash(widths) == 0x8906415ae58ef909ULL,
        "all128 reference width entries, including96 glyph widths, must match");
    require(std::accumulate(widths.begin(), widths.end(), 0U) == 1246,
        "reference font total advance widths changed");
    std::vector<std::uint8_t> pixels;
    std::map<unsigned, std::size_t> counts;
    for (unsigned code = 32; code < 128; ++code) {
        const IndexedGlyph& glyph = font.glyph(code);
        require(glyph.width == font.widths()[code] && glyph.height == 23, "reference glyph metric mismatch");
        pixels.insert(pixels.end(), glyph.indices.begin(), glyph.indices.end());
        for (std::size_t i = 0; i < glyph.indices.size(); ++i) ++counts[glyph.indices[i]];
    }
    require(pixels.size() == 28658 && hash(pixels) == 0x2221934c0e7c873bULL,
        "reference glyph indices/order/orientation changed");
    require(counts.size() == 3 && counts[0] == 20127 && counts[80] == 6169 && counts[105] == 2362,
        "reference raw index distinction changed");
    require(static_cast<std::size_t>(std::count(font.glyph('?').indices.begin(), font.glyph('?').indices.end(), 0)) ==
        font.glyph('?').indices.size(), "reference question-mark resource is expected to be blank");
    require(measure_text(font, "SPACE CADET 0123456789").width == 349,
        "reference sample string must use stored variable widths and two-pixel gaps");
    std::cout << "Asset font passed:96 glyphs,128 width entries, preserved header and all28658 top-down indices.\n";
}
void actual_atlas(const std::string& path)
{
    const GlyphAtlas atlas = load_atlas(path);
    require(atlas.glyphs.size() == 10, "FONT.DAT must contain10 atlas ordinals");
    static const std::uint64_t expected[10] = {
        0xcf3e17f7410a1a73ULL,0xe4b20d2e7a6be03bULL,0xf7bddfeb0aab891dULL,0x7612e85998f64f3bULL,
        0xb000bf3d744898e1ULL,0xb33f74c57418ec29ULL,0xef04b6889c858d9fULL,0x18ca64c78e1b4979ULL,
        0xa29ddcd6f7806b81ULL,0x5294b4b0beab9623ULL};
    for (std::size_t i = 0; i < atlas.glyphs.size(); ++i) {
        const AtlasGlyph& entry = atlas.glyphs[i];
        require(entry.group_index == i && entry.bitmap_field_index == 0 &&
            entry.glyph.width == 15 && entry.glyph.height == 22, "reference atlas location/shape changed");
        require(hash(entry.glyph.indices) == expected[i], "reference atlas ordinal row orientation or indices changed");
    }
    require(atlas.notices.size() == 1 && atlas.notices[0] == "Copyright 1994, Cinematronics",
        "reference atlas notice must remain visible");
    std::cout << "Asset atlas passed:10 raw15x22 glyph ordinals, independent top-down checksums, original notice.\n";
}
}

int main(int argc, char** argv)
{
    if (argc > 3) {
        std::cerr << "Usage: pinball_font_tests [PINBALL2.MID [FONT.DAT]]\n";
        return 2;
    }
    try {
        header_bounds_and_owned_indices();
        clipping_style_spacing_and_lines();
        unsupported_bytes_are_explicit();
        rgba_extent_guards_before_source_storage();
        if (argc >= 2) actual_font(argv[1]);
        if (argc == 3) actual_atlas(argv[2]);
        std::cout << "Bitmap-font tests passed: LE metrics, bounds/width consistency, preserved indices,"
                     " clipped explicit styling, gap/newline layout and unsupported-byte policies.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Bitmap-font test failed: " << error.what() << '\n';
        return 1;
    }
}
