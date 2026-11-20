#ifndef PINBALL_REWRITE_TABLE_MODEL_HPP
#define PINBALL_REWRITE_TABLE_MODEL_HPP

#include "dat_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace pinball {
namespace table {

class ModelError : public std::runtime_error {
public:
    explicit ModelError(const std::string& message);
};

struct Dimensions {
    std::int16_t width;
    std::int16_t height;
    std::size_t group_index;
    std::size_t field_index;
};

struct ObjectEntry {
    std::size_t ordinal;
    std::int16_t type;
    std::int16_t resource_id;
    std::size_t group_index; // A validated, nonnegative resource_id.
};

struct ShortRecord {
    std::size_t field_index;
    std::uint8_t field_type; // 0, 2 or 10; no semantic keys are assumed.
    std::vector<std::int16_t> values;
};

struct FloatRecord {
    std::size_t field_index;
    std::vector<float> values; // Raw IEEE bytes also remain in dat().
};

struct ImageState {
    std::size_t state_index; // Ordinal in the returned image-state vector.
    std::size_t group_index;
    std::size_t bitmap_index; // Ordinal among this group's type-1 fields.
    std::size_t bitmap_field_index;
    data::BitmapHeader bitmap; // Literal stored coordinates; no viewport transform.
    bool has_depth;
    std::size_t depth_field_index; // Valid only when has_depth is true.
    data::DepthHeader depth;
};

struct GroupInventory {
    std::size_t group_index;
    std::string name;
    bool has_record_marker;
    std::int16_t record_marker; // Sole type-0 short, when present.
    std::vector<ShortRecord> short_records;
    std::vector<FloatRecord> float_records;
    std::vector<ImageState> images; // This group only, indexed by type-1 ordinal.
    std::vector<std::size_t> depth_field_indices; // Includes unpaired type-12s.
    std::vector<std::size_t> opaque_depth_field_indices; // All-zero non-raster records.
    bool has_declared_state_count;
    std::int16_t declared_state_count; // Leading [100,n] pair in a type-10 field.
    std::vector<std::size_t> state_group_indices;
};

// Immutable, owned inventory of a Windows table DAT. All unclassified fields,
// original bytes and unknown numeric metadata are retained by dat(). Object type
// IDs are numeric facts, not physics or rules implementations. Marker 200 plus
// consecutive marker-201 groups is an observed image-sequence convention; it
// does not identify animation timing or the correct initial game state.
class TableModel {
public:
    static TableModel from_dat(data::DatFile dat);
    static TableModel load(const std::string& path);

    const data::DatFile& dat() const;
    const Dimensions& dimensions() const;
    std::size_t objects_group_index() const;
    std::size_t objects_field_index() const;
    std::int16_t objects_prefix() const; // Unknown leading table_objects value.
    const std::vector<ObjectEntry>& objects() const;
    const std::map<std::int16_t, std::size_t>& type_histogram() const;
    const std::vector<GroupInventory>& groups() const;
    const GroupInventory& group(std::size_t index) const;

    // Exact, case-sensitive DAT names. Duplicate names remain visible; resolving
    // a single group rejects ambiguity. An absent optional lookup returns false.
    std::vector<std::size_t> find_named_groups(const std::string& name) const;
    bool find_named_group(const std::string& name, std::size_t& index) const;
    std::size_t require_named_group(const std::string& name) const;

    // Base marker-200 group plus following marker-201 groups, in DAT order.
    // Other group kinds return their own images. Depth is paired by same-group
    // ordinal only when counts and dimensions agree; ambiguities are diagnosed.
    const std::vector<ImageState>& image_states(std::size_t resource_group) const;
    const std::vector<std::string>& diagnostics() const;

private:
    explicit TableModel(data::DatFile dat);
    data::DatFile dat_;
    Dimensions dimensions_;
    std::size_t objects_group_index_;
    std::size_t objects_field_index_;
    std::int16_t objects_prefix_;
    std::vector<ObjectEntry> objects_;
    std::map<std::int16_t, std::size_t> type_histogram_;
    std::vector<GroupInventory> groups_;
    std::map<std::string, std::vector<std::size_t> > named_groups_;
    std::vector<std::vector<ImageState> > image_sequences_;
    std::vector<std::string> diagnostics_;
};

} // namespace table
} // namespace pinball

#endif
