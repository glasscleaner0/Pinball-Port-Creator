#include "scene.hpp"
#include "table_model.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace pinball {
namespace scene {
namespace {

std::uint16_t le16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                    (static_cast<std::uint16_t>(p[1]) << 8));
}

std::size_t area(std::uint32_t w, std::uint32_t h)
{
    if (w == 0 || h == 0)
        throw std::runtime_error("scene dimensions must be positive");
    if (w > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
        h > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
        throw std::runtime_error("scene dimensions exceed coordinate range");
    if (static_cast<std::size_t>(h) > (std::numeric_limits<std::size_t>::max)() / w / 4U)
        throw std::runtime_error("scene color storage overflows address space");
    return static_cast<std::size_t>(w) * h;
}

void validate_surface(const Surface& surface)
{
    const std::size_t count = area(surface.width, surface.height);
    if (surface.pixels.size() != count * 4U || surface.depth.size() != count)
        throw std::runtime_error("surface dimensions do not match owned color/depth storage");
}

void validate_sprite(const Sprite& sprite)
{
    const std::size_t count = area(sprite.bitmap.width, sprite.bitmap.height);
    if (sprite.bitmap.pixels.size() != count * 4U || sprite.indices.size() != count)
        throw std::runtime_error("sprite dimensions do not match RGBA/index storage");
    if (!sprite.depth.values.empty() &&
        (sprite.depth.width != sprite.bitmap.width ||
         sprite.depth.height != sprite.bitmap.height || sprite.depth.values.size() != count))
        throw std::runtime_error("sprite dimensions do not match depth map");
}

bool passes(std::uint16_t source, std::uint16_t destination, DepthTest test)
{
    switch (test) {
    case DepthTest::Always: return true;
    case DepthTest::Less: return source < destination;
    case DepthTest::LessEqual: return source <= destination;
    case DepthTest::Greater: return source > destination;
    case DepthTest::GreaterEqual: return source >= destination;
    }
    throw std::runtime_error("unknown depth comparison");
}

pinball360::Palette256 unique_palette(const data::DatFile& dat)
{
    const data::Field* selected = 0;
    for (std::size_t g = 0; g < dat.groups().size(); ++g) {
        const std::vector<data::Field>& fields = dat.groups()[g].fields;
        for (std::size_t f = 0; f < fields.size(); ++f) {
            if (fields[f].type == 5) {
                if (selected != 0)
                    throw std::runtime_error("resource scene needs one unambiguous palette");
                selected = &fields[f];
            }
        }
    }
    if (selected == 0)
        throw std::runtime_error("resource scene has no palette");
    const data::ByteView bytes = dat.payload(*selected);
    return pinball360::decode_palette(bytes.data, bytes.size);
}

bool physical_type(std::int16_t type)
{
    // General light resources remain excluded; selected mapped lamp
    // banks are handled separately. Ordinary walls retain hidden hit images.
    switch (type) {
    case 1000: case 1010:
    case 1001: case 1003: case 1004: case 1005: case 1006:
    case 1011: case 1013: case 1014: case 1015: case 1018:
    case 1019: case 1020: return true;
    default: return false;
    }
}

void append_selected_lamp(const table::TableModel& model,std::size_t group,
                          std::vector<std::size_t>& selected,std::size_t frames=1)
{
    bool light=false;
    for(std::size_t o=0;o<model.objects().size();++o)
        if(model.objects()[o].group_index==group && model.objects()[o].type==1002)light=true;
    if(!light || model.image_states(group).size()!=frames)
        throw std::runtime_error("selected lamp has unexpected registered image states");
    if(std::find(selected.begin(),selected.end(),group)==selected.end())selected.push_back(group);
}
std::vector<std::size_t> selected_lamp_groups(const table::TableModel& model)
{
    // Only explicitly mapped feature banks and indicators are cached. The
    // caller owns their logical state, flash timing and initial visibility.
    struct Bank {
        const char* name;std::size_t lamps,frames;
        Bank(const char* n,std::size_t count,std::size_t states=1):name(n),lamps(count),frames(states) {}
    };
    const Bank banks[]={{"top_target_lights",4},{"bmpr_inc_lights",3},
        {"ramp_bmpr_inc_lights",3},{"bumper_target_lights",3},
        {"top_circle_tgt_lights",3},{"ramp_tgt_lights",3},{"lchute_tgt_lights",3},
        {"bpr_solotgt_lights",3},{"fuel_bargraph",6},{"skill_shot_lights",6},
        {"goal_lights",23},{"middle_circle",9},{"outer_circle",18},{"hyperspace_lights",4},
        {"worm_hole_lights",3},{"bsink_arrow_lights",3,3}};
    std::vector<std::size_t> selected;
    for(std::size_t n=0;n<sizeof(banks)/sizeof(banks[0]);++n) {
        std::size_t bank=0;
        if(!model.find_named_group(banks[n].name,bank))continue;
        const std::vector<table::ShortRecord>& records=model.group(bank).short_records;
        const std::vector<std::int16_t>* members=0;
        for(std::size_t r=0;r<records.size();++r) {
            if(records[r].field_type!=10 || records[r].values.empty() || records[r].values[0]!=1027)continue;
            if(members)throw std::runtime_error("ambiguous selected lamp membership");
            members=&records[r].values;
        }
        if(!members || members->size()!=banks[n].lamps+1)
            throw std::runtime_error("selected lamp bank has unexpected membership");
        std::vector<std::size_t> bank_members;
        for(std::size_t i=1;i<members->size();++i) {
            if((*members)[i]<0)throw std::runtime_error("invalid selected lamp group");
            const std::size_t group=static_cast<std::size_t>((*members)[i]);
            if(std::find(bank_members.begin(),bank_members.end(),group)!=bank_members.end())
                throw std::runtime_error("duplicate selected lamp group");
            bank_members.push_back(group);
            append_selected_lamp(model,group,selected,banks[n].frames);
        }
    }
    const char* indicators[]={"lite110","lite25","lite56","lite198","lite199","lite200",
        "lite16","lite17","lite18","lite19","lite20","lite26","lite27","lite28",
        "lite54","lite58","lite59","lite60","lite61","lite62",
        "lite1","lite29","lite30","lite195","lite196","lite77"};
    for(std::size_t n=0;n<sizeof(indicators)/sizeof(indicators[0]);++n) {
        std::size_t group=0;
        if(model.find_named_group(indicators[n],group))append_selected_lamp(model,group,selected);
    }
    return selected;
}

void append16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 255U));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 255U));
}

void append32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 255U));
}

std::string json_string(const std::string& text)
{
    std::ostringstream out;
    out << '"';
    static const char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}

} // namespace

Surface::Surface() : width(0), height(0) {}
Surface::Surface(std::uint32_t w, std::uint32_t h) : width(w), height(h)
{
    const std::size_t count = area(w, h);
    if (count * 4U > pixels.max_size() || count > depth.max_size())
        throw std::runtime_error("scene storage exceeds container capacity");
    pixels.resize(count * 4U);
    depth.resize(count);
    const pinball360::Rgba8 black = {0, 0, 0, 255};
    clear(black);
}

void Surface::clear(pinball360::Rgba8 color, std::uint16_t depth_value)
{
    validate_surface(*this);
    for (std::size_t i = 0; i < depth.size(); ++i) {
        pixels[i * 4U] = color.r;
        pixels[i * 4U + 1U] = color.g;
        pixels[i * 4U + 2U] = color.b;
        pixels[i * 4U + 3U] = color.a;
    }
    std::fill(depth.begin(), depth.end(), depth_value);
}

Rect::Rect() : x(0), y(0), width((std::numeric_limits<int>::max)()),
               height((std::numeric_limits<int>::max)()) {}
Rect::Rect(int px, int py, int w, int h) : x(px), y(py), width(w), height(h) {}
DepthMap::DepthMap() : width(0), height(0) {}
Sprite::Sprite() : bitmap(), group_index(0), bitmap_field_index(0) {}
DrawOptions::DrawOptions() : transparent_zero(true), depth_test(DepthTest::LessEqual),
                             write_depth(true), constant_depth(0) {}
SceneLayout::SceneLayout() : width(600), height(416), table_x(0), table_y(0) {}
ResourceScene::ResourceScene() : table_stored_x(0), table_stored_y(0) {}

DepthMap decode_depth_map(const std::uint8_t* bytes, std::size_t size)
{
    if (bytes == 0 || size < 14U)
        throw std::runtime_error("truncated depth header");
    const std::uint16_t w = le16(bytes);
    const std::uint16_t h = le16(bytes + 2);
    const std::uint16_t stride = le16(bytes + 4);
    if (w == 0 || h == 0 || w > 32767U || h > 32767U || stride > 32767U)
        throw std::runtime_error("depth dimensions must be positive signed 16-bit values");
    if (stride < w)
        throw std::runtime_error("depth stride is smaller than width");
    const std::size_t count = area(w, h);
    if (static_cast<std::size_t>(h) > (std::numeric_limits<std::size_t>::max)() / stride / 2U)
        throw std::runtime_error("depth row storage overflows address space");
    const std::size_t expected = static_cast<std::size_t>(stride) * h * 2U;
    if (size - 14U != expected)
        throw std::runtime_error("depth payload is truncated or contains trailing bytes");
    DepthMap result;
    result.width = w;
    result.height = h;
    result.values.resize(count);
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t* source = bytes + 14U + (h - y - 1U) * stride * 2U;
        for (std::size_t x = 0; x < w; ++x)
            result.values[y * w + x] = le16(source + x * 2U);
    }
    return result;
}

Sprite decode_sprite(const data::DatFile& dat, std::size_t group_index,
                     std::size_t bitmap_field_index,
                     const pinball360::Palette256& palette)
{
    if (group_index >= dat.groups().size())
        throw std::runtime_error("sprite group index is out of range");
    const std::vector<data::Field>& fields = dat.groups()[group_index].fields;
    if (bitmap_field_index >= fields.size() || fields[bitmap_field_index].type != 1)
        throw std::runtime_error("sprite field is not a type-1 bitmap");
    const data::ByteView bytes = dat.payload(fields[bitmap_field_index]);
    Sprite result;
    result.bitmap = pinball360::decode_bitmap8(bytes.data, bytes.size, palette);
    result.group_index = group_index;
    result.bitmap_field_index = bitmap_field_index;
    const std::size_t w = result.bitmap.width;
    const std::size_t h = result.bitmap.height;
    const std::size_t stride = (w + 3U) & ~static_cast<std::size_t>(3U);
    result.indices.resize(w * h);
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t* source = bytes.data + 14U + (h - y - 1U) * stride;
        std::copy(source, source + w, result.indices.begin() + y * w);
    }
    std::vector<std::size_t> bitmaps;
    std::vector<std::size_t> depths;
    std::size_t ordinal = 0;
    for (std::size_t f = 0; f < fields.size(); ++f) {
        if (fields[f].type == 1) {
            if (f == bitmap_field_index) ordinal = bitmaps.size();
            bitmaps.push_back(f);
        }
        if (fields[f].type == 12) depths.push_back(f);
    }
    if (!depths.empty()) {
        if (depths.size() != bitmaps.size())
            throw std::runtime_error("ambiguous sprite/depth pairing");
        const data::ByteView zbytes = dat.payload(fields[depths[ordinal]]);
        result.depth = decode_depth_map(zbytes.data, zbytes.size);
    }
    validate_sprite(result);
    return result;
}

void draw_sprite(Surface& target, const Sprite& sprite, int x, int y,
                 const DrawOptions& options)
{
    validate_surface(target);
    validate_sprite(sprite);
    if (options.clip.width <= 0 || options.clip.height <= 0) return;
    // Widen additions before clipping: a hostile INT_MAX position or clip
    // must not wrap, address the wrong row, or touch an out-of-range sample.
    const std::int64_t left = (std::max)(std::int64_t(0),
        (std::max)(std::int64_t(x), std::int64_t(options.clip.x)));
    const std::int64_t top = (std::max)(std::int64_t(0),
        (std::max)(std::int64_t(y), std::int64_t(options.clip.y)));
    const std::int64_t right = (std::min)(std::int64_t(target.width),
        (std::min)(std::int64_t(x) + sprite.bitmap.width,
                   std::int64_t(options.clip.x) + options.clip.width));
    const std::int64_t bottom = (std::min)(std::int64_t(target.height),
        (std::min)(std::int64_t(y) + sprite.bitmap.height,
                   std::int64_t(options.clip.y) + options.clip.height));
    if (right <= left || bottom <= top) return;
    for (std::int64_t row = top; row < bottom; ++row) {
        std::size_t source = static_cast<std::size_t>(row - y) * sprite.bitmap.width +
                             static_cast<std::size_t>(left - x);
        std::size_t destination = static_cast<std::size_t>(row) * target.width +
                                  static_cast<std::size_t>(left);
        for (std::int64_t column = left; column < right; ++column, ++source, ++destination) {
            if ((options.transparent_zero && sprite.indices[source] == 0) ||
                sprite.bitmap.pixels[source * 4U + 3U] == 0) continue;
            const std::uint16_t z = sprite.depth.values.empty() ?
                options.constant_depth : sprite.depth.values[source];
            if (!passes(z, target.depth[destination], options.depth_test)) continue;
            std::copy(sprite.bitmap.pixels.begin() + source * 4U,
                      sprite.bitmap.pixels.begin() + source * 4U + 4U,
                      target.pixels.begin() + destination * 4U);
            if (options.write_depth) target.depth[destination] = z;
        }
    }
}

void ResourceScene::render()
{
    validate_surface(background);
    surface = background;
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const SpriteInstance& instance = instances[i];
        if (!instance.visible) continue;
        if (instance.sprite_index >= sprites.size())
            throw std::runtime_error("scene instance refers to missing sprite");
        draw_sprite(surface, sprites[instance.sprite_index], instance.x, instance.y, instance.draw);
    }
}

void ResourceScene::set_position(std::size_t instance, int x, int y)
{
    if (instance >= instances.size()) throw std::out_of_range("scene instance index");
    instances[instance].x = x;
    instances[instance].y = y;
}
void ResourceScene::set_visible(std::size_t instance, bool visible)
{
    if (instance >= instances.size()) throw std::out_of_range("scene instance index");
    instances[instance].visible = visible;
}
void ResourceScene::set_state(std::size_t instance, std::size_t sprite_index,
                             std::size_t state_index)
{
    if (instance >= instances.size() || sprite_index >= sprites.size())
        throw std::out_of_range("scene state index");
    SpriteInstance& selected = instances[instance];
    const Sprite& previous = sprites[selected.sprite_index];
    const Sprite& next = sprites[sprite_index];
    const std::int64_t next_x = std::int64_t(selected.x) + next.bitmap.x - previous.bitmap.x;
    const std::int64_t next_y = std::int64_t(selected.y) + next.bitmap.y - previous.bitmap.y;
    if (next_x < (std::numeric_limits<int>::min)() || next_x > (std::numeric_limits<int>::max)() ||
        next_y < (std::numeric_limits<int>::min)() || next_y > (std::numeric_limits<int>::max)())
        throw std::runtime_error("state change position overflows coordinate range");
    selected.x = static_cast<int>(next_x);
    selected.y = static_cast<int>(next_y);
    selected.stored_x = next.bitmap.x;
    selected.stored_y = next.bitmap.y;
    selected.sprite_index = sprite_index;
    selected.state_index = state_index;
}

ResourceScene static_resource_scene(const table::TableModel& model, const SceneLayout& layout)
{
    const data::DatFile& dat = model.dat();
    const pinball360::Palette256 palette = unique_palette(dat);
    const std::size_t table_group = model.require_named_group("table");
    const std::vector<table::ImageState>& table_images = model.image_states(table_group);
    if (table_images.size() != 1)
        throw std::runtime_error("static scene needs one table bitmap");
    const Sprite table_sprite = decode_sprite(dat, table_group,
        table_images[0].bitmap_field_index, palette);
    ResourceScene result;
    result.layout = layout;
    result.table_stored_x = table_sprite.bitmap.x;
    result.table_stored_y = table_sprite.bitmap.y;
    result.background = Surface(layout.width, layout.height);
    DrawOptions table_draw;
    table_draw.transparent_zero = false;
    table_draw.depth_test = DepthTest::Always;
    draw_sprite(result.background, table_sprite, layout.table_x, layout.table_y, table_draw);
    std::size_t sidebar_group = 0;
    if (model.find_named_group("background", sidebar_group)) {
        const std::vector<table::ImageState>& sidebar_images = model.image_states(sidebar_group);
        if (sidebar_images.size() != 1)
            throw std::runtime_error("static scene needs one sidebar bitmap");
        const Sprite sidebar = decode_sprite(dat, sidebar_group,
            sidebar_images[0].bitmap_field_index, palette);
        DrawOptions sidebar_draw;
        sidebar_draw.transparent_zero = false;
        sidebar_draw.depth_test = DepthTest::Always;
        sidebar_draw.write_depth = false;
        draw_sprite(result.background, sidebar, sidebar.bitmap.x, sidebar.bitmap.y, sidebar_draw);
    }
    const std::vector<std::size_t> lamp_groups=selected_lamp_groups(model);
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> decoded;
    for (std::size_t o = 0; o < model.objects().size(); ++o) {
        const table::ObjectEntry& object = model.objects()[o];
        const bool bank_lamp=object.type==1002 &&
            std::find(lamp_groups.begin(),lamp_groups.end(),object.group_index)!=lamp_groups.end();
        if (!physical_type(object.type) && !bank_lamp) continue;
        const std::vector<table::ImageState>& states = model.image_states(object.group_index);
        if (states.empty()) continue;
        SpriteInstance instance;
        instance.object_ordinal = object.ordinal;
        instance.object_type = object.type;
        instance.resource_group = object.group_index;
        instance.state_index = 0;
        instance.visible = object.type != 1000 && object.type != 1010 && !bank_lamp;
        for (std::size_t s = 0; s < states.size(); ++s) {
            const std::pair<std::size_t, std::size_t> key(states[s].group_index,
                                                       states[s].bitmap_field_index);
            std::map<std::pair<std::size_t, std::size_t>, std::size_t>::const_iterator found =
                decoded.find(key);
            std::size_t index;
            if (found == decoded.end()) {
                index = result.sprites.size();
                result.sprites.push_back(decode_sprite(dat, key.first, key.second, palette));
                decoded.insert(std::make_pair(key, index));
            } else index = found->second;
            instance.state_sprite_indices.push_back(index);
        }
        instance.sprite_index = instance.state_sprite_indices[0];
        instance.stored_x = states[0].bitmap.x;
        instance.stored_y = states[0].bitmap.y;
        instance.x = layout.table_x + instance.stored_x - result.table_stored_x;
        instance.y = layout.table_y + instance.stored_y - result.table_stored_y;
        result.instances.push_back(instance);
    }
    result.limitations.push_back("Static resource scene: physical objects use base images; ordinary-wall hit images and the mapped profile lamp images start hidden. Runtime state and timing belong to the caller.");
    result.limitations.push_back("Physical bitmap positions subtract table header origin (137,2 in the supplied asset); table-local placement is an explicit asset-alignment inference pending runtime reference.");
    result.limitations.push_back("Sidebar uses literal stored coordinates. Output canvas is configurable; DAT table_size is 600x416 while table bitmap is 365x470.");
    result.limitations.push_back("Index zero transparency and lower-depth visibility are supported by matching sprite/depth masks and sampled table overlaps; runtime comparison remains pending.");
    result.limitations.push_back("Only mapped profile lamp images are cached; other stateful lights, ball placement, score text, rules, physics and audio are not initialized by this static compositor.");
    result.render();
    return result;
}

void write_bmp(const std::string& path, const Surface& surface)
{
    validate_surface(surface);
    const std::uint64_t row_size = (static_cast<std::uint64_t>(surface.width) * 3U + 3U) & ~std::uint64_t(3U);
    const std::uint64_t data_size = row_size * surface.height;
    if (data_size + 54U > (std::numeric_limits<std::uint32_t>::max)())
        throw std::runtime_error("BMP output exceeds 32-bit file size limit");
    std::vector<std::uint8_t> header;
    header.push_back('B'); header.push_back('M');
    append32(header, static_cast<std::uint32_t>(data_size + 54U));
    append16(header, 0); append16(header, 0); append32(header, 54); append32(header, 40);
    append32(header, surface.width); append32(header, surface.height);
    append16(header, 1); append16(header, 24); append32(header, 0);
    append32(header, static_cast<std::uint32_t>(data_size));
    append32(header, 2835); append32(header, 2835); append32(header, 0); append32(header, 0);
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open scene BMP output: " + path);
    output.write(reinterpret_cast<const char*>(header.data()), header.size());
    std::vector<std::uint8_t> row(static_cast<std::size_t>(row_size), 0);
    for (std::size_t y = 0; y < surface.height; ++y) {
        const std::uint8_t* source = surface.pixels.data() +
            (surface.height - y - 1U) * surface.width * 4U;
        for (std::size_t x = 0; x < surface.width; ++x) {
            row[x * 3U] = source[x * 4U + 2U];
            row[x * 3U + 1U] = source[x * 4U + 1U];
            row[x * 3U + 2U] = source[x * 4U];
        }
        output.write(reinterpret_cast<const char*>(row.data()), row.size());
    }
    output.flush();
    if (!output) throw std::runtime_error("failed to write scene BMP output: " + path);
}

void write_scene_trace(const std::string& path, const ResourceScene& scene)
{
    std::ofstream out(path.c_str(), std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open scene trace output: " + path);
    out << "{\n  \"kind\": \"static_resource_scene\",\n  \"canvas\": {\"width\": "
        << scene.surface.width << ", \"height\": " << scene.surface.height << "},\n"
        << "  \"table_stored_origin\": [" << scene.table_stored_x << ", " << scene.table_stored_y << "],\n"
        << "  \"table_destination\": [" << scene.layout.table_x << ", " << scene.layout.table_y << "],\n"
        << "  \"instances\": [\n";
    for (std::size_t i = 0; i < scene.instances.size(); ++i) {
        const SpriteInstance& instance = scene.instances[i];
        const Sprite& sprite = scene.sprites[instance.sprite_index];
        if (i) out << ",\n";
        out << "    {\"object_ordinal\": " << instance.object_ordinal
            << ", \"type\": " << instance.object_type
            << ", \"resource_group\": " << instance.resource_group
            << ", \"image_group\": " << sprite.group_index
            << ", \"bitmap_field\": " << sprite.bitmap_field_index
            << ", \"state\": " << instance.state_index
            << ", \"available_states\": " << instance.state_sprite_indices.size()
            << ", \"stored\": [" << instance.stored_x << ", " << instance.stored_y
            << "], \"translation\": [" << scene.layout.table_x - scene.table_stored_x
            << ", " << scene.layout.table_y - scene.table_stored_y
            << "], \"destination\": [" << instance.x << ", " << instance.y
            << "], \"size\": [" << sprite.bitmap.width << ", " << sprite.bitmap.height
            << "], \"depth\": " << (sprite.depth.values.empty() ? "false" : "true")
            << ", \"visible\": " << (instance.visible ? "true" : "false") << '}';
    }
    out << "\n  ],\n  \"limitations\": [\n";
    for (std::size_t i = 0; i < scene.limitations.size(); ++i) {
        if (i) out << ",\n";
        out << "    " << json_string(scene.limitations[i]);
    }
    out << "\n  ]\n}\n";
    out.flush();
    if (!out) throw std::runtime_error("failed to write scene trace output: " + path);
}

} // namespace scene
} // namespace pinball
