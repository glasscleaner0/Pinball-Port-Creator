#include "pinball360/bitmap.hpp"

#include <limits>
#include <string>

namespace pinball360 {
namespace {

std::uint16_t le16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                    (static_cast<std::uint16_t>(p[1]) << 8));
}

std::int16_t signed_le16(const std::uint8_t* p)
{
    const std::uint16_t value = le16(p);
    if (value <= 0x7fffU)
        return static_cast<std::int16_t>(value);
    return static_cast<std::int16_t>(-1 - static_cast<int>(0xffffU - value));
}

std::uint32_t le32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

BitmapError::BitmapError(const std::string& message) : std::runtime_error(message) {}

Palette256 decode_palette(const std::uint8_t* data, std::size_t size)
{
    if (size != 1024 || data == 0)
        throw BitmapError("type-5 palette must contain exactly 1024 bytes");
    Palette256 palette;
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const std::uint8_t* entry = data + i * 4;
        if (entry[3] != 0)
            throw BitmapError("unsupported type-5 palette: nonzero reserved byte");
        palette[i].r = entry[2];
        palette[i].g = entry[1];
        palette[i].b = entry[0];
        palette[i].a = 255;
    }
    return palette;
}

Bitmap8 decode_bitmap8(const std::uint8_t* data, std::size_t size,
                      const Palette256& palette)
{
    const std::size_t header_size = 14;
    if (data == 0 || size < header_size)
        throw BitmapError("truncated type-1 bitmap header (14 bytes required)");
    if (data[0] != 0)
        throw BitmapError("unsupported bitmap resolution; only resolution 0 is validated");
    const std::int16_t width = signed_le16(data + 1);
    const std::int16_t height = signed_le16(data + 3);
    if (width <= 0 || height <= 0)
        throw BitmapError("bitmap dimensions must both be positive");
    const std::uint8_t flags = data[13];
    if ((flags & 4U) != 0)
        throw BitmapError("unsupported bitmap layout: spliced sprite/depth data");
    if ((flags & 2U) != 0)
        throw BitmapError("unsupported bitmap layout: separate DIB flag is not validated");
    if (flags != 1)
        throw BitmapError("unsupported bitmap flags; only raw aligned flags=1 is validated");

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    const std::size_t stride = (w + 3U) & ~static_cast<std::size_t>(3U);
    if (h > (std::numeric_limits<std::size_t>::max)() / stride)
        throw BitmapError("bitmap row storage size overflows address space");
    const std::size_t expected_size = stride * h;
    const std::uint32_t declared_size = le32(data + 9);
    if (declared_size != expected_size)
        throw BitmapError("bitmap data size does not match four-byte-aligned row storage");
    if (size - header_size != expected_size)
        throw BitmapError("bitmap payload is truncated or contains trailing bytes");
    if (w > (std::numeric_limits<std::size_t>::max)() / h / 4U)
        throw BitmapError("bitmap RGBA storage size overflows address space");

    Bitmap8 result;
    result.width = static_cast<std::uint16_t>(width);
    result.height = static_cast<std::uint16_t>(height);
    result.x = signed_le16(data + 5);
    result.y = signed_le16(data + 7);
    const std::size_t rgba_size = w * h * 4U;
    if (rgba_size > result.pixels.max_size())
        throw BitmapError("bitmap RGBA storage exceeds container capacity");
    result.pixels.resize(rgba_size);
    for (std::size_t row = 0; row < h; ++row) {
        const std::uint8_t* src = data + header_size + (h - row - 1U) * stride;
        std::uint8_t* dst = result.pixels.data() + row * w * 4U;
        for (std::size_t column = 0; column < w; ++column) {
            const Rgba8& color = palette[src[column]];
            dst[column * 4U] = color.r;
            dst[column * 4U + 1U] = color.g;
            dst[column * 4U + 2U] = color.b;
            dst[column * 4U + 3U] = color.a;
        }
    }
    return result;
}

} // namespace pinball360
