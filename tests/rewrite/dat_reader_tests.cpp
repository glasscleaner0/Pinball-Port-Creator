#include "dat_reader.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

typedef std::vector<std::uint8_t> Bytes;
using pinball::data::DatFile;
using pinball::data::Field;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value & 255);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.at(offset + i) = static_cast<std::uint8_t>((value >> (i * 8)) & 255);
    }
}

void append32(Bytes& bytes, std::uint32_t value) {
    const std::size_t start = bytes.size();
    bytes.resize(start + 4);
    put32(bytes, start, value);
}

Bytes text_bytes(const std::string& text) {
    Bytes bytes(text.begin(), text.end());
    bytes.push_back(0);
    return bytes;
}

Bytes base_header(std::uint16_t groups = 0) {
    Bytes bytes(183, 0);
    const char signature[] = "PARTOUT(4.0)RESOURCE";
    for (std::size_t i = 0; i < sizeof(signature); ++i) {
        bytes[i] = static_cast<std::uint8_t>(signature[i]);
    }
    const std::string app = "Synthetic reader fixture";
    for (std::size_t i = 0; i < app.size(); ++i) bytes[21 + i] = app[i];
    put16(bytes, 175, groups);
    return bytes;
}

void seal(Bytes& bytes) {
    put32(bytes, 171, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, 177, static_cast<std::uint32_t>(bytes.size() - 183));
}

void add_variable(Bytes& bytes, std::uint8_t type, const Bytes& value) {
    bytes.push_back(type);
    append32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

Bytes fixture() {
    Bytes bytes = base_header(1);
    bytes.push_back(16);
    add_variable(bytes, 3, text_bytes("test_group"));
    bytes.push_back(0); bytes.push_back(0x34); bytes.push_back(0x12);
    bytes.push_back(2); bytes.push_back(0x00); bytes.push_back(0x80);
    add_variable(bytes, 10, Bytes{0xff, 0x7f, 0x00, 0x80, 0xff, 0xff});
    add_variable(bytes, 11, Bytes{0x00, 0x00, 0x80, 0x3f,
                                 0x00, 0x00, 0x20, 0xc0,
                                 0x00, 0x00, 0x00, 0x80});
    Bytes bitmap(14, 0);
    bitmap[0] = 7;
    put16(bitmap, 1, 123); put16(bitmap, 3, 456);
    put16(bitmap, 5, 0xfffe); put16(bitmap, 7, 0x8000);
    put32(bitmap, 9, 0x78563412u); bitmap[13] = 0xa5;
    add_variable(bytes, 1, bitmap);
    Bytes depth(14, 0);
    put16(depth, 0, 33); put16(depth, 2, 44); put16(depth, 4, 48);
    put32(depth, 6, 0xfedcba98u);
    put16(depth, 10, 0xffff); put16(depth, 12, 0x8000);
    add_variable(bytes, 12, depth);
    add_variable(bytes, 9, text_bytes("Tone.WAV"));
    add_variable(bytes, 9, text_bytes("tone.wav"));
    add_variable(bytes, 9, text_bytes("second.wav"));
    bytes.push_back(13);
    for (std::uint8_t type = 4; type <= 8; ++type) {
        add_variable(bytes, type, Bytes{0x00, type, 0xfe});
    }
    seal(bytes);
    return bytes;
}

const Field& field_of_type(const DatFile& file, std::uint8_t type) {
    const std::vector<Field>& fields = file.groups().at(0).fields;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].type == type) return fields[i];
    }
    throw std::runtime_error("test fixture field missing");
}

void expect_parse_error(Bytes bytes, const std::string& label) {
    try {
        DatFile::parse(std::move(bytes));
    } catch (const pinball::data::ParseError&) {
        return;
    }
    throw std::runtime_error("malformed input accepted: " + label);
}

void expect_error(const std::function<void()>& action, const std::string& label) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid access accepted: " + label);
}

void test_decoding_and_ownership() {
    const Bytes original = fixture();
    DatFile file = DatFile::parse(original);
    check(file.header().application == "Synthetic reader fixture", "header text");
    check(file.header().body_offset == 183, "body offset");
    check(file.groups().size() == 1 && file.groups()[0].fields.size() == 16,
          "group and field counts");
    check(file.group_name(0) == "test_group", "group name");
    check(file.short_value(field_of_type(file, 0), 0) == 0x1234, "fixed short");
    check(file.short_value(field_of_type(file, 2), 0) == -32768, "fixed negative short");
    const Field& shorts = field_of_type(file, 10);
    check(file.short_value(shorts, 0) == 32767, "positive short limit");
    check(file.short_value(shorts, 1) == -32768, "negative short limit");
    check(file.short_value(shorts, 2) == -1, "negative short");
    const Field& floats = field_of_type(file, 11);
    check(file.float_value(floats, 0) == 1.0f, "IEEE float one");
    check(file.float_value(floats, 1) == -2.5f, "IEEE negative float");
    check(std::signbit(file.float_value(floats, 2)), "IEEE signed zero");
    const pinball::data::BitmapHeader bitmap = file.bitmap_header(field_of_type(file, 1));
    check(bitmap.resolution == 7 && bitmap.width == 123 && bitmap.height == 456 &&
          bitmap.x == -2 && bitmap.y == -32768 && bitmap.data_size == 0x78563412u &&
          bitmap.flags == 0xa5, "bitmap header offsets and endian decoding");
    const pinball::data::DepthHeader depth = file.depth_header(field_of_type(file, 12));
    check(depth.width == 33 && depth.height == 44 && depth.stride == 48 &&
          depth.unknown_0 == 0xfedcba98u && depth.unknown_1 == -1 &&
          depth.unknown_2 == -32768, "depth header offsets and endian decoding");
    const pinball::data::ByteView opaque = file.payload(field_of_type(file, 5));
    check(opaque.size == 3 && opaque.data[0] == 0 && opaque.data[1] == 5 &&
          opaque.data[2] == 0xfe, "opaque bytes retained");
    check(file.payload(field_of_type(file, 13)).size == 0, "zero-byte field");
    const std::vector<std::string> sounds = file.sound_filenames();
    check(sounds.size() == 2 && sounds[0] == "second.wav" && sounds[1] == "Tone.WAV",
          "case-insensitive sorted unique WAV inventory");
    check(file.bytes() == original, "original bytes retained");
    DatFile copied = file;
    DatFile moved = std::move(copied);
    check(moved.group_name(0) == "test_group" &&
          moved.payload(field_of_type(moved, 5)).data[2] == 0xfe,
          "owned offsets survive copy and move");
    expect_error([&file, &shorts]() { file.short_value(shorts, 3); }, "short index");
    expect_error([&file, &floats]() { file.float_value(floats, 3); }, "float index");
    expect_error([&file, &shorts]() { file.float_value(shorts, 0); }, "wrong type");
    expect_error([&file]() { file.group_name(1); }, "group index");
    Field invalid = field_of_type(file, 5);
    invalid.payload_offset = std::numeric_limits<std::size_t>::max();
    expect_error([&file, &invalid]() { file.payload(invalid); }, "overflowing offset");
    invalid.payload_offset = 1;
    invalid.payload_size = std::numeric_limits<std::size_t>::max();
    expect_error([&file, &invalid]() { file.payload(invalid); }, "overflowing size");
}

void test_boundaries() {
    Bytes empty = base_header(); seal(empty);
    check(DatFile::parse(empty).groups().empty(), "empty DAT body");
    Bytes extra = base_header(1);
    put16(extra, 181, 3);
    extra.push_back(0xa1); extra.push_back(0xb2); extra.push_back(0xc3);
    extra.push_back(0); seal(extra);
    check(DatFile::parse(extra).groups()[0].header_offset == 186, "extra header skipped");
    // Either observed body-size convention is retained without inventing a rule.
    put32(extra, 177, 1);
    check(DatFile::parse(extra).header().declared_body_size == 1, "body size retained");
    const Bytes complete = fixture();
    for (std::size_t size = 0; size < complete.size(); ++size) {
        Bytes prefix(complete.begin(), complete.begin() + size);
        if (size >= 183) seal(prefix);
        expect_parse_error(prefix, "every truncated fixture prefix");
    }
    Bytes bad = complete; bad[20] = 1;
    expect_parse_error(bad, "signature terminator");
    bad = complete; put32(bad, 171, static_cast<std::uint32_t>(bad.size() - 1));
    expect_parse_error(bad, "file size mismatch");
    bad = complete; put32(bad, 177, std::numeric_limits<std::uint32_t>::max());
    expect_parse_error(bad, "oversized body");
    bad = complete; put16(bad, 181, 65535);
    expect_parse_error(bad, "oversized extra header");
    bad = complete; put16(bad, 175, 65535);
    expect_parse_error(bad, "oversized group count");
    bad = complete; put16(bad, 175, 0);
    expect_parse_error(bad, "unaccounted groups");
    bad = complete; bad.push_back(0); seal(bad);
    expect_parse_error(bad, "trailing bytes");
    bad = complete; bad[184] = 14;
    expect_parse_error(bad, "unknown field type");
    bad = complete; put32(bad, 185, std::numeric_limits<std::uint32_t>::max());
    expect_parse_error(bad, "oversized payload");
    for (std::uint8_t type = 10; type <= 11; ++type) {
        Bytes misaligned = base_header(1); misaligned.push_back(1);
        add_variable(misaligned, type, Bytes{0}); seal(misaligned);
        expect_parse_error(misaligned, "misaligned typed array");
    }
    Bytes arrays = base_header(1); arrays.push_back(2);
    add_variable(arrays, 10, Bytes()); add_variable(arrays, 11, Bytes()); seal(arrays);
    check(DatFile::parse(arrays).groups()[0].fields.size() == 2, "empty typed arrays");
    Bytes short_bitmap = base_header(1); short_bitmap.push_back(1);
    add_variable(short_bitmap, 1, Bytes{0}); seal(short_bitmap);
    DatFile small = DatFile::parse(short_bitmap);
    expect_error([&small]() { small.bitmap_header(small.groups()[0].fields[0]); },
                 "truncated bitmap metadata access");
    // A bounded field string never reads into the next field without a NUL.
    Bytes text = base_header(1); text.push_back(1);
    add_variable(text, 9, Bytes{'a', 'b', 'c'}); seal(text);
    const DatFile bounded = DatFile::parse(text);
    check(bounded.string_value(bounded.groups()[0].fields[0]) == "abc", "bounded string");
}

void test_local_asset(const std::string& path, bool known_pinball) {
    const DatFile file = DatFile::load(path);
    std::size_t count = 0;
    for (std::size_t g = 0; g < file.groups().size(); ++g) {
        for (std::size_t f = 0; f < file.groups()[g].fields.size(); ++f) {
            const Field& field = file.groups()[g].fields[f];
            const pinball::data::ByteView bytes = file.payload(field);
            check(bytes.size == field.payload_size, "asset payload bounds");
            if (field.type == 10 && field.payload_size != 0)
                file.short_value(field, field.payload_size / 2 - 1);
            if (field.type == 11 && field.payload_size != 0)
                file.float_value(field, field.payload_size / 4 - 1);
            if (field.type == 1) file.bitmap_header(field);
            if (field.type == 12) file.depth_header(field);
            ++count;
        }
    }
    if (known_pinball) {
        check(file.groups().size() == 541, "reference table group count");
        check(count == 2255, "reference table field count");
        check(file.sound_filenames().size() == 47, "reference WAV inventory count");
    }
    std::cout << "Asset parsed: " << file.groups().size() << " groups, " << count
              << " fields, " << file.sound_filenames().size() << " WAV names\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            test_decoding_and_ownership();
            test_boundaries();
            std::cout << "DAT reader synthetic tests passed\n";
            return 0;
        }
        if (argc == 3 && (std::string(argv[1]) == "--real" ||
                          std::string(argv[1]) == "--file")) {
            test_local_asset(argv[2], std::string(argv[1]) == "--real");
            return 0;
        }
        std::cerr << "Usage: pinball_dat_tests [--real <PINBALL.DAT> | --file <DAT>]\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "DAT reader test failed: " << error.what() << '\n';
        return 1;
    }
}
