#include "dat_reader.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

std::string json_string(const std::string& value) {
    const char digits[] = "0123456789abcdef";
    std::string result = "\"";
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(value[i]);
        if (byte == '"' || byte == '\\') {
            result.push_back('\\');
            result.push_back(static_cast<char>(byte));
        } else if (byte < 0x20 || byte >= 0x80) {
            result += "\\u00";
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 15]);
        } else {
            result.push_back(static_cast<char>(byte));
        }
    }
    result.push_back('"');
    return result;
}

void inspect(const pinball::data::DatFile& file, bool json) {
    const pinball::data::Header& header = file.header();
    std::map<unsigned int, std::size_t> histogram;
    std::size_t field_count = 0;
    for (std::size_t g = 0; g < file.groups().size(); ++g) {
        const std::vector<pinball::data::Field>& fields = file.groups()[g].fields;
        field_count += fields.size();
        for (std::size_t f = 0; f < fields.size(); ++f) {
            ++histogram[fields[f].type];
        }
    }
    const std::vector<std::string> sounds = file.sound_filenames();
    if (json) {
        std::cout << "{\n  \"header\": {\n"
            << "    \"signature\": " << json_string(header.signature) << ",\n"
            << "    \"application\": " << json_string(header.application) << ",\n"
            << "    \"description\": " << json_string(header.description) << ",\n"
            << "    \"declared_file_size\": " << header.declared_file_size << ",\n"
            << "    \"declared_group_count\": " << header.declared_group_count << ",\n"
            << "    \"declared_body_size\": " << header.declared_body_size << ",\n"
            << "    \"extra_header_size\": " << header.extra_header_size << ",\n"
            << "    \"body_offset\": " << header.body_offset << "\n  },\n"
            << "  \"group_count\": " << file.groups().size() << ",\n"
            << "  \"field_count\": " << field_count << ",\n"
            << "  \"field_histogram\": {";
        bool first = true;
        for (std::map<unsigned int, std::size_t>::const_iterator it = histogram.begin();
             it != histogram.end(); ++it) {
            if (!first) std::cout << ',';
            std::cout << "\n    \"" << it->first << "\": " << it->second;
            first = false;
        }
        std::cout << "\n  },\n  \"sound_filename_count\": " << sounds.size()
            << ",\n  \"sound_filenames\": [";
        for (std::size_t i = 0; i < sounds.size(); ++i) {
            if (i != 0) std::cout << ',';
            std::cout << "\n    " << json_string(sounds[i]);
        }
        std::cout << "\n  ]\n}\n";
    } else {
        std::cout << "Signature: " << header.signature
            << "\nApplication: " << header.application
            << "\nDescription: " << header.description
            << "\nFile bytes: " << header.declared_file_size
            << "\nDeclared body bytes: " << header.declared_body_size
            << "\nExtra header bytes: " << header.extra_header_size
            << "\nGroups: " << file.groups().size()
            << "\nFields: " << field_count << "\nField histogram:";
        for (std::map<unsigned int, std::size_t>::const_iterator it = histogram.begin();
             it != histogram.end(); ++it) {
            std::cout << " " << it->first << '=' << it->second;
        }
        std::cout << "\nWAV filenames (" << sounds.size() << "):\n";
        for (std::size_t i = 0; i < sounds.size(); ++i) {
            std::cout << "  " << sounds[i] << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    bool json = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--json") {
            json = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: pinball_dat_inspect [--json] <PINBALL.DAT>\n";
            return 0;
        } else if (path.empty() && !argument.empty() && argument[0] != '-') {
            path = argument;
        } else {
            std::cerr << "Usage: pinball_dat_inspect [--json] <PINBALL.DAT>\n";
            return 2;
        }
    }
    if (path.empty()) {
        std::cerr << "Usage: pinball_dat_inspect [--json] <PINBALL.DAT>\n";
        return 2;
    }
    try {
        inspect(pinball::data::DatFile::load(path), json);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DAT inspection failed: " << error.what() << '\n';
        return 1;
    }
}
