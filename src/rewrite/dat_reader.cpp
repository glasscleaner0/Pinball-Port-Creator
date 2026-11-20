#include "dat_reader.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace pinball {
namespace data {
namespace {

const std::size_t fixed_header_size = 183;

std::string error_text(std::size_t offset, const std::string& message) {
    std::ostringstream out;
    out << message << " at byte " << offset;
    return out.str();
}

void require_bytes(const std::vector<std::uint8_t>& bytes,
                   std::size_t offset, std::size_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        throw ParseError(offset, "truncated data");
    }
}

std::uint16_t unsigned_short(const std::vector<std::uint8_t>& bytes,
                             std::size_t offset) {
    require_bytes(bytes, offset, 2);
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

std::uint32_t unsigned_word(const std::vector<std::uint8_t>& bytes,
                           std::size_t offset) {
    require_bytes(bytes, offset, 4);
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::int16_t signed_short(const std::vector<std::uint8_t>& bytes,
                          std::size_t offset) {
    const std::uint16_t value = unsigned_short(bytes, offset);
    const std::int32_t result = value < 0x8000u ? value :
        static_cast<std::int32_t>(value) - 65536;
    return static_cast<std::int16_t>(result);
}

std::string bounded_string(const std::vector<std::uint8_t>& bytes,
                           std::size_t offset, std::size_t size) {
    require_bytes(bytes, offset, size);
    std::string result;
    for (std::size_t i = 0; i < size && bytes[offset + i] != 0; ++i) {
        result.push_back(static_cast<char>(bytes[offset + i]));
    }
    return result;
}

std::string lower_ascii(std::string text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] >= 'A' && text[i] <= 'Z') {
            text[i] = static_cast<char>(text[i] - 'A' + 'a');
        }
    }
    return text;
}

void require_type(const Field& field, std::uint8_t type) {
    if (field.type != type) {
        throw ParseError(field.header_offset, "field has the wrong type");
    }
}

} // namespace

ParseError::ParseError(std::size_t offset, const std::string& message)
    : std::runtime_error(error_text(offset, message)), offset_(offset) {}

std::size_t ParseError::offset() const { return offset_; }

DatFile::DatFile() : header_() {}

DatFile DatFile::parse(std::vector<std::uint8_t> bytes) {
    require_bytes(bytes, 0, fixed_header_size);
    const char signature[] = "PARTOUT(4.0)RESOURCE";
    for (std::size_t i = 0; i < sizeof(signature); ++i) {
        if (bytes[i] != static_cast<std::uint8_t>(signature[i])) {
            throw ParseError(i, "invalid DAT signature");
        }
    }

    DatFile result;
    result.bytes_ = std::move(bytes);
    const std::vector<std::uint8_t>& input = result.bytes_;
    result.header_.signature = bounded_string(input, 0, 21);
    result.header_.application = bounded_string(input, 21, 50);
    result.header_.description = bounded_string(input, 71, 100);
    result.header_.declared_file_size = unsigned_word(input, 171);
    result.header_.declared_group_count = unsigned_short(input, 175);
    result.header_.declared_body_size = unsigned_word(input, 177);
    result.header_.extra_header_size = unsigned_short(input, 181);
    if (result.header_.declared_file_size != input.size()) {
        throw ParseError(171, "declared file size does not match input");
    }
    // The extra-header inclusion convention is retained rather than inferred.
    if (result.header_.declared_body_size > input.size() - fixed_header_size) {
        throw ParseError(177, "declared body size exceeds available bytes");
    }
    require_bytes(input, fixed_header_size, result.header_.extra_header_size);
    std::size_t position = fixed_header_size + result.header_.extra_header_size;
    result.header_.body_offset = position;
    if (result.header_.declared_group_count > input.size() - position) {
        throw ParseError(position, "group count exceeds available bytes");
    }
    result.groups_.reserve(result.header_.declared_group_count);
    for (std::size_t group_index = 0;
         group_index < result.header_.declared_group_count; ++group_index) {
        require_bytes(input, position, 1);
        Group group;
        group.header_offset = position;
        const std::uint8_t field_count = input[position++];
        group.fields.reserve(field_count);
        for (std::size_t field_index = 0; field_index < field_count; ++field_index) {
            require_bytes(input, position, 1);
            Field field;
            field.header_offset = position;
            field.type = input[position++];
            if (field.type > 13) {
                throw ParseError(field.header_offset, "unknown field type");
            }
            if (field.type == 0 || field.type == 2) {
                field.payload_size = 2;
            } else if (field.type == 13) {
                field.payload_size = 0;
            } else {
                field.payload_size = unsigned_word(input, position);
                position += 4;
            }
            field.payload_offset = position;
            require_bytes(input, position, field.payload_size);
            if (field.type == 10 && field.payload_size % 2 != 0) {
                throw ParseError(position, "short array has an incomplete element");
            }
            if (field.type == 11 && field.payload_size % 4 != 0) {
                throw ParseError(position, "float array has an incomplete element");
            }
            position += field.payload_size;
            group.fields.push_back(field);
        }
        result.groups_.push_back(std::move(group));
    }
    if (position != input.size()) {
        throw ParseError(position, "bytes remain after the declared groups");
    }
    return result;
}

DatFile DatFile::load(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open DAT file: " + path);
    }
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) >
            std::numeric_limits<std::uint32_t>::max() ||
        static_cast<std::uintmax_t>(length) > std::numeric_limits<std::size_t>::max() ||
        static_cast<std::uintmax_t>(length) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("DAT file size is not supported: " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(&bytes[0]),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            throw std::runtime_error("cannot read complete DAT file: " + path);
        }
    }
    return parse(std::move(bytes));
}

const Header& DatFile::header() const { return header_; }
const std::vector<Group>& DatFile::groups() const { return groups_; }
const std::vector<std::uint8_t>& DatFile::bytes() const { return bytes_; }

ByteView DatFile::payload(const Field& field) const {
    require_bytes(bytes_, field.payload_offset, field.payload_size);
    ByteView view;
    view.data = bytes_.empty() ? NULL : &bytes_[0] + field.payload_offset;
    view.size = field.payload_size;
    return view;
}

std::string DatFile::string_value(const Field& field) const {
    if (field.type != 3 && field.type != 9) {
        throw ParseError(field.header_offset, "field is not a string");
    }
    return bounded_string(bytes_, field.payload_offset, field.payload_size);
}

std::string DatFile::group_name(std::size_t group_index) const {
    if (group_index >= groups_.size()) {
        throw std::out_of_range("group index");
    }
    const std::vector<Field>& fields = groups_[group_index].fields;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].type == 3) {
            return string_value(fields[i]);
        }
    }
    return std::string();
}

std::int16_t DatFile::short_value(const Field& field, std::size_t index) const {
    if (field.type != 0 && field.type != 2 && field.type != 10) {
        throw ParseError(field.header_offset, "field is not a short value or array");
    }
    payload(field);
    if (field.payload_size % 2 != 0 || index >= field.payload_size / 2) {
        throw std::out_of_range("short element index");
    }
    return signed_short(bytes_, field.payload_offset + index * 2);
}

float DatFile::float_value(const Field& field, std::size_t index) const {
    require_type(field, 11);
    payload(field);
    if (field.payload_size % 4 != 0 || index >= field.payload_size / 4) {
        throw std::out_of_range("float element index");
    }
    if (sizeof(float) != 4 || !std::numeric_limits<float>::is_iec559) {
        throw std::runtime_error("host does not support 32-bit IEEE float");
    }
    const std::uint32_t bits = unsigned_word(bytes_, field.payload_offset + index * 4);
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(bits));
    return result;
}

BitmapHeader DatFile::bitmap_header(const Field& field) const {
    require_type(field, 1);
    payload(field);
    if (field.payload_size < 14) {
        throw ParseError(field.payload_offset, "bitmap header is truncated");
    }
    const std::size_t offset = field.payload_offset;
    BitmapHeader result;
    result.resolution = bytes_[offset];
    result.width = signed_short(bytes_, offset + 1);
    result.height = signed_short(bytes_, offset + 3);
    result.x = signed_short(bytes_, offset + 5);
    result.y = signed_short(bytes_, offset + 7);
    result.data_size = unsigned_word(bytes_, offset + 9);
    result.flags = bytes_[offset + 13];
    return result;
}

DepthHeader DatFile::depth_header(const Field& field) const {
    require_type(field, 12);
    payload(field);
    if (field.payload_size < 14) {
        throw ParseError(field.payload_offset, "depth header is truncated");
    }
    const std::size_t offset = field.payload_offset;
    DepthHeader result;
    result.width = signed_short(bytes_, offset);
    result.height = signed_short(bytes_, offset + 2);
    result.stride = signed_short(bytes_, offset + 4);
    result.unknown_0 = unsigned_word(bytes_, offset + 6);
    result.unknown_1 = signed_short(bytes_, offset + 10);
    result.unknown_2 = signed_short(bytes_, offset + 12);
    return result;
}

std::vector<std::string> DatFile::sound_filenames() const {
    std::map<std::string, std::string> unique;
    for (std::size_t g = 0; g < groups_.size(); ++g) {
        for (std::size_t f = 0; f < groups_[g].fields.size(); ++f) {
            const Field& field = groups_[g].fields[f];
            if (field.type != 9) {
                continue;
            }
            const std::string value = string_value(field);
            const std::string key = lower_ascii(value);
            if (key.size() >= 4 && key.compare(key.size() - 4, 4, ".wav") == 0 &&
                unique.find(key) == unique.end()) {
                unique.insert(std::make_pair(key, value));
            }
        }
    }
    std::vector<std::string> result;
    result.reserve(unique.size());
    for (std::map<std::string, std::string>::const_iterator it = unique.begin();
         it != unique.end(); ++it) {
        result.push_back(it->second);
    }
    return result;
}

} // namespace data
} // namespace pinball

