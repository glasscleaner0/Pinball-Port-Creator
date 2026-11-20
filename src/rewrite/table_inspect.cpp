#include "table_model.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using pinball::table::TableModel;

void quoted(std::ostream& out, const std::string& value) {
    const char hex[] = "0123456789abcdef";
    out << '"';
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"';
}

void indices(std::ostream& out, const std::vector<std::size_t>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        out << values[i];
    }
    out << ']';
}

void depth_header(std::ostream& out, const pinball::data::DepthHeader& value) {
    out << "{\"width\":" << value.width << ",\"height\":" << value.height
        << ",\"stride\":" << value.stride << ",\"unknown_0\":" << value.unknown_0
        << ",\"unknown_1\":" << value.unknown_1 << ",\"unknown_2\":" << value.unknown_2 << '}';
}

void image(std::ostream& out, const pinball::table::ImageState& value) {
    out << "{\"state_index\":" << value.state_index << ",\"group\":" << value.group_index
        << ",\"bitmap_index\":" << value.bitmap_index << ",\"bitmap_field\":"
        << value.bitmap_field_index << ",\"bitmap\":{\"resolution\":"
        << static_cast<unsigned int>(value.bitmap.resolution)
        << ",\"width\":" << value.bitmap.width << ",\"height\":" << value.bitmap.height
        << ",\"x\":" << value.bitmap.x << ",\"y\":" << value.bitmap.y
        << ",\"data_size\":" << value.bitmap.data_size << ",\"flags\":"
        << static_cast<unsigned int>(value.bitmap.flags) << "},\"depth\":";
    if (value.has_depth) {
        out << "{\"field\":" << value.depth_field_index << ",\"header\":";
        depth_header(out, value.depth);
        out << '}';
    } else out << "null";
    out << '}';
}

void inventory_group(std::ostream& out, const TableModel& table, std::size_t index) {
    const pinball::table::GroupInventory& group = table.group(index);
    const std::vector<pinball::data::Field>& fields = table.dat().groups()[index].fields;
    out << "{\"index\":" << index << ",\"name\":";
    quoted(out, group.name);
    out << ",\"record_marker\":";
    if (group.has_record_marker) out << group.record_marker; else out << "null";
    out << ",\"leading_100_count\":";
    if (group.has_declared_state_count) out << group.declared_state_count; else out << "null";
    out << ",\"state_groups\":";
    indices(out, group.state_group_indices);
    out << ",\"images\":[";
    for (std::size_t i = 0; i < group.images.size(); ++i) {
        if (i) out << ',';
        image(out, group.images[i]);
    }
    out << "],\"image_sequence\":[";
    const std::vector<pinball::table::ImageState>& states = table.image_states(index);
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (i) out << ',';
        image(out, states[i]);
    }
    out << "],\"depth_fields\":[";
    for (std::size_t i = 0; i < group.depth_field_indices.size(); ++i) {
        if (i) out << ',';
        const std::size_t field_index = group.depth_field_indices[i];
        bool opaque = false;
        for (std::size_t o = 0; o < group.opaque_depth_field_indices.size(); ++o) {
            if (group.opaque_depth_field_indices[o] == field_index) opaque = true;
        }
        out << "{\"field\":" << field_index << ",\"opaque\":" << (opaque ? "true" : "false")
            << ",\"header\":";
        depth_header(out, table.dat().depth_header(fields[field_index]));
        out << '}';
    }
    out << "],\"short_records\":[";
    for (std::size_t r = 0; r < group.short_records.size(); ++r) {
        if (r) out << ',';
        const pinball::table::ShortRecord& record = group.short_records[r];
        out << "{\"field\":" << record.field_index << ",\"type\":"
            << static_cast<unsigned int>(record.field_type) << ",\"values\":[";
        for (std::size_t i = 0; i < record.values.size(); ++i) {
            if (i) out << ',';
            out << record.values[i];
        }
        out << "]}";
    }
    out << "],\"float_records\":[";
    for (std::size_t r = 0; r < group.float_records.size(); ++r) {
        if (r) out << ',';
        const pinball::table::FloatRecord& record = group.float_records[r];
        out << "{\"field\":" << record.field_index << ",\"values\":[";
        for (std::size_t i = 0; i < record.values.size(); ++i) {
            if (i) out << ',';
            if (std::isfinite(record.values[i])) out << record.values[i];
            else out << "null"; // Exact bits accompany every value, including NaNs.
        }
        out << "],\"ieee754_bits\":[";
        const pinball::data::ByteView bytes = table.dat().payload(fields[record.field_index]);
        for (std::size_t i = 0; i < record.values.size(); ++i) {
            if (i) out << ',';
            const std::size_t at = i * 4;
            const std::uint32_t bits = static_cast<std::uint32_t>(bytes.data[at]) |
                (static_cast<std::uint32_t>(bytes.data[at + 1]) << 8) |
                (static_cast<std::uint32_t>(bytes.data[at + 2]) << 16) |
                (static_cast<std::uint32_t>(bytes.data[at + 3]) << 24);
            out << bits;
        }
        out << "]}";
    }
    out << "],\"fields\":[";
    for (std::size_t f = 0; f < fields.size(); ++f) {
        if (f) out << ',';
        out << "{\"index\":" << f << ",\"type\":" << static_cast<unsigned int>(fields[f].type)
            << ",\"payload_offset\":" << fields[f].payload_offset
            << ",\"payload_size\":" << fields[f].payload_size << '}';
    }
    out << "]}";
}

std::size_t select_group(const TableModel& table, const std::string& argument) {
    bool numeric = !argument.empty();
    for (std::size_t i = 0; i < argument.size(); ++i) {
        if (argument[i] < '0' || argument[i] > '9') { numeric = false; break; }
    }
    if (!numeric) return table.require_named_group(argument);
    std::size_t index = 0;
    for (std::size_t i = 0; i < argument.size(); ++i) {
        const std::size_t digit = static_cast<std::size_t>(argument[i] - '0');
        if (index > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            throw std::runtime_error("group index is too large");
        }
        index = index * 10 + digit;
    }
    table.group(index);
    return index;
}

void write_inventory(std::ostream& out, const TableModel& table, bool summary,
                     bool selected, std::size_t selection) {
    std::size_t image_count = 0;
    std::size_t depth_count = 0;
    std::size_t paired_count = 0;
    std::size_t opaque_count = 0;
    for (std::size_t g = 0; g < table.groups().size(); ++g) {
        const pinball::table::GroupInventory& group = table.group(g);
        image_count += group.images.size();
        depth_count += group.depth_field_indices.size();
        opaque_count += group.opaque_depth_field_indices.size();
        for (std::size_t i = 0; i < group.images.size(); ++i) {
            if (group.images[i].has_depth) ++paired_count;
        }
    }
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\n  \"format\":\"pinball-table-inventory-v1\",\n  \"interpretation\":{"
        << "\"coordinates\":\"literal DAT values; viewport transforms are unspecified\","
        << "\"object_types\":\"numeric IDs; behavior is unimplemented\","
        << "\"image_sequences\":\"observed marker-200 start and consecutive marker-201 groups\","
        << "\"depth_association\":\"same-group ordinal when counts and dimensions agree\","
        << "\"unclassified_fields\":\"retained by source offset and original DAT bytes\"},\n";
    out << "  \"table_size\":{\"width\":" << table.dimensions().width
        << ",\"height\":" << table.dimensions().height << ",\"group\":"
        << table.dimensions().group_index << ",\"field\":" << table.dimensions().field_index << "},\n";
    out << "  \"table_objects\":{\"group\":" << table.objects_group_index()
        << ",\"field\":" << table.objects_field_index() << ",\"prefix_unknown\":"
        << table.objects_prefix() << ",\"count\":" << table.objects().size() << "},\n";
    out << "  \"statistics\":{\"groups\":" << table.groups().size()
        << ",\"image_fields\":" << image_count << ",\"depth_fields\":" << depth_count
        << ",\"opaque_depth_fields\":" << opaque_count
        << ",\"paired_image_depth_states\":" << paired_count << "},\n";
    out << "  \"type_histogram\":{";
    bool comma = false;
    for (std::map<std::int16_t, std::size_t>::const_iterator it = table.type_histogram().begin();
         it != table.type_histogram().end(); ++it) {
        if (comma) out << ',';
        out << '"' << it->first << "\":" << it->second;
        comma = true;
    }
    out << "},\n";
    if (!summary) {
        out << "  \"objects\":[";
        comma = false;
        for (std::size_t i = 0; i < table.objects().size(); ++i) {
            const pinball::table::ObjectEntry& object = table.objects()[i];
            if (selected && object.group_index != selection) continue;
            if (comma) out << ',';
            out << "{\"index\":" << object.ordinal << ",\"type\":" << object.type
                << ",\"resource_id\":" << object.resource_id << ",\"group\":"
                << object.group_index << ",\"name\":";
            quoted(out, table.group(object.group_index).name);
            out << ",\"image_state_count\":" << table.image_states(object.group_index).size() << '}';
            comma = true;
        }
        out << "],\n  \"groups\":[";
        if (selected) inventory_group(out, table, selection);
        else for (std::size_t g = 0; g < table.groups().size(); ++g) {
            if (g) out << ',';
            inventory_group(out, table, g);
        }
        out << "],\n";
    }
    out << "  \"diagnostics\":[";
    for (std::size_t i = 0; i < table.diagnostics().size(); ++i) {
        if (i) out << ',';
        quoted(out, table.diagnostics()[i]);
    }
    out << "]\n}\n";
}

void usage() {
    std::cerr << "Usage: pinball_table_inspect <PINBALL.DAT> [--summary | --group <index-or-name>]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3 && argc != 4) { usage(); return 2; }
        const bool summary = argc == 3 && std::string(argv[2]) == "--summary";
        const bool selected = argc == 4 && std::string(argv[2]) == "--group";
        if ((argc == 3 && !summary) || (argc == 4 && !selected)) { usage(); return 2; }
        const TableModel table = TableModel::load(argv[1]);
        const std::size_t selection = selected ? select_group(table, argv[3]) : 0;
        write_inventory(std::cout, table, summary, selected, selection);
        if (!std::cout) throw std::runtime_error("inventory output failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Table inventory failed: " << error.what() << '\n';
        return 1;
    }
}
