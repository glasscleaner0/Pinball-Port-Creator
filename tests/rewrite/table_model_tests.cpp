#include "table_model.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using pinball::data::DatFile;
using pinball::table::TableModel;
typedef std::vector<std::uint8_t> Bytes;

struct Entry {
    std::uint8_t type;
    Bytes bytes;
    Entry(std::uint8_t type_value, Bytes payload) : type(type_value), bytes(std::move(payload)) {}
};
typedef std::vector<Entry> FixtureGroup;
typedef std::vector<FixtureGroup> FixtureGroups;

void check(bool condition, const std::string& label) {
    if (!condition) throw std::runtime_error(label);
}

void put16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

Bytes shorts(std::initializer_list<std::int16_t> values) {
    Bytes result(values.size() * 2);
    std::size_t i = 0;
    for (std::initializer_list<std::int16_t>::const_iterator v = values.begin();
         v != values.end(); ++v, ++i) put16(result, i * 2, static_cast<std::uint16_t>(*v));
    return result;
}

Bytes floats(std::initializer_list<float> values) {
    Bytes result(values.size() * 4);
    std::size_t i = 0;
    for (std::initializer_list<float>::const_iterator v = values.begin();
         v != values.end(); ++v, ++i) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &*v, 4);
        put32(result, i * 4, bits);
    }
    return result;
}

Entry name(const std::string& value) {
    Bytes result(value.begin(), value.end());
    result.push_back(0);
    return Entry(3, result);
}

Entry bitmap(std::int16_t width, std::int16_t height, std::int16_t x,
             std::int16_t y) {
    const std::uint32_t pixels = static_cast<std::uint32_t>(width * height);
    Bytes result(14 + pixels, 0x5a);
    result[0] = 0;
    put16(result, 1, width); put16(result, 3, height);
    put16(result, 5, x); put16(result, 7, y);
    put32(result, 9, pixels); result[13] = 0x81; // Preserve unknown flag bits.
    return Entry(1, result);
}

Entry depth(std::int16_t width, std::int16_t height, std::int16_t stride) {
    Bytes result(14 + stride * height * 2, 0);
    put16(result, 0, width); put16(result, 2, height); put16(result, 4, stride);
    put32(result, 6, 0x12345678u); put16(result, 10, 0xfffe); put16(result, 12, 80);
    return Entry(12, result);
}

FixtureGroups base_groups() {
    FixtureGroups groups;
    groups.push_back(FixtureGroup{name("table_size"), Entry(10, shorts({600, 416}))});
    groups.push_back(FixtureGroup{name("table_objects"),
        Entry(10, shorts({1025, 7777, 2, 1003, 2}))});
    groups.push_back(FixtureGroup{Entry(0, shorts({200})), name("sprite"),
        Entry(10, shorts({100, 2, 999, -5})), bitmap(2, 2, -7, 11),
        bitmap(1, 1, 19, -3), depth(2, 2, 4), depth(1, 1, 1)});
    groups.push_back(FixtureGroup{Entry(0, shorts({201})), bitmap(3, 2, 5, 6),
        Entry(11, floats({600.0f, 123.5f, -4.0f})), depth(3, 2, 4)});
    groups.push_back(FixtureGroup{name("opaque"), Entry(4, Bytes{0, 1, 0xfe}),
        Entry(13, Bytes())});
    return groups;
}

Bytes encode(const FixtureGroups& groups) {
    Bytes result(183, 0);
    const char signature[] = "PARTOUT(4.0)RESOURCE";
    for (std::size_t i = 0; i < sizeof(signature); ++i) result[i] = signature[i];
    put16(result, 175, static_cast<std::uint16_t>(groups.size()));
    for (std::size_t g = 0; g < groups.size(); ++g) {
        result.push_back(static_cast<std::uint8_t>(groups[g].size()));
        for (std::size_t f = 0; f < groups[g].size(); ++f) {
            const Entry& entry = groups[g][f];
            result.push_back(entry.type);
            if (entry.type != 0 && entry.type != 2 && entry.type != 13) {
                const std::size_t at = result.size();
                result.resize(at + 4);
                put32(result, at, static_cast<std::uint32_t>(entry.bytes.size()));
            }
            result.insert(result.end(), entry.bytes.begin(), entry.bytes.end());
        }
    }
    put32(result, 171, static_cast<std::uint32_t>(result.size()));
    put32(result, 177, static_cast<std::uint32_t>(result.size() - 183));
    return result;
}

TableModel model(const FixtureGroups& groups) {
    return TableModel::from_dat(DatFile::parse(encode(groups)));
}

void expect_error(const std::function<void()>& action, const std::string& label) {
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("invalid inventory accepted: " + label);
}

void test_inventory_and_ownership() {
    const Bytes bytes = encode(base_groups());
    const TableModel table = TableModel::from_dat(DatFile::parse(bytes));
    check(table.dimensions().width == 600 && table.dimensions().height == 416 &&
        table.dimensions().group_index == 0 && table.dimensions().field_index == 1,
        "dimensions retain their source field");
    check(table.objects_prefix() == 1025 && table.objects_group_index() == 1 &&
        table.objects_field_index() == 1, "unknown object prefix retained");
    check(table.objects().size() == 2 && table.objects()[0].type == 7777 &&
        table.objects()[0].resource_id == 2 && table.objects()[0].group_index == 2 &&
        table.objects()[1].ordinal == 1, "unknown object type and repeated resource retained");
    check(table.type_histogram().at(7777) == 1 && table.type_histogram().at(1003) == 1,
        "type histogram preserves numeric type IDs");
    std::size_t group = 99;
    check(table.find_named_group("sprite", group) && group == 2, "named group lookup");
    check(!table.find_named_group("Sprite", group) && group == 2,
        "name lookup is exact and absent lookup preserves output");
    check(table.require_named_group("table_objects") == 1 &&
        table.find_named_groups("missing").empty(), "required and plural name lookup");
    check(table.group(2).short_records[1].values[3] == -5,
        "unclassified short metadata retained");
    check(table.group(3).float_records[0].values[1] == 123.5f,
        "unclassified float metadata retained");
    const std::vector<pinball::table::ImageState>& states = table.image_states(2);
    check(table.group(2).images.size() == 2 && states.size() == 3,
        "state enumeration uses type-1 ordinals across declared group run");
    check(table.group(2).state_group_indices == std::vector<std::size_t>({2, 3}) &&
        table.group(2).has_declared_state_count && table.group(2).declared_state_count == 2,
        "declared state groups retained without treating count as image-field count");
    check(states[0].bitmap.x == -7 && states[0].bitmap.y == 11 &&
        states[0].bitmap.flags == 0x81 && states[1].bitmap.x == 19 &&
        states[1].bitmap.y == -3, "literal image placement and unknown flags retained");
    check(states[0].group_index == 2 && states[0].bitmap_field_index == 3 &&
        states[1].bitmap_index == 1 && states[2].state_index == 2 &&
        states[2].group_index == 3, "image descriptors preserve group and field identity");
    check(states[0].has_depth && states[0].depth_field_index == 5 &&
        states[1].has_depth && states[1].depth_field_index == 6 &&
        states[0].depth.unknown_0 == 0x12345678u && states[0].depth.unknown_1 == -2,
        "same-group depth association and unknown header values retained");
    check(table.image_states(3).size() == 1 && table.image_states(4).empty(),
        "continuation and nonimage resources can be inspected independently");
    check(table.diagnostics().empty(), "unambiguous synthetic inventory");
    check(table.dat().bytes() == bytes && table.dat().payload(table.dat().groups()[4]
        .fields[1]).data[2] == 0xfe, "all original unknown bytes retained");
    TableModel copied = table;
    TableModel moved = std::move(copied);
    check(moved.dat().bytes() == bytes && moved.image_states(2)[2].has_depth,
        "model ownership survives copy and move");
    expect_error([&table]() { table.group(5); }, "group bounds");
    expect_error([&table]() { table.image_states(5); }, "state resource bounds");
    expect_error([&table]() { table.require_named_group("absent"); }, "required name");
}

void test_malformed_and_ambiguous_metadata() {
    FixtureGroups groups = base_groups();
    groups[0][1] = Entry(10, shorts({0, 416}));
    expect_error([&groups]() { model(groups); }, "nonpositive dimensions");
    groups = base_groups(); groups[0][1] = Entry(10, shorts({600, 416, 7}));
    expect_error([&groups]() { model(groups); }, "dimension array arity");
    groups = base_groups(); groups[0].push_back(Entry(10, shorts({1, 2})));
    expect_error([&groups]() { model(groups); }, "ambiguous dimension arrays");
    groups = base_groups(); groups[0][0] = name("not_table_size");
    expect_error([&groups]() { model(groups); }, "missing dimensions");
    groups = base_groups(); groups[4][0] = name("table_size");
    expect_error([&groups]() { model(groups); }, "duplicate required name");
    groups = base_groups(); groups[1][1] = Entry(10, shorts({1025, 1000}));
    expect_error([&groups]() { model(groups); }, "incomplete object pair");
    groups = base_groups(); groups[1][1] = Entry(10, Bytes());
    expect_error([&groups]() { model(groups); }, "missing object prefix");
    groups = base_groups(); groups[1][1] = Entry(10, shorts({1025, 1000, -1}));
    expect_error([&groups]() { model(groups); }, "negative resource ID");
    groups = base_groups(); groups[1][1] = Entry(10, shorts({1025, 1000, 5}));
    expect_error([&groups]() { model(groups); }, "resource ID at group-count boundary");
    groups = base_groups(); groups[2][2] = Entry(10, shorts({100, 3}));
    expect_error([&groups]() { model(groups); }, "declared run exceeds continuation groups");
    groups = base_groups(); groups[2][2] = Entry(10, shorts({100, 0}));
    expect_error([&groups]() { model(groups); }, "zero declared run");
    groups = base_groups(); groups[2].push_back(Entry(10, shorts({100, 2})));
    expect_error([&groups]() { model(groups); }, "ambiguous declared runs");
    groups = base_groups(); put16(groups[2][3].bytes, 1, 0);
    expect_error([&groups]() { model(groups); }, "invalid image dimensions");
    groups = base_groups(); put32(groups[2][3].bytes, 9, 0xffffffffu);
    expect_error([&groups]() { model(groups); }, "invalid image byte count");
    groups = base_groups(); groups[2][3] = Entry(1, Bytes(13));
    expect_error([&groups]() { model(groups); }, "truncated image header");
    groups = base_groups(); put16(groups[2][5].bytes, 4, 1);
    expect_error([&groups]() { model(groups); }, "depth stride smaller than width");
    groups = base_groups(); groups[2][5].bytes.pop_back();
    expect_error([&groups]() { model(groups); }, "truncated depth pixels");

    groups = base_groups(); groups[4][0] = name("sprite");
    const TableModel duplicates = model(groups);
    check(duplicates.find_named_groups("sprite").size() == 2, "duplicate optional names visible");
    expect_error([&duplicates]() { duplicates.require_named_group("sprite"); },
        "single-name lookup rejects ambiguity");

    groups = base_groups(); groups[2].pop_back();
    const TableModel counts = model(groups);
    check(!counts.group(2).images[0].has_depth && !counts.group(2).images[1].has_depth &&
        counts.group(2).depth_field_indices.size() == 1 && !counts.diagnostics().empty(),
        "ambiguous depth counts retained without guessing associations");
    groups = base_groups(); groups[2][5] = depth(1, 2, 4);
    const TableModel extents = model(groups);
    check(!extents.group(2).images[0].has_depth && extents.group(2).images[1].has_depth &&
        !extents.diagnostics().empty(), "dimension mismatch is explicit and other pair remains usable");
    groups = base_groups(); groups[2][2] = Entry(10, shorts({999, -5}));
    const TableModel uncounted = model(groups);
    check(uncounted.image_states(2).size() == 3 && !uncounted.diagnostics().empty(),
        "uncounted continuation convention diagnosed");
    groups = base_groups(); groups[4].push_back(Entry(12, Bytes(19, 0)));
    const TableModel opaque_depth = model(groups);
    check(opaque_depth.group(4).opaque_depth_field_indices.size() == 1 &&
        opaque_depth.group(4).images.empty() && !opaque_depth.diagnostics().empty(),
        "all-zero type-12 payload remains explicit opaque metadata");
    groups = base_groups(); groups[1][1] = Entry(10, shorts({-123}));
    check(model(groups).objects().empty(), "empty object table still retains unknown prefix");
}

void test_real(const std::string& path, bool known_asset) {
    const TableModel table = TableModel::load(path);
    for (std::size_t i = 0; i < table.objects().size(); ++i) {
        const pinball::table::ObjectEntry& object = table.objects()[i];
        check(object.ordinal == i && object.group_index < table.groups().size(),
            "every real object resource is validated");
    }
    std::size_t image_count = 0;
    for (std::size_t g = 0; g < table.groups().size(); ++g) {
        for (std::size_t i = 0; i < table.group(g).images.size(); ++i) {
            const pinball::table::ImageState& state = table.group(g).images[i];
            check(state.group_index == g && state.bitmap_index == i &&
                table.dat().groups()[g].fields[state.bitmap_field_index].type == 1,
                "every real image preserves field identity");
            if (state.has_depth) check(table.dat().groups()[g]
                .fields[state.depth_field_index].type == 12, "depth field identity");
            ++image_count;
        }
    }
    if (known_asset) {
        check(table.groups().size() == 541 && table.objects().size() == 340,
            "reference group and object counts");
        check(table.dimensions().width == 600 && table.dimensions().height == 416 &&
            table.objects_prefix() == 1025, "reference dimensions and unknown prefix");
        const std::map<std::int16_t, std::size_t> expected = {
            {1000,45},{1001,1},{1002,140},{1003,1},{1004,1},{1005,7},{1006,9},
            {1007,1},{1010,19},{1011,1},{1012,2},{1013,2},{1014,2},{1015,17},
            {1016,9},{1017,4},{1018,2},{1019,13},{1020,1},{1021,2},{1022,1},
            {1023,1},{1024,5},{1026,18},{1028,2},{1029,1},{1030,1},{1031,30},{1033,2}};
        check(table.type_histogram() == expected, "complete reference object type histogram");
        check(image_count == 318 && table.diagnostics().size() == 2, "reference image inventory");
        const std::size_t left = table.require_named_group("a_flip1");
        const std::size_t right = table.require_named_group("a_flip2");
        check(left == 355 && right == 363 && table.image_states(left).size() == 8 &&
            table.image_states(right).size() == 8, "reference flipper state runs");
        check(table.image_states(left)[0].bitmap.x == 261 &&
            table.image_states(left)[0].bitmap.y == 378 &&
            table.image_states(left)[7].group_index == 362,
            "reference literal image placements and continuation IDs");
        check(table.image_states(table.require_named_group("ball")).size() == 7 &&
            !table.image_states(table.require_named_group("ball"))[0].has_depth,
            "ball image sequence has no same-group depth");
        check(table.group(497).images.empty() && table.group(497).depth_field_indices.size() == 1 &&
            table.group(497).opaque_depth_field_indices.size() == 1 &&
            table.group(498).opaque_depth_field_indices.size() == 1,
            "standalone original depth resource retained");
    }
    std::cout << "Table inventory: " << table.objects().size() << " objects, "
        << image_count << " images, " << table.type_histogram().size() << " numeric types\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            test_inventory_and_ownership();
            test_malformed_and_ambiguous_metadata();
            std::cout << "Table model synthetic tests passed\n";
            return 0;
        }
        if (argc == 3 && (std::string(argv[1]) == "--real" || std::string(argv[1]) == "--file")) {
            test_real(argv[2], std::string(argv[1]) == "--real");
            return 0;
        }
        std::cerr << "Usage: pinball_table_tests [--real <PINBALL.DAT> | --file <DAT>]\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Table model test failed: " << error.what() << '\n';
        return 1;
    }
}
