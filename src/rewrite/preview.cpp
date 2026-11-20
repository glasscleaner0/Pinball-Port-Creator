#include "dat_reader.hpp"
#include "pinball360/bitmap.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Selection {
    std::size_t group;
    std::size_t field;
};

bool find_named_bitmap(const pinball::data::DatFile& dat, const std::string& name,
                       Selection& selected)
{
    const std::vector<pinball::data::Group>& groups = dat.groups();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (dat.group_name(g) != name)
            continue;
        for (std::size_t f = 0; f < groups[g].fields.size(); ++f) {
            if (groups[g].fields[f].type == 1) {
                selected.group = g;
                selected.field = f;
                return true;
            }
        }
    }
    return false;
}

Selection select_bitmap(const pinball::data::DatFile& dat, const std::string& requested)
{
    Selection result = {0, 0};
    if (!requested.empty()) {
        if (!find_named_bitmap(dat, requested, result))
            throw std::runtime_error("requested group has no type-1 bitmap: " + requested);
        return result;
    }
    // The supplied Windows asset names its playfield 'table'. 'background'
    // is a separate, smaller sidebar bitmap. Report the exact choice below.
    if (find_named_bitmap(dat, "table", result) ||
        find_named_bitmap(dat, "background", result))
        return result;
    const std::vector<pinball::data::Group>& groups = dat.groups();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (std::size_t f = 0; f < groups[g].fields.size(); ++f) {
            const pinball::data::Field& field = groups[g].fields[f];
            if (field.type != 1)
                continue;
            const pinball::data::BitmapHeader header = dat.bitmap_header(field);
            if (header.width >= 128 && header.height >= 128) {
                result.group = g;
                result.field = f;
                return result;
            }
        }
    }
    throw std::runtime_error("no named table/background or bitmap at least 128 by 128 found");
}

Selection select_palette(const pinball::data::DatFile& dat, std::size_t bitmap_group)
{
    const std::vector<pinball::data::Group>& groups = dat.groups();
    Selection result = {0, 0};
    std::size_t count = 0;
    for (std::size_t f = 0; f < groups[bitmap_group].fields.size(); ++f) {
        if (groups[bitmap_group].fields[f].type == 5) {
            result.group = bitmap_group;
            result.field = f;
            ++count;
        }
    }
    if (count == 1)
        return result;
    if (count > 1)
        throw std::runtime_error("multiple palettes in selected bitmap group are unsupported");
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (std::size_t f = 0; f < groups[g].fields.size(); ++f) {
            if (groups[g].fields[f].type == 5) {
                result.group = g;
                result.field = f;
                ++count;
            }
        }
    }
    if (count != 1)
        throw std::runtime_error("preview needs one unambiguous type-5 palette");
    return result;
}

void append16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 255U));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 255U));
}

void append32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 255U));
}

void write_bmp(const std::string& path, const pinball360::Bitmap8& bitmap)
{
    const std::size_t row_size = (static_cast<std::size_t>(bitmap.width) * 3U + 3U) &
                                 ~static_cast<std::size_t>(3U);
    const std::uint64_t data_size = static_cast<std::uint64_t>(row_size) * bitmap.height;
    if (data_size + 54U > (std::numeric_limits<std::uint32_t>::max)())
        throw std::runtime_error("BMP output exceeds the 32-bit file size limit");
    std::vector<std::uint8_t> header;
    header.push_back('B');
    header.push_back('M');
    append32(header, static_cast<std::uint32_t>(data_size + 54U));
    append16(header, 0);
    append16(header, 0);
    append32(header, 54);
    append32(header, 40);
    append32(header, bitmap.width);
    append32(header, bitmap.height);
    append16(header, 1);
    append16(header, 24);
    append32(header, 0);
    append32(header, static_cast<std::uint32_t>(data_size));
    append32(header, 2835);
    append32(header, 2835);
    append32(header, 0);
    append32(header, 0);
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot open preview output: " + path);
    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));
    std::vector<std::uint8_t> row(row_size, 0);
    for (std::size_t stored_row = 0; stored_row < bitmap.height; ++stored_row) {
        const std::size_t top_row = bitmap.height - stored_row - 1U;
        const std::uint8_t* src = bitmap.pixels.data() + top_row * bitmap.width * 4U;
        for (std::size_t x = 0; x < bitmap.width; ++x) {
            row[x * 3U] = src[x * 4U + 2U];
            row[x * 3U + 1U] = src[x * 4U + 1U];
            row[x * 3U + 2U] = src[x * 4U];
        }
        output.write(reinterpret_cast<const char*>(row.data()),
                     static_cast<std::streamsize>(row.size()));
    }
    output.flush();
    if (!output)
        throw std::runtime_error("failed to write preview output: " + path);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: pinball_dat_preview <PINBALL.DAT> <output.bmp> [group-name]\n";
        return 2;
    }
    try {
        const pinball::data::DatFile dat = pinball::data::DatFile::load(argv[1]);
        const Selection selected = select_bitmap(dat, argc == 4 ? argv[3] : "");
        const Selection colors = select_palette(dat, selected.group);
        const pinball::data::Field& field = dat.groups()[selected.group].fields[selected.field];
        const pinball::data::Field& palette_field = dat.groups()[colors.group].fields[colors.field];
        const pinball::data::ByteView palette_bytes = dat.payload(palette_field);
        const pinball360::Palette256 palette =
            pinball360::decode_palette(palette_bytes.data, palette_bytes.size);
        const pinball::data::ByteView bitmap_bytes = dat.payload(field);
        const pinball360::Bitmap8 bitmap =
            pinball360::decode_bitmap8(bitmap_bytes.data, bitmap_bytes.size, palette);
        write_bmp(argv[2], bitmap);
        std::cout << "DAT bitmap preview; this is not a playable game.\n"
                  << "Group " << selected.group << " name='" << dat.group_name(selected.group)
                  << "', field " << selected.field << " (type 1), payload offset "
                  << field.payload_offset << ".\n"
                  << "Dimensions " << bitmap.width << 'x' << bitmap.height << ", stored position ("
                  << bitmap.x << ',' << bitmap.y << "), RGBA8 top-down.\n"
                  << "Palette group " << colors.group << ", field " << colors.field
                  << " (type 5).\n"
                  << "Wrote 24-bit BMP: " << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Preview failed: " << error.what() << '\n';
        return 1;
    }
}
