#include "ball_visual.hpp"

#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace pinball {
namespace visual {
namespace {

void validate_ball_sprite(const scene::Sprite& sprite)
{
    const std::size_t width = sprite.bitmap.width;
    const std::size_t height = sprite.bitmap.height;
    if (!width || !height) throw VisualError("ball sprite dimensions must be positive");
    const std::size_t limit = (std::numeric_limits<std::size_t>::max)();
    if (height > limit / width / 4U) throw VisualError("ball RGBA extent overflows size_t");
    const std::size_t samples = width * height;
    const std::size_t rgba_bytes = samples * 4U;
    if (rgba_bytes > sprite.bitmap.pixels.max_size())
        throw VisualError("ball RGBA extent exceeds vector capacity");
    if (sprite.indices.size() != samples || sprite.bitmap.pixels.size() != rgba_bytes)
        throw VisualError("ball sprite dimensions disagree with owned color/index storage");
    if (!sprite.depth.values.empty())
        throw VisualError("ball cache requires depth-free sprites; caller selects constant depth");
}

bool subtract_anchor(int position, int offset, int& result)
{
    if (offset > 0 && position < (std::numeric_limits<int>::min)() + offset) return false;
    if (offset < 0 && position > (std::numeric_limits<int>::max)() + offset) return false;
    result = position - offset;
    return true;
}

void validate_direction(PullDirection direction)
{
    if (direction != PullDirection::FirstToLast && direction != PullDirection::LastToFirst)
        throw VisualError("invalid plunger pull direction");
}

void validate_selection(FrameSelection selection)
{
    if (selection != FrameSelection::Nearest && selection != FrameSelection::Floor)
        throw VisualError("invalid plunger frame selection");
}

} // namespace

VisualError::VisualError(const std::string& message) : std::runtime_error(message) {}

std::vector<BitmapResource> ordered_image_resources(const table::TableModel& model,
                                                   std::size_t resource_group)
{
    const std::vector<table::ImageState>& states = model.image_states(resource_group);
    std::vector<BitmapResource> result;
    result.reserve(states.size());
    for (std::size_t i = 0; i < states.size(); ++i) {
        const BitmapResource resource = {states[i].group_index, states[i].bitmap_field_index};
        result.push_back(resource);
    }
    return result;
}

BallSpriteCache::BallSpriteCache() {}
BallSpriteCache BallSpriteCache::from_resources(const data::DatFile& dat,
    const std::vector<BitmapResource>& resources, const pinball360::Palette256& palette)
{
    if (resources.empty()) throw VisualError("ball cache requires at least one explicit resource");
    std::vector<scene::Sprite> sprites;
    sprites.reserve(resources.size());
    for (std::size_t i = 0; i < resources.size(); ++i) {
        scene::Sprite sprite = scene::decode_sprite(dat, resources[i].group_index,
                                                   resources[i].bitmap_field_index, palette);
        validate_ball_sprite(sprite);
        sprites.push_back(std::move(sprite));
    }
    return from_sprites(std::move(sprites));
}

BallSpriteCache BallSpriteCache::from_groups(const table::TableModel& model,
    const std::vector<std::size_t>& groups, const pinball360::Palette256& palette)
{
    if (groups.empty()) throw VisualError("ball cache requires at least one explicit group");
    std::vector<BitmapResource> resources;
    resources.reserve(groups.size());
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const table::GroupInventory& group = model.group(groups[i]);
        if (group.images.size() != 1)
            throw VisualError("explicit ball group must contain exactly one local bitmap");
        const BitmapResource resource = {groups[i], group.images[0].bitmap_field_index};
        resources.push_back(resource);
    }
    return from_resources(model.dat(), resources, palette);
}

BallSpriteCache BallSpriteCache::from_sprites(std::vector<scene::Sprite> sprites)
{
    if (sprites.empty()) throw VisualError("ball cache requires at least one owned sprite");
    for (std::size_t i = 0; i < sprites.size(); ++i) validate_ball_sprite(sprites[i]);
    BallSpriteCache result;
    result.sprites_ = std::move(sprites);
    return result;
}
std::size_t BallSpriteCache::size() const { return sprites_.size(); }
const scene::Sprite& BallSpriteCache::sprite(std::size_t variant) const
{
    if (variant >= sprites_.size()) throw VisualError("ball variant index is out of range");
    return sprites_[variant];
}
BallSpriteInfo BallSpriteCache::info(std::size_t variant) const
{
    const scene::Sprite& selected = sprite(variant);
    BallSpriteInfo result;
    result.resource.group_index = selected.group_index;
    result.resource.bitmap_field_index = selected.bitmap_field_index;
    result.width = selected.bitmap.width;
    result.height = selected.bitmap.height;
    result.stored_x = selected.bitmap.x;
    result.stored_y = selected.bitmap.y;
    return result;
}

AnchorOffset::AnchorOffset(int px, int py) : x(px), y(py) {}
BallDrawOptions::BallDrawOptions(std::uint32_t z, bool shown)
    : depth(z), visible(shown), depth_test(scene::DepthTest::LessEqual), write_depth(true) {}

void draw_ball(scene::Surface& target, const BallSpriteCache& cache, std::size_t variant,
               int anchor_x, int anchor_y, AnchorOffset anchor, const BallDrawOptions& options)
{
    const scene::Sprite& sprite = cache.sprite(variant);
    if (options.depth > (std::numeric_limits<std::uint16_t>::max)())
        throw VisualError("ball depth is outside uint16 range");
    if (!options.visible) return;
    // An anchor subtraction outside int range is wholly off-screen for an
    // int-sized target and uint16 sprite. Check before subtracting, and compare
    // against negative dimensions instead of adding widths to extreme origins.
    if (target.width > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
        target.height > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
        throw VisualError("ball destination exceeds compositor coordinate range");
    int left = 0, top = 0;
    if (!subtract_anchor(anchor_x, anchor.x, left) || !subtract_anchor(anchor_y, anchor.y, top)) return;
    if (left >= static_cast<int>(target.width) || top >= static_cast<int>(target.height) ||
        left <= -static_cast<int>(sprite.bitmap.width) || top <= -static_cast<int>(sprite.bitmap.height)) return;
    scene::DrawOptions draw;
    draw.clip = options.clip;
    draw.transparent_zero = true;
    draw.depth_test = options.depth_test;
    draw.write_depth = options.write_depth;
    draw.constant_depth = static_cast<std::uint16_t>(options.depth);
    scene::draw_sprite(target, sprite, left, top, draw);
}

std::size_t plunger_pose_index(std::size_t state_count, double normalized_pull,
                              PullDirection direction, FrameSelection selection)
{
    if (!state_count) throw VisualError("plunger pose list must not be empty");
    validate_direction(direction);
    validate_selection(selection);
    if (!std::isfinite(normalized_pull) || normalized_pull < 0 || normalized_pull > 1)
        throw VisualError("normalized plunger pull must be finite in [0,1]");
    const std::size_t last = state_count - 1U;
    std::size_t progress = 0;
    if (normalized_pull == 1) progress = last;
    else if (normalized_pull != 0 && last != 0) {
        const double endpoint = static_cast<double>(last);
        double scaled = endpoint * normalized_pull;
        if (selection == FrameSelection::Nearest) scaled += 0.5;
        // Bound before floating-to-integer conversion, including size_t64
        // endpoints that round upward when represented as double.
        if (scaled >= endpoint) progress = last;
        else progress = static_cast<std::size_t>(scaled);
    }
    return direction == PullDirection::FirstToLast ? progress : last - progress;
}
std::size_t plunger_pose(const std::vector<std::size_t>& ordered_states, double normalized_pull,
                        PullDirection direction, FrameSelection selection)
{
    return ordered_states[plunger_pose_index(ordered_states.size(), normalized_pull, direction, selection)];
}

} // namespace visual
} // namespace pinball
