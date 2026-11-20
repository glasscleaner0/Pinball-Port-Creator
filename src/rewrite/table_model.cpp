#include "table_model.hpp"

#include <sstream>
#include <utility>

namespace pinball {
namespace table {
namespace {

std::string context(std::size_t group, const std::string& text) {
    std::ostringstream out;
    out << "group " << group << ": " << text;
    return out.str();
}

std::size_t sole_short_array(const data::DatFile& dat, std::size_t group) {
    const std::vector<data::Field>& fields = dat.groups().at(group).fields;
    bool found = false;
    std::size_t result = 0;
    for (std::size_t f = 0; f < fields.size(); ++f) {
        if (fields[f].type == 10) {
            if (found) throw ModelError(context(group, "multiple table arrays"));
            found = true;
            result = f;
        }
    }
    if (!found) throw ModelError(context(group, "required table array missing"));
    return result;
}

void validate_bitmap(const data::Field& field, const data::BitmapHeader& bitmap,
                     std::size_t group) {
    if (bitmap.width <= 0 || bitmap.height <= 0) {
        throw ModelError(context(group, "nonpositive image dimensions"));
    }
    if (bitmap.data_size != field.payload_size - 14) {
        throw ModelError(context(group, "bitmap byte count disagrees with payload"));
    }
}

void validate_depth(const data::Field& field, const data::DepthHeader& depth,
                    std::size_t group) {
    if (depth.width <= 0 || depth.height <= 0 || depth.stride < depth.width) {
        throw ModelError(context(group, "invalid depth dimensions or stride"));
    }
    const std::size_t stride_bytes = static_cast<std::size_t>(depth.stride) * 2;
    const std::size_t height = static_cast<std::size_t>(depth.height);
    if (height > (field.payload_size - 14) / stride_bytes ||
        height * stride_bytes != field.payload_size - 14) {
        throw ModelError(context(group, "depth extent disagrees with payload"));
    }
}

} // namespace

ModelError::ModelError(const std::string& message) : std::runtime_error(message) {}

TableModel::TableModel(data::DatFile dat)
    : dat_(std::move(dat)), dimensions_(), objects_group_index_(0),
      objects_field_index_(0), objects_prefix_(0) {}

TableModel TableModel::from_dat(data::DatFile dat) {
    TableModel result(std::move(dat));
    const data::DatFile& input = result.dat_;
    result.groups_.reserve(input.groups().size());
    for (std::size_t g = 0; g < input.groups().size(); ++g) {
        GroupInventory inventory = GroupInventory();
        inventory.group_index = g;
        inventory.name = input.group_name(g);
        const std::vector<data::Field>& fields = input.groups()[g].fields;
        std::size_t marker_count = 0;
        std::map<std::string, bool> group_names;
        for (std::size_t f = 0; f < fields.size(); ++f) {
            const data::Field& field = fields[f];
            if (field.type == 3) {
                const std::string name = input.string_value(field);
                if (!name.empty() && group_names.insert(std::make_pair(name, true)).second) {
                    result.named_groups_[name].push_back(g);
                }
            }
            if (field.type == 0 || field.type == 2 || field.type == 10) {
                ShortRecord record;
                record.field_index = f;
                record.field_type = field.type;
                record.values.reserve(field.payload_size / 2);
                for (std::size_t i = 0; i < field.payload_size / 2; ++i) {
                    record.values.push_back(input.short_value(field, i));
                }
                if (field.type == 0) {
                    ++marker_count;
                    inventory.record_marker = record.values[0];
                }
                inventory.short_records.push_back(std::move(record));
            } else if (field.type == 11) {
                FloatRecord record;
                record.field_index = f;
                record.values.reserve(field.payload_size / 4);
                for (std::size_t i = 0; i < field.payload_size / 4; ++i) {
                    record.values.push_back(input.float_value(field, i));
                }
                inventory.float_records.push_back(std::move(record));
            } else if (field.type == 1) {
                ImageState image = ImageState();
                image.state_index = inventory.images.size();
                image.group_index = g;
                image.bitmap_index = inventory.images.size();
                image.bitmap_field_index = f;
                image.bitmap = input.bitmap_header(field);
                validate_bitmap(field, image.bitmap, g);
                inventory.images.push_back(image);
            } else if (field.type == 12) {
                const data::DepthHeader depth = input.depth_header(field);
                bool all_zero = true;
                const data::ByteView payload = input.payload(field);
                for (std::size_t b = 0; b < payload.size; ++b) {
                    if (payload.data[b] != 0) { all_zero = false; break; }
                }
                if (all_zero) {
                    inventory.opaque_depth_field_indices.push_back(f);
                    result.diagnostics_.push_back(context(g,
                        "all-zero type-12 payload retained as opaque metadata"));
                } else {
                    validate_depth(field, depth, g);
                }
                inventory.depth_field_indices.push_back(f);
            }
        }
        inventory.has_record_marker = marker_count == 1;
        if (marker_count > 1) {
            result.diagnostics_.push_back(context(g,
                "multiple type-0 fields; no record marker assigned"));
        }
        if (inventory.has_record_marker && inventory.record_marker == 200) {
            for (std::size_t r = 0; r < inventory.short_records.size(); ++r) {
                const ShortRecord& record = inventory.short_records[r];
                if (record.field_type == 10 && record.values.size() >= 2 &&
                    record.values[0] == 100) {
                    if (inventory.has_declared_state_count) {
                        throw ModelError(context(g, "ambiguous leading [100,n] records"));
                    }
                    inventory.has_declared_state_count = true;
                    inventory.declared_state_count = record.values[1];
                    if (record.values[1] <= 0) {
                        throw ModelError(context(g, "nonpositive declared state count"));
                    }
                }
            }
        }
        if (!inventory.images.empty() && !inventory.depth_field_indices.empty()) {
            if (inventory.images.size() != inventory.depth_field_indices.size()) {
                result.diagnostics_.push_back(context(g,
                    "image/depth counts differ; depth association unresolved"));
            } else {
                for (std::size_t i = 0; i < inventory.images.size(); ++i) {
                    ImageState& image = inventory.images[i];
                    const std::size_t depth_field = inventory.depth_field_indices[i];
                    const data::DepthHeader depth = input.depth_header(fields[depth_field]);
                    if (image.bitmap.width == depth.width && image.bitmap.height == depth.height) {
                        image.has_depth = true;
                        image.depth_field_index = depth_field;
                        image.depth = depth;
                    } else {
                        result.diagnostics_.push_back(context(g,
                            "image/depth dimensions differ; depth association unresolved"));
                    }
                }
            }
        }
        result.groups_.push_back(std::move(inventory));
    }

    const std::size_t size_group = result.require_named_group("table_size");
    const std::size_t size_field = sole_short_array(input, size_group);
    const data::Field& size = input.groups()[size_group].fields[size_field];
    if (size.payload_size != 4) {
        throw ModelError(context(size_group, "table_size must contain two shorts"));
    }
    result.dimensions_.width = input.short_value(size, 0);
    result.dimensions_.height = input.short_value(size, 1);
    result.dimensions_.group_index = size_group;
    result.dimensions_.field_index = size_field;
    if (result.dimensions_.width <= 0 || result.dimensions_.height <= 0) {
        throw ModelError(context(size_group, "table_size dimensions must be positive"));
    }

    result.objects_group_index_ = result.require_named_group("table_objects");
    result.objects_field_index_ = sole_short_array(input, result.objects_group_index_);
    const data::Field& object_field = input.groups()[result.objects_group_index_]
        .fields[result.objects_field_index_];
    const std::size_t object_values = object_field.payload_size / 2;
    if (object_values == 0 || object_values % 2 == 0) {
        throw ModelError(context(result.objects_group_index_,
            "table_objects must contain a prefix followed by complete pairs"));
    }
    result.objects_prefix_ = input.short_value(object_field, 0);
    result.objects_.reserve((object_values - 1) / 2);
    for (std::size_t i = 1; i < object_values; i += 2) {
        ObjectEntry object;
        object.ordinal = result.objects_.size();
        object.type = input.short_value(object_field, i);
        object.resource_id = input.short_value(object_field, i + 1);
        if (object.resource_id < 0 ||
            static_cast<std::size_t>(object.resource_id) >= result.groups_.size()) {
            throw ModelError(context(result.objects_group_index_,
                "table object references an invalid group"));
        }
        object.group_index = static_cast<std::size_t>(object.resource_id);
        result.objects_.push_back(object);
        ++result.type_histogram_[object.type];
    }

    result.image_sequences_.resize(result.groups_.size());
    for (std::size_t g = 0; g < result.groups_.size(); ++g) {
        GroupInventory& inventory = result.groups_[g];
        inventory.state_group_indices.push_back(g);
        if (inventory.has_record_marker && inventory.record_marker == 200) {
            for (std::size_t next = g + 1; next < result.groups_.size(); ++next) {
                const GroupInventory& following = result.groups_[next];
                if (!following.has_record_marker || following.record_marker != 201) break;
                inventory.state_group_indices.push_back(next);
            }
            if (inventory.has_declared_state_count &&
                static_cast<std::size_t>(inventory.declared_state_count) !=
                    inventory.state_group_indices.size()) {
                throw ModelError(context(g,
                    "leading [100,n] count disagrees with marker-200/201 group run"));
            }
            if (!inventory.has_declared_state_count && inventory.state_group_indices.size() > 1) {
                result.diagnostics_.push_back(context(g,
                    "marker-201 continuation has no leading [100,n] count"));
            }
        }
        std::vector<ImageState>& states = result.image_sequences_[g];
        for (std::size_t s = 0; s < inventory.state_group_indices.size(); ++s) {
            const GroupInventory& state_group = result.groups_[inventory.state_group_indices[s]];
            for (std::size_t i = 0; i < state_group.images.size(); ++i) {
                ImageState image = state_group.images[i];
                image.state_index = states.size();
                states.push_back(image);
            }
        }
    }
    return result;
}

TableModel TableModel::load(const std::string& path) {
    return from_dat(data::DatFile::load(path));
}

const data::DatFile& TableModel::dat() const { return dat_; }
const Dimensions& TableModel::dimensions() const { return dimensions_; }
std::size_t TableModel::objects_group_index() const { return objects_group_index_; }
std::size_t TableModel::objects_field_index() const { return objects_field_index_; }
std::int16_t TableModel::objects_prefix() const { return objects_prefix_; }
const std::vector<ObjectEntry>& TableModel::objects() const { return objects_; }
const std::map<std::int16_t, std::size_t>& TableModel::type_histogram() const {
    return type_histogram_;
}
const std::vector<GroupInventory>& TableModel::groups() const { return groups_; }
const GroupInventory& TableModel::group(std::size_t index) const { return groups_.at(index); }

std::vector<std::size_t> TableModel::find_named_groups(const std::string& name) const {
    const std::map<std::string, std::vector<std::size_t> >::const_iterator found =
        named_groups_.find(name);
    return found == named_groups_.end() ? std::vector<std::size_t>() : found->second;
}

bool TableModel::find_named_group(const std::string& name, std::size_t& index) const {
    const std::map<std::string, std::vector<std::size_t> >::const_iterator found =
        named_groups_.find(name);
    if (found == named_groups_.end()) return false;
    if (found->second.size() != 1) {
        throw ModelError("ambiguous DAT group name: " + name);
    }
    index = found->second[0];
    return true;
}

std::size_t TableModel::require_named_group(const std::string& name) const {
    std::size_t index = 0;
    if (!find_named_group(name, index)) throw ModelError("required DAT group missing: " + name);
    return index;
}

const std::vector<ImageState>& TableModel::image_states(std::size_t resource_group) const {
    return image_sequences_.at(resource_group);
}

const std::vector<std::string>& TableModel::diagnostics() const { return diagnostics_; }

} // namespace table
} // namespace pinball
