#ifndef PINBALL360_REWRITE_BITMAP_HPP
#define PINBALL360_REWRITE_BITMAP_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace pinball360 {

struct Rgba8 {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

typedef std::array<Rgba8, 256> Palette256;

struct Bitmap8 {
    std::uint16_t width;
    std::uint16_t height;
    std::int16_t x;
    std::int16_t y;
    // Owned, tightly packed RGBA bytes, with the top image row first.
    // No native-word pixel representation or dependency on host endianness.
    std::vector<std::uint8_t> pixels;
};

class BitmapError : public std::runtime_error {
public:
    explicit BitmapError(const std::string& message);
};

// A DAT type-5 payload contains exactly 256 B,G,R,reserved entries.
// The validated assets use zero reserved bytes; output alpha is always 255.
Palette256 decode_palette(const std::uint8_t* data, std::size_t size);

// Decode a complete DAT type-1 payload, including its 14-byte LE header.
// Only the observed resolution-0, flags-1 layout is supported: bottom-up
// 8-bit indexed rows padded to a multiple of four bytes. Other layouts fail
// explicitly, including spliced sprite/depth data and the separate DIB flag.
Bitmap8 decode_bitmap8(const std::uint8_t* data, std::size_t size,
                      const Palette256& palette);

} // namespace pinball360

#endif
