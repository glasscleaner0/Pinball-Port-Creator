#include "bitmap_font.hpp"
#include "pinball360/bitmap.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace pinball {
namespace font {
namespace {

std::uint16_t le16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) |
        (static_cast<std::uint16_t>(bytes[1]) << 8));
}

void need(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t size)
{
    if (offset > bytes.size() || size > bytes.size() - offset) {
        std::ostringstream message;
        message << "truncated bitmap font at byte " << offset;
        throw FontError(message.str());
    }
}

std::uint64_t add(std::uint64_t left, std::uint64_t right)
{
    // Keep layout representable with any signed int draw origin as well.
    const std::uint64_t limit = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) -
        static_cast<std::uint64_t>((std::numeric_limits<int>::max)());
    if (left > limit || right > limit - left) throw FontError("font layout exceeds coordinate range");
    return left + right;
}

const IndexedGlyph* resolve(const BitmapFont& font, unsigned byte, const TextOptions& options)
{
    if (font.contains(byte)) return &font.glyph(byte);
    if (options.unsupported == UnsupportedCharacter::Skip) return 0;
    if (options.unsupported == UnsupportedCharacter::Replace) return &font.glyph(options.replacement);
    std::ostringstream message;
    message << "unsupported ASCII font byte " << byte;
    throw FontError(message.str());
}

struct Placement {
    const IndexedGlyph* glyph;
    std::uint64_t x;
    std::uint64_t y;
};
struct Layout {
    TextMetrics metrics;
    std::vector<Placement> glyphs;
};

Layout layout_text(const BitmapFont& font, const std::string& text, const TextOptions& options)
{
    if (options.unsupported == UnsupportedCharacter::Replace && !font.contains(options.replacement))
        throw FontError("replacement byte has no glyph");
    Layout result;
    result.metrics = TextMetrics();
    if (text.empty()) return result;
    result.metrics.line_count = 1;
    std::uint64_t pen_x = 0, pen_y = 0, line_width = 0;
    const std::uint64_t line_advance = add(font.height(), options.line_gap);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned byte = static_cast<unsigned char>(text[i]);
        const bool crlf = byte == '\r' && i + 1U < text.size() && text[i + 1U] == '\n';
        if (byte == '\n' || crlf) {
            if (crlf) ++i;
            result.metrics.width = (std::max)(result.metrics.width, line_width);
            pen_x = 0;
            line_width = 0;
            pen_y = add(pen_y, line_advance);
            ++result.metrics.line_count;
            continue;
        }
        const IndexedGlyph* glyph = resolve(font, byte, options);
        if (!glyph) continue;
        const Placement placement = {glyph, pen_x, pen_y};
        result.glyphs.push_back(placement);
        line_width = add(pen_x, glyph->width);
        pen_x = add(line_width, font.gap());
        ++result.metrics.glyph_count;
    }
    result.metrics.width = (std::max)(result.metrics.width, line_width);
    result.metrics.height = add(pen_y, font.height());
    return result;
}

} // namespace

FontError::FontError(const std::string& message) : std::runtime_error(message) {}
BitmapFont::BitmapFont() : gap_(0), unknown_(0), height_(0), widths_() {}

BitmapFont BitmapFont::parse(std::vector<std::uint8_t> bytes)
{
    need(bytes, 0, 134);
    BitmapFont result;
    result.gap_ = le16(bytes.data());
    result.unknown_ = le16(bytes.data() + 2);
    result.height_ = le16(bytes.data() + 4);
    if (!result.height_) throw FontError("bitmap font height must be positive");
    std::copy(bytes.begin() + 6, bytes.begin() + 134, result.widths_.begin());
    result.glyphs_.reserve(96);
    std::size_t position = 134;
    for (unsigned code = 32; code < 128; ++code) {
        need(bytes, position, 1);
        const std::uint8_t width = bytes[position++];
        if (width != result.widths_[code]) {
            std::ostringstream message;
            message << "glyph width disagrees with width table for ASCII " << code;
            throw FontError(message.str());
        }
        const std::size_t samples = static_cast<std::size_t>(width) * result.height_;
        need(bytes, position, samples);
        IndexedGlyph glyph;
        glyph.width = width;
        glyph.height = result.height_;
        glyph.source_offset = position;
        glyph.indices.assign(bytes.begin() + position, bytes.begin() + position + samples);
        result.glyphs_.push_back(glyph);
        position += samples;
    }
    if (position != bytes.size()) throw FontError("bitmap font has trailing bytes after ASCII127");
    return result;
}

BitmapFont BitmapFont::load(const std::string& path)
{
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input) throw FontError("cannot open bitmap font: " + path);
    const std::streamoff size = input.tellg();
    std::vector<std::uint8_t> bytes;
    if (size < 0 || static_cast<std::uint64_t>(size) > bytes.max_size() ||
        static_cast<std::uint64_t>(size) > static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)()))
        throw FontError("bitmap font file size is unsupported");
    bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input) throw FontError("cannot read complete bitmap font: " + path);
    return parse(std::move(bytes));
}

std::uint16_t BitmapFont::gap() const { return gap_; }
std::uint16_t BitmapFont::unknown_header_value() const { return unknown_; }
std::uint16_t BitmapFont::height() const { return height_; }
const std::array<std::uint8_t, 128>& BitmapFont::widths() const { return widths_; }
const std::vector<IndexedGlyph>& BitmapFont::glyphs() const { return glyphs_; }
bool BitmapFont::contains(unsigned byte) const { return byte >= 32 && byte < 128; }
const IndexedGlyph& BitmapFont::glyph(unsigned byte) const
{
    if (!contains(byte)) throw FontError("requested byte has no font glyph");
    return glyphs_[byte - 32U];
}

GlyphAtlas decode_atlas(const data::DatFile& dat)
{
    GlyphAtlas result;
    // Decoder validation is reused without assigning any rendering style.
    const pinball360::Palette256 validation_palette = {};
    for (std::size_t g = 0; g < dat.groups().size(); ++g) {
        const std::vector<data::Field>& fields = dat.groups()[g].fields;
        for (std::size_t f = 0; f < fields.size(); ++f) {
            if (fields[f].type == 9) result.notices.push_back(dat.string_value(fields[f]));
            if (fields[f].type != 1) continue;
            const data::ByteView bytes = dat.payload(fields[f]);
            const pinball360::Bitmap8 bitmap = pinball360::decode_bitmap8(bytes.data, bytes.size, validation_palette);
            AtlasGlyph entry;
            entry.group_index = g;
            entry.bitmap_field_index = f;
            entry.stored_header = dat.bitmap_header(fields[f]);
            entry.glyph.width = bitmap.width;
            entry.glyph.height = bitmap.height;
            entry.glyph.source_offset = fields[f].payload_offset + 14U;
            entry.glyph.indices.resize(static_cast<std::size_t>(bitmap.width) * bitmap.height);
            const std::size_t stride = (static_cast<std::size_t>(bitmap.width) + 3U) & ~std::size_t(3U);
            for (std::size_t y = 0; y < bitmap.height; ++y) {
                const std::uint8_t* source = bytes.data + 14U + (bitmap.height - y - 1U) * stride;
                std::copy(source, source + bitmap.width, entry.glyph.indices.begin() + y * bitmap.width);
            }
            result.glyphs.push_back(entry);
        }
    }
    return result;
}
GlyphAtlas load_atlas(const std::string& path) { return decode_atlas(data::DatFile::load(path)); }

FontPaint::FontPaint() : colors(), draw()
{
    draw.transparent_zero = true;
    draw.depth_test = scene::DepthTest::Always;
    draw.write_depth = false;
}
TextOptions::TextOptions() : unsupported(UnsupportedCharacter::Reject), replacement('?'), line_gap(0) {}

TextMetrics measure_text(const BitmapFont& font, const std::string& ascii, const TextOptions& options)
{
    return layout_text(font, ascii, options).metrics;
}

void draw_glyph(scene::Surface& target, const IndexedGlyph& glyph, int x, int y, const FontPaint& paint)
{
    if (!glyph.height) throw FontError("invalid indexed glyph dimensions/storage");
    const std::size_t width = glyph.width;
    const std::size_t height = glyph.height;
    const std::size_t size_limit = (std::numeric_limits<std::size_t>::max)();
    if (width && height > size_limit / width)
        throw FontError("glyph sample extent overflows size_t");
    const std::size_t samples = width * height;
    if (samples > size_limit / 4U)
        throw FontError("glyph RGBA extent overflows size_t");
    const std::size_t rgba_size = samples * 4U;
    scene::Sprite sprite;
    if (rgba_size > sprite.bitmap.pixels.max_size())
        throw FontError("glyph RGBA extent exceeds vector capacity");
    // Validate the derived RGBA extent before checking source storage or
    // allocating/copying: a public oversized glyph can be rejected safely
    // without requiring its potentially multi-gigabyte index vector.
    if (glyph.indices.size() != samples) throw FontError("invalid indexed glyph dimensions/storage");
    if (!glyph.width) return;
    sprite.bitmap.width = glyph.width;
    sprite.bitmap.height = glyph.height;
    sprite.bitmap.x = sprite.bitmap.y = 0;
    sprite.indices = glyph.indices;
    sprite.bitmap.pixels.resize(rgba_size);
    for (std::size_t i = 0; i < samples; ++i) {
        const pinball360::Rgba8 color = paint.colors[glyph.indices[i]];
        sprite.bitmap.pixels[i * 4U] = color.r;
        sprite.bitmap.pixels[i * 4U + 1U] = color.g;
        sprite.bitmap.pixels[i * 4U + 2U] = color.b;
        sprite.bitmap.pixels[i * 4U + 3U] = color.a;
    }
    scene::DrawOptions options = paint.draw;
    options.transparent_zero = true;
    scene::draw_sprite(target, sprite, x, y, options);
}

void draw_text(scene::Surface& target, const BitmapFont& font, const std::string& ascii,
               int x, int y, const FontPaint& paint, const TextOptions& options)
{
    // Validate the complete input before touching the destination surface.
    const Layout layout = layout_text(font, ascii, options);
    for (std::size_t i = 0; i < layout.glyphs.size(); ++i) {
        const Placement& entry = layout.glyphs[i];
        const std::int64_t px = std::int64_t(x) + static_cast<std::int64_t>(entry.x);
        const std::int64_t py = std::int64_t(y) + static_cast<std::int64_t>(entry.y);
        if (px >= target.width || py >= target.height ||
            px + entry.glyph->width <= 0 || py + entry.glyph->height <= 0) continue;
        // A visible glyph lies inside target's int-sized coordinate range.
        draw_glyph(target, *entry.glyph, static_cast<int>(px), static_cast<int>(py), paint);
    }
}

} // namespace font
} // namespace pinball
