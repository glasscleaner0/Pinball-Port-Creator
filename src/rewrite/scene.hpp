#ifndef PINBALL_REWRITE_SCENE_HPP
#define PINBALL_REWRITE_SCENE_HPP

#include "dat_reader.hpp"
#include "pinball360/bitmap.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pinball {
namespace table { class TableModel; }
namespace scene {

// All color storage is tightly packed, top-down R,G,B,A bytes. Depth is an
// owned top-down uint16_t array; decoding never reads native-endian words.
struct Surface {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint16_t> depth;
    Surface();
    Surface(std::uint32_t width, std::uint32_t height);
    void clear(pinball360::Rgba8 color, std::uint16_t depth_value = 65535U);
};

struct Rect {
    int x;
    int y;
    int width;
    int height;
    Rect();
    Rect(int x, int y, int width, int height);
};

struct DepthMap {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint16_t> values;
    DepthMap();
};

struct Sprite {
    pinball360::Bitmap8 bitmap;
    // Preserve palette indices separately from opaque RGBA decoder output.
    // This avoids treating every black RGB pixel as a transparent pixel.
    std::vector<std::uint8_t> indices;
    DepthMap depth;
    std::size_t group_index;
    std::size_t bitmap_field_index;
    Sprite();
};

enum class DepthTest { Always, Less, LessEqual, Greater, GreaterEqual };

struct DrawOptions {
    Rect clip;
    bool transparent_zero;
    DepthTest depth_test;
    bool write_depth;
    std::uint16_t constant_depth;
    DrawOptions();
};

// Windows type-12 14-byte header, bottom-up rows, stride in uint16_t units.
// Dimensions/stride/length are checked before any sample is read.
DepthMap decode_depth_map(const std::uint8_t* bytes, std::size_t size);
Sprite decode_sprite(const data::DatFile& dat, std::size_t group_index,
                     std::size_t bitmap_field_index,
                     const pinball360::Palette256& palette);

// Fully clipped before sampling; transparent samples update neither RGBA nor
// depth. Sprites without depth use constant_depth. Equal-depth behavior is
// explicit, so callers can use a different rule once runtime evidence exists.
void draw_sprite(Surface& target, const Sprite& sprite, int x, int y,
                 const DrawOptions& options = DrawOptions());

struct SceneLayout {
    std::uint32_t width;
    std::uint32_t height;
    int table_x;
    int table_y;
    SceneLayout();
};

struct SpriteInstance {
    std::size_t object_ordinal;
    std::int16_t object_type;
    std::size_t resource_group;
    std::size_t state_index;
    std::size_t sprite_index;
    std::vector<std::size_t> state_sprite_indices;
    int stored_x;
    int stored_y;
    int x;
    int y;
    // Ordinary-wall hit images and the selected profile lamps begin
    // hidden. Visibility preserves the frame, position and owned depth map.
    bool visible;
    DrawOptions draw;
};

struct ResourceScene {
    // Immutable by convention: table and sidebar with their depth. Each
    // render begins from this base, avoiding stale moving-sprite depth.
    Surface background;
    Surface surface;
    std::vector<Sprite> sprites;
    std::vector<SpriteInstance> instances;
    std::vector<std::string> limitations;
    SceneLayout layout;
    int table_stored_x;
    int table_stored_y;
    ResourceScene();
    void render();
    void set_position(std::size_t instance, int x, int y);
    void set_visible(std::size_t instance, bool visible);
    void set_state(std::size_t instance, std::size_t sprite_index,
                   std::size_t state_index);
};

// A static resource scene, not a claim about verified runtime initial state.
// Includes physical base resources, hidden wall hit images and only mapped
// explicitly mapped profile lamp images (hidden initially). Other lights are
// omitted. The caller owns target/lamp visibility and reaction timing via
// set_state/set_visible. Bitmap coordinates use the stored table origin; this is
// an explicit asset-alignment inference pending reference runtime comparison.
ResourceScene static_resource_scene(const table::TableModel& model,
                                    const SceneLayout& layout = SceneLayout());
void write_bmp(const std::string& path, const Surface& surface);
void write_scene_trace(const std::string& path, const ResourceScene& scene);

} // namespace scene
} // namespace pinball
#endif
