#include "pinball360/bitmap.hpp"
#include "dat_reader.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void expect_error(const std::function<void()>& operation, const std::string& text)
{
    try {
        operation();
    } catch (const pinball360::BitmapError& error) {
        require(std::string(error.what()).find(text) != std::string::npos,
                "wrong failure: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("expected BitmapError containing: " + text);
}

void set16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 255U);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8) & 255U);
}

void set32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8)) & 255U);
}

std::vector<std::uint8_t> synthetic_palette()
{
    // Generated test colors only; no original asset bytes are embedded.
    std::vector<std::uint8_t> bytes(1024, 0);
    for (unsigned i = 0; i < 256; ++i) {
        bytes[i * 4U] = static_cast<std::uint8_t>(i);
        bytes[i * 4U + 1U] = static_cast<std::uint8_t>(i ^ 0x55U);
        bytes[i * 4U + 2U] = static_cast<std::uint8_t>(255U - i);
    }
    return bytes;
}

std::vector<std::uint8_t> synthetic_bitmap()
{
    // Width 3 needs a fourth padding byte on each stored bottom-up row.
    std::vector<std::uint8_t> bytes(22, 0);
    set16(bytes, 1, 3);
    set16(bytes, 3, 2);
    set16(bytes, 5, 0xfffeU); // signed x = -2, decoded without native-word loads
    set16(bytes, 7, 0x0102U); // y = 258 distinguishes byte order
    set32(bytes, 9, 8);
    bytes[13] = 1;
    bytes[14] = 1;
    bytes[15] = 2;
    bytes[16] = 3;
    bytes[17] = 241; // padding must not become a pixel
    bytes[18] = 4;
    bytes[19] = 5;
    bytes[20] = 6;
    bytes[21] = 242;
    return bytes;
}

void check_pixel(const pinball360::Bitmap8& bitmap, std::size_t pixel, unsigned index)
{
    const std::size_t offset = pixel * 4U;
    require(bitmap.pixels[offset] == 255U - index, "red channel must come from palette byte 2");
    require(bitmap.pixels[offset + 1U] == (index ^ 0x55U), "green channel mismatch");
    require(bitmap.pixels[offset + 2U] == index, "blue channel must come from palette byte 0");
    require(bitmap.pixels[offset + 3U] == 255, "reserved palette byte must not produce zero alpha");
}

void palette_and_orientation()
{
    std::vector<std::uint8_t> colors = synthetic_palette();
    const pinball360::Palette256 palette = pinball360::decode_palette(colors.data(), colors.size());
    std::vector<std::uint8_t> bytes = synthetic_bitmap();
    const pinball360::Bitmap8 bitmap = pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette);
    require(bitmap.width == 3 && bitmap.height == 2, "dimensions mismatch");
    require(bitmap.x == -2 && bitmap.y == 258, "signed LE position mismatch");
    require(bitmap.pixels.size() == 24, "padding entered RGBA storage");
    check_pixel(bitmap, 0, 4);
    check_pixel(bitmap, 1, 5);
    check_pixel(bitmap, 2, 6);
    check_pixel(bitmap, 3, 1);
    check_pixel(bitmap, 4, 2);
    check_pixel(bitmap, 5, 3);
    bytes.assign(bytes.size(), 0);
    colors.assign(colors.size(), 0);
    check_pixel(bitmap, 0, 4); // decoded storage owns its data
}

void malformed_and_unsupported()
{
    std::vector<std::uint8_t> colors = synthetic_palette();
    const pinball360::Palette256 palette = pinball360::decode_palette(colors.data(), colors.size());
    expect_error([&]() { pinball360::decode_palette(colors.data(), 1023); }, "exactly 1024");
    expect_error([&]() { pinball360::decode_palette(0, 1024); }, "exactly 1024");
    colors[3] = 1;
    expect_error([&]() { pinball360::decode_palette(colors.data(), colors.size()); }, "reserved");

    std::vector<std::uint8_t> bytes = synthetic_bitmap();
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), 13, palette); }, "header");
    expect_error([&]() { pinball360::decode_bitmap8(0, bytes.size(), palette); }, "header");
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size() - 1U, palette); }, "truncated");
    bytes.push_back(0);
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "trailing");
    bytes = synthetic_bitmap();
    set32(bytes, 9, 6); // tight rows are invalid for this observed layout
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "row storage");
    bytes = synthetic_bitmap();
    set32(bytes, 9, 0x01000008U);
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "row storage");
    bytes = synthetic_bitmap();
    set16(bytes, 1, 0);
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "positive");
    bytes = synthetic_bitmap();
    set16(bytes, 3, 0xffffU);
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "positive");
    bytes = synthetic_bitmap();
    bytes[0] = 1;
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "resolution");
    bytes = synthetic_bitmap();
    bytes[13] = 5;
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "spliced");
    bytes[13] = 3;
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "DIB");
    bytes[13] = 0;
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "flags");
    bytes[13] = 0x81U;
    expect_error([&]() { pinball360::decode_bitmap8(bytes.data(), bytes.size(), palette); }, "flags");
}


std::uint64_t pixel_hash(const std::vector<std::uint8_t>& pixels)
{
    std::uint64_t result = 14695981039346656037ULL;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        result ^= pixels[i];
        result *= 1099511628211ULL;
    }
    return result;
}

void original_asset_integration(const std::string& path)
{
    const pinball::data::DatFile dat = pinball::data::DatFile::load(path);
    const std::vector<pinball::data::Group>& groups = dat.groups();
    pinball360::Palette256 palette;
    std::size_t palette_count = 0;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (std::size_t f = 0; f < groups[g].fields.size(); ++f) {
            if (groups[g].fields[f].type == 5) {
                const pinball::data::ByteView bytes = dat.payload(groups[g].fields[f]);
                palette = pinball360::decode_palette(bytes.data, bytes.size);
                ++palette_count;
            }
        }
    }
    require(palette_count == 1, "reference PINBALL.DAT must contain one palette");
    std::size_t bitmap_count = 0;
    bool checked_table = false;
    bool checked_background = false;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (std::size_t f = 0; f < groups[g].fields.size(); ++f) {
            if (groups[g].fields[f].type != 1)
                continue;
            const pinball::data::ByteView bytes = dat.payload(groups[g].fields[f]);
            const pinball360::Bitmap8 bitmap =
                pinball360::decode_bitmap8(bytes.data, bytes.size, palette);
            ++bitmap_count;
            if (dat.group_name(g) == "table") {
                require(g == 107 && f == 1, "reference table field changed");
                require(bitmap.width == 365 && bitmap.height == 470, "reference table size changed");
                // Checksums were calculated separately from local binary assets;
                // the test contains no original pixel or palette byte sequences.
                require(pixel_hash(bitmap.pixels) == 0x6eb6fe9b39a82439ULL,
                        "reference table RGBA checksum mismatch");
                checked_table = true;
            }
            if (dat.group_name(g) == "background") {
                require(g == 2 && f == 0, "reference background field changed");
                require(bitmap.width == 203 && bitmap.height == 394, "reference background size changed");
                require(pixel_hash(bitmap.pixels) == 0x740e456753cfff01ULL,
                        "reference background RGBA checksum mismatch");
                checked_background = true;
            }
        }
    }
    require(bitmap_count == 318, "reference PINBALL.DAT must contain 318 bitmap fields");
    require(checked_table && checked_background, "reference named bitmap checks did not run");
    std::cout << "Asset integration passed: all 318 bitmaps decoded; table/background "
                 "dimensions, field locations and RGBA checksums match.\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 2) {
        std::cerr << "Usage: pinball_bitmap_tests [PINBALL.DAT]\n";
        return 2;
    }
    try {
        palette_and_orientation();
        malformed_and_unsupported();
        if (argc == 2)
            original_asset_integration(argv[1]);
        std::cout << "Bitmap tests passed: palette channels, opacity, ownership, bottom-up rows, "
                     "odd stride, signed LE coordinates, malformed data, unsupported layouts.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Bitmap test failed: " << error.what() << '\n';
        return 1;
    }
}
