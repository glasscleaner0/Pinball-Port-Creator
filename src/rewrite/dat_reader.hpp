#ifndef PINBALL_REWRITE_DAT_READER_HPP
#define PINBALL_REWRITE_DAT_READER_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace pinball {
namespace data {

// Every offset is measured from the beginning of the owned DAT byte vector.
struct Header {
    std::string signature;
    std::string application;
    std::string description;
    std::uint32_t declared_file_size;
    std::uint16_t declared_group_count;
    std::uint32_t declared_body_size;
    std::uint16_t extra_header_size;
    std::size_t body_offset;
};

struct Field {
    std::uint8_t type;
    std::size_t header_offset;
    std::size_t payload_offset;
    std::size_t payload_size;
};

struct Group {
    std::size_t header_offset;
    std::vector<Field> fields;
};

struct ByteView {
    const std::uint8_t* data;
    std::size_t size;
};

struct BitmapHeader {
    std::uint8_t resolution;
    std::int16_t width;
    std::int16_t height;
    std::int16_t x;
    std::int16_t y;
    std::uint32_t data_size;
    std::uint8_t flags;
};

// This describes the 14-byte Windows DAT depth header only.
struct DepthHeader {
    std::int16_t width;
    std::int16_t height;
    std::int16_t stride;
    std::uint32_t unknown_0;
    std::int16_t unknown_1;
    std::int16_t unknown_2;
};

class ParseError : public std::runtime_error {
public:
    ParseError(std::size_t offset, const std::string& message);
    std::size_t offset() const;
private:
    std::size_t offset_;
};

class DatFile {
public:
    // Parsing consumes the vector; the resulting object owns all payload bytes.
    // Malformed input raises ParseError. File I/O failures raise runtime_error.
    static DatFile parse(std::vector<std::uint8_t> bytes);
    static DatFile load(const std::string& path);

    const Header& header() const;
    const std::vector<Group>& groups() const;
    const std::vector<std::uint8_t>& bytes() const;
    ByteView payload(const Field& field) const;

    // Strings are bounded by the payload and end at the first NUL, if present.
    std::string string_value(const Field& field) const;
    std::string group_name(std::size_t group_index) const;
    std::int16_t short_value(const Field& field, std::size_t index) const;
    float float_value(const Field& field, std::size_t index) const;
    BitmapHeader bitmap_header(const Field& field) const;
    DepthHeader depth_header(const Field& field) const;

    // Unique type-9 WAV filenames, compared case-insensitively; original spelling.
    std::vector<std::string> sound_filenames() const;

private:
    DatFile();
    Header header_;
    std::vector<Group> groups_;
    std::vector<std::uint8_t> bytes_;
};

} // namespace data
} // namespace pinball

#endif
