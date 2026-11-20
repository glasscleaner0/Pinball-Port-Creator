#ifndef PINBALL_REWRITE_BALL_VISUAL_HPP
#define PINBALL_REWRITE_BALL_VISUAL_HPP

#include "scene.hpp"
#include "table_model.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace pinball {
namespace visual {

class VisualError : public std::runtime_error {
public:
    explicit VisualError(const std::string& message);
};

struct BitmapResource {
    std::size_t group_index;
    std::size_t bitmap_field_index;
};

// Literal model sequence order, without guessing a screen-depth/size mapping.
std::vector<BitmapResource> ordered_image_resources(const table::TableModel& model,
                                                   std::size_t resource_group);

struct BallSpriteInfo {
    BitmapResource resource;
    std::uint16_t width;
    std::uint16_t height;
    std::int16_t stored_x;
    std::int16_t stored_y;
};

class BallSpriteCache {
public:
    // Explicit selections, in caller order. No group-name lookup or sorting.
    static BallSpriteCache from_resources(const data::DatFile& dat,
        const std::vector<BitmapResource>& resources, const pinball360::Palette256& palette);
    // Each explicit group must contain exactly one local image. This does NOT
    // expand a sequence start; use ordered_image_resources for that purpose.
    static BallSpriteCache from_groups(const table::TableModel& model,
        const std::vector<std::size_t>& groups, const pinball360::Palette256& palette);
    // Also accepts already-decoded owned resources, retaining their order.
    static BallSpriteCache from_sprites(std::vector<scene::Sprite> sprites);
    std::size_t size() const;
    const scene::Sprite& sprite(std::size_t variant) const;
    BallSpriteInfo info(std::size_t variant) const;
private:
    BallSpriteCache();
    std::vector<scene::Sprite> sprites_;
};

struct AnchorOffset {
    int x;
    int y;
    AnchorOffset(int x = 0, int y = 0);
};

struct BallDrawOptions {
    // Required caller choices. Depth is wide enough to reject values above
    // UINT16_MAX before narrowing; no physical z/perspective conversion occurs.
    std::uint32_t depth;
    bool visible;
    scene::Rect clip;
    scene::DepthTest depth_test;
    bool write_depth;
    BallDrawOptions(std::uint32_t depth, bool visible);
};

// anchor_x/y are integer surface-pixel coordinates. The image's top-left is
// (anchor_x-anchor.x, anchor_y-anchor.y), computed without signed overflow.
// An anchor offset of(0,0) means top-left. Callers choose any center convention
// from info().width/height; stored DAT x/y are metadata and are never added.
// Index0 is always keyed. Only depth-free sprites are accepted into this cache;
// the selected constant depth is applied to every visible sample. Even hidden
// draws validate variant/depth. The original ball assets provide no depth map.
void draw_ball(scene::Surface& target, const BallSpriteCache& cache, std::size_t variant,
               int anchor_x, int anchor_y, AnchorOffset anchor, const BallDrawOptions& options);

enum class PullDirection { FirstToLast, LastToFirst };
enum class FrameSelection { Nearest, Floor };

// New, explicit display policy: uniformly-spaced frames, not original timing.
// Pull must be finite in[0,1].0 selects the released endpoint specified by
// direction;1 selects its pulled endpoint. Nearest half ties advance toward
// the pulled endpoint. Reverse the ordinal only AFTER selecting pull progress.
std::size_t plunger_pose_index(std::size_t state_count, double normalized_pull,
                              PullDirection direction, FrameSelection selection = FrameSelection::Nearest);
// State IDs are opaque caller values; preserve the supplied order, including
// nonnumeric ordering or duplicate IDs. Empty lists reject.
std::size_t plunger_pose(const std::vector<std::size_t>& ordered_states, double normalized_pull,
                        PullDirection direction, FrameSelection selection = FrameSelection::Nearest);

// Local DAT observations, freshly read through TableModel: named ball resource
//108 has seven ordered image groups108..114, dimensions9x9..15x15, stored(0,0),
// no depth maps. Plunger473 has eight frames473..480 with different crops and
// stored origins. Resource ordering is known; perspective, resting pose, pull
// duration and force are not established by those numeric data alone.

} // namespace visual
} // namespace pinball
#endif
