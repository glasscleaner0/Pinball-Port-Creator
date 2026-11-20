#include "ball_visual.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>

namespace {
using namespace pinball::visual;

void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation, const std::string& fragment)
{
    try { operation(); }
    catch (const VisualError& error) {
        require(std::string(error.what()).find(fragment) != std::string::npos,
            "wrong ball visual failure: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("expected VisualError containing " + fragment);
}

pinball::scene::Sprite generated_sprite(unsigned width, unsigned height, unsigned red)
{
    pinball::scene::Sprite sprite;
    sprite.bitmap.width = static_cast<std::uint16_t>(width);
    sprite.bitmap.height = static_cast<std::uint16_t>(height);
    sprite.bitmap.x = 123;
    sprite.bitmap.y = -45;
    sprite.group_index = 41;
    sprite.bitmap_field_index = 3;
    sprite.indices.assign(width * height, 1);
    if (width == 3 && height == 3) {
        const std::uint8_t shape[9] = {0,1,0, 2,1,2, 0,2,0};
        sprite.indices.assign(shape,shape + 9);
    }
    sprite.bitmap.pixels.resize(width * height * 4U);
    for (std::size_t i = 0; i < sprite.indices.size(); ++i) {
        sprite.bitmap.pixels[i * 4U] = static_cast<std::uint8_t>(red * sprite.indices[i]);
        sprite.bitmap.pixels[i * 4U + 1U] = static_cast<std::uint8_t>(i);
        sprite.bitmap.pixels[i * 4U + 2U] = 7;
        sprite.bitmap.pixels[i * 4U + 3U] = 255;
    }
    return sprite;
}

BallSpriteCache generated_cache()
{
    std::vector<pinball::scene::Sprite> sprites;
    sprites.push_back(generated_sprite(3,3,100));
    sprites.push_back(generated_sprite(2,2,150));
    return BallSpriteCache::from_sprites(std::move(sprites));
}

void cache_ownership_and_invalid_geometry()
{
    pinball::scene::Sprite sprite = generated_sprite(3,3,100);
    const BallSpriteCache cache = BallSpriteCache::from_sprites({sprite});
    sprite.bitmap.pixels.assign(sprite.bitmap.pixels.size(),0);
    require(cache.size() == 1 && cache.sprite(0).bitmap.pixels[4] == 100,
        "ball cache must own decoded pixels independently of caller storage");
    const BallSpriteInfo info = cache.info(0);
    require(info.resource.group_index == 41 && info.resource.bitmap_field_index == 3 &&
        info.width == 3 && info.height == 3 && info.stored_x == 123 && info.stored_y == -45,
        "ball cache must preserve source identity and literal metrics");
    rejects([&] { cache.info(1); }, "variant");
    rejects([] { BallSpriteCache::from_sprites({}); }, "at least one");
    sprite = generated_sprite(3,3,100); sprite.indices.pop_back();
    rejects([&] { BallSpriteCache::from_sprites({sprite}); }, "storage");
    sprite = generated_sprite(3,3,100); sprite.depth.values.assign(9,10);
    rejects([&] { BallSpriteCache::from_sprites({sprite}); }, "depth-free");
    sprite = pinball::scene::Sprite(); sprite.bitmap.width = 65535; sprite.bitmap.height = 65535;
    const std::uint64_t rgba = 65535ULL * 65535ULL * 4U;
    if (rgba > (std::numeric_limits<std::size_t>::max)())
        rejects([&] { BallSpriteCache::from_sprites({sprite}); }, "RGBA extent overflows");
    else if (rgba > sprite.bitmap.pixels.max_size())
        rejects([&] { BallSpriteCache::from_sprites({sprite}); }, "vector capacity");
    else rejects([&] { BallSpriteCache::from_sprites({sprite}); }, "storage");
}

void anchoring_mask_and_clipping()
{
    const BallSpriteCache cache = generated_cache();
    pinball::scene::Surface target(4,4);
    const pinball360::Rgba8 background = {9,8,7,255};
    target.clear(background,500);
    BallDrawOptions draw(80,true);
    draw.clip = pinball::scene::Rect(1,0,1,3);
    draw_ball(target,cache,0,1,1,AnchorOffset(1,1),draw);
    require(target.pixels[1U * 4U] == 100 && target.pixels[(1U * 4U + 1U) * 4U] == 100 &&
        target.pixels[(2U * 4U + 1U) * 4U] == 200,
        "anchor/crop coordinates must use requested top-left, ignoring stored DAT coordinates");
    require(target.pixels[(1U * 4U) * 4U] == 9 && target.depth[4] == 500,
        "clip left edge must preserve color and depth");
    require(target.depth[1] == 80 && target.depth[5] == 80 && target.depth[9] == 80,
        "caller depth must apply to every drawn indexed sample");
    target.clear(background,500);
    draw.clip = pinball::scene::Rect();
    draw_ball(target,cache,0,0,0,AnchorOffset(),draw);
    require(target.pixels[0] == 9 && target.depth[0] == 500,
        "index-zero corners must remain transparent even if decoded RGBA is opaque");
    target.clear(background,500);
    draw_ball(target,cache,0,-1,-1,AnchorOffset(),draw);
    require(target.pixels[0] == 100 && target.pixels[1] == 4 && target.depth[0] == 80,
        "negative clipped origin must select the matching source row and column");
    const std::vector<std::uint8_t> before = target.pixels;
    const std::vector<std::uint16_t> z_before = target.depth;
    draw.clip = pinball::scene::Rect((std::numeric_limits<int>::max)(),0,(std::numeric_limits<int>::max)(),4);
    draw_ball(target,cache,0,0,0,AnchorOffset(),draw);
    require(target.pixels == before && target.depth == z_before,
        "extreme clip addition must not wrap into the destination");
}

void explicit_depth_visibility_and_overlap()
{
    const BallSpriteCache cache = generated_cache();
    pinball::scene::Surface target(4,4);
    const pinball360::Rgba8 background = {9,8,7,255};
    target.clear(background,50);
    BallDrawOptions far(80,true);
    draw_ball(target,cache,0,0,0,AnchorOffset(),far);
    require(target.pixels[5U * 4U] == 9 && target.depth[5] == 50,
        "ball behind caller depth must remain occluded");
    BallDrawOptions front(20,true);
    draw_ball(target,cache,0,0,0,AnchorOffset(),front);
    BallDrawOptions behind(30,true);
    draw_ball(target,cache,1,1,1,AnchorOffset(),behind);
    require(target.pixels[5U * 4U] == 100 && target.depth[5] == 20,
        "near overlap must survive a later farther sprite");
    require(target.pixels[10U * 4U] == 150 && target.depth[10] == 30,
        "farther sprite must still draw where the near sprite was keyed transparent");
    behind.depth = 20;
    behind.depth_test = pinball::scene::DepthTest::Less;
    draw_ball(target,cache,1,1,1,AnchorOffset(),behind);
    require(target.pixels[5U * 4U] == 100,"strict depth test must reject ties");
    behind.depth_test = pinball::scene::DepthTest::LessEqual;
    draw_ball(target,cache,1,1,1,AnchorOffset(),behind);
    require(target.pixels[5U * 4U] == 150,"explicit LessEqual must accept ties");
    const std::vector<std::uint8_t> before = target.pixels;
    const std::vector<std::uint16_t> z_before = target.depth;
    BallDrawOptions hidden(0,false);
    draw_ball(target,cache,0,0,0,AnchorOffset(),hidden);
    require(target.pixels == before && target.depth == z_before,"hidden ball must preserve both buffers");
    hidden.depth = 65536;
    rejects([&] { draw_ball(target,cache,0,0,0,AnchorOffset(),hidden); }, "uint16 range");
    far.depth = (std::numeric_limits<std::uint32_t>::max)();
    rejects([&] { draw_ball(target,cache,0,0,0,AnchorOffset(),far); }, "uint16 range");
    rejects([&] { draw_ball(target,cache,9,0,0,AnchorOffset(),front); }, "variant");
    target.clear(background,65535);
    BallDrawOptions maximum(65535,true);
    draw_ball(target,cache,1,0,0,AnchorOffset(),maximum);
    require(target.depth[0] == 65535 && target.pixels[0] == 150,
        "maximum valid uint16 depth must not wrap or be rejected");
    maximum.depth = 0; maximum.write_depth = false;
    draw_ball(target,cache,0,0,0,AnchorOffset(),maximum);
    require(target.pixels[5U * 4U] == 100 && target.depth[5] == 65535,
        "caller may draw color while explicitly retaining destination depth");
}

void anchor_subtraction_extremes()
{
    const BallSpriteCache cache = generated_cache();
    pinball::scene::Surface target(4,4);
    const BallDrawOptions draw(0,true);
    const std::vector<std::uint8_t> original = target.pixels;
    const int low = (std::numeric_limits<int>::min)(), high = (std::numeric_limits<int>::max)();
    draw_ball(target,cache,0,low,0,AnchorOffset(high,0),draw);
    draw_ball(target,cache,0,high,0,AnchorOffset(low,0),draw);
    draw_ball(target,cache,0,0,low,AnchorOffset(0,high),draw);
    draw_ball(target,cache,0,0,high,AnchorOffset(0,low),draw);
    require(target.pixels == original,"out-of-range anchor differences must clip without signed overflow");
    draw_ball(target,cache,0,high,low,AnchorOffset(high,low),draw);
    require(target.pixels[4] == 100 && target.depth[1] == 0,
        "extreme coordinates that cancel must still draw at the correct top-left");
}

void normalized_pull_has_explicit_order()
{
    using D = PullDirection;
    using S = FrameSelection;
    require(plunger_pose_index(8,0,D::FirstToLast) == 0 && plunger_pose_index(8,1,D::FirstToLast) == 7,
        "forward pull endpoints mismatch");
    require(plunger_pose_index(8,0,D::LastToFirst) == 7 && plunger_pose_index(8,1,D::LastToFirst) == 0,
        "reversed pull endpoints mismatch");
    require(plunger_pose_index(8,0.5,D::FirstToLast,S::Nearest) == 4 &&
        plunger_pose_index(8,0.5,D::LastToFirst,S::Nearest) == 3,
        "nearest half tie must progress toward the explicitly pulled endpoint");
    require(plunger_pose_index(8,0.5,D::FirstToLast,S::Floor) == 3,
        "floor quantization must be explicit and independent of nearest");
    const std::vector<std::size_t> ids = {8,19,77,2};
    require(plunger_pose(ids,0.5,D::FirstToLast) == 77 && plunger_pose(ids,0.5,D::LastToFirst) == 19,
        "caller resource order must not be numerically sorted");
    require(plunger_pose_index(1,0.8,D::LastToFirst) == 0,"singleton pose must be stable");
    const std::size_t enormous = (std::numeric_limits<std::size_t>::max)();
    require(plunger_pose_index(enormous,0,D::FirstToLast) == 0 &&
        plunger_pose_index(enormous,1,D::FirstToLast) == enormous - 1U,
        "extreme state-count endpoints must avoid floating-to-integer overflow");
    require(plunger_pose_index(enormous,std::nextafter(1.0,0.0),D::FirstToLast) < enormous,
        "near-endpoint quantization must remain inside its state list");
    rejects([] { plunger_pose_index(0,0,D::FirstToLast); }, "empty");
    rejects([] { plunger_pose_index(8,-0.01,D::FirstToLast); }, "finite");
    rejects([] { plunger_pose_index(8,1.01,D::FirstToLast); }, "finite");
    rejects([] { plunger_pose_index(8,(std::numeric_limits<double>::quiet_NaN)(),D::FirstToLast); }, "finite");
    rejects([] { plunger_pose_index(8,(std::numeric_limits<double>::infinity)(),D::FirstToLast); }, "finite");
    rejects([] { plunger_pose_index(8,0.5,static_cast<D>(99)); }, "direction");
    rejects([] { plunger_pose_index(8,0.5,D::FirstToLast,static_cast<S>(99)); }, "selection");
}

pinball360::Palette256 palette(const pinball::table::TableModel& model)
{
    pinball360::Palette256 result = {};
    std::size_t count = 0;
    for (std::size_t g = 0; g < model.dat().groups().size(); ++g) {
        const std::vector<pinball::data::Field>& fields = model.dat().groups()[g].fields;
        for (std::size_t f = 0; f < fields.size(); ++f) {
            if (fields[f].type != 5) continue;
            const pinball::data::ByteView bytes = model.dat().payload(fields[f]);
            result = pinball360::decode_palette(bytes.data,bytes.size);
            ++count;
        }
    }
    require(count == 1,"real DAT palette count changed");
    return result;
}
void real_assets(const std::string& path)
{
    // Fresh model only; no supplied source/executable implementation inspected.
    const pinball::table::TableModel model = pinball::table::TableModel::load(path);
    const pinball360::Palette256 colors = palette(model);
    const std::size_t base = model.require_named_group("ball");
    const std::vector<BitmapResource> resources = ordered_image_resources(model,base);
    require(base == 108 && resources.size() == 7,"real ball resource sequence changed");
    const BallSpriteCache cache = BallSpriteCache::from_resources(model.dat(),resources,colors);
    for (std::size_t i = 0; i < cache.size(); ++i) {
        const BallSpriteInfo info = cache.info(i);
        require(info.resource.group_index == 108 + i && info.resource.bitmap_field_index == 1 &&
            info.width == 9 + i && info.height == 9 + i && info.stored_x == 0 && info.stored_y == 0,
            "real ball dimensions/order/metadata changed");
        const pinball::scene::Sprite& sprite = cache.sprite(i);
        require(sprite.depth.values.empty(),"real ball sprites must not contain a depth map");
        const std::size_t transparent = static_cast<std::size_t>(std::count(sprite.indices.begin(),sprite.indices.end(),0));
        require(transparent > 0 && transparent < sprite.indices.size(),"real ball must have both keyed and opaque samples");
        pinball::scene::Surface target(info.width,info.height);
        const pinball360::Rgba8 background = {7,8,9,255};
        target.clear(background,4000);
        draw_ball(target,cache,i,info.width / 2,info.height / 2,
            AnchorOffset(info.width / 2,info.height / 2),BallDrawOptions(1000,true));
        for (std::size_t p = 0; p < sprite.indices.size(); ++p) {
            if (sprite.indices[p] == 0) {
                require(target.pixels[p * 4U] == 7 && target.depth[p] == 4000,
                    "real ball transparent mask changed destination");
            } else {
                require(std::equal(sprite.bitmap.pixels.begin() + p * 4U,
                    sprite.bitmap.pixels.begin() + p * 4U + 4U,target.pixels.begin() + p * 4U) && target.depth[p] == 1000,
                    "real ball anchored render does not preserve palette pixels and explicit depth");
            }
        }
    }
    const BallSpriteCache reordered = BallSpriteCache::from_groups(model,{114,108,110},colors);
    require(reordered.info(0).width == 15 && reordered.info(1).width == 9 && reordered.info(2).width == 11,
        "explicit group order must remain caller-selected");
    require(BallSpriteCache::from_groups(model,{108},colors).size() == 1,
        "local group caching must not secretly expand its resource sequence");
    rejects([&] { BallSpriteCache::from_groups(model,{model.require_named_group("plunger")},colors); }, "depth-free");
    const std::vector<BitmapResource> plunger = ordered_image_resources(model,model.require_named_group("plunger"));
    require(plunger.size() == 8 && plunger.front().group_index == 473 && plunger.back().group_index == 480,
        "real plunger resource order changed");
    const std::vector<std::size_t> state_ids = {473,474,475,476,477,478,479,480};
    require(plunger_pose(state_ids,0,PullDirection::FirstToLast) == 473 &&
        plunger_pose(state_ids,1,PullDirection::FirstToLast) == 480,
        "real plunger mapper must use supplied resource order without timing inference");
    std::cout << "Real visual assets passed: seven9..15-pixel ball variants in literal order, indexed masks/depth,"
                 " eight plunger resources with explicit pull direction.\n";
}
}

int main(int argc,char** argv)
{
    if (argc > 2) { std::cerr << "Usage: pinball_ball_visual_tests [PINBALL.DAT]\n"; return 2; }
    try {
        cache_ownership_and_invalid_geometry();
        anchoring_mask_and_clipping();
        explicit_depth_visibility_and_overlap();
        anchor_subtraction_extremes();
        normalized_pull_has_explicit_order();
        if (argc == 2) real_assets(argv[1]);
        std::cout << "Ball visual tests passed: owned resources, explicit anchors/depth/visibility, clipping and"
                     " overflow guards, normalized ordered plunger pose selection.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ball visual test failed: " << error.what() << '\n';
        return 1;
    }
}
