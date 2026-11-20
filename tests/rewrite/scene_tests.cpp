#include "scene.hpp"
#include "table_model.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace pinball::scene;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}
void expect_error(const std::function<void()>& operation, const std::string& text)
{
    try { operation(); }
    catch (const std::exception& error) {
        require(std::string(error.what()).find(text) != std::string::npos,
                "wrong error: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("expected rejection containing: " + text);
}
void set16(std::vector<std::uint8_t>& bytes, std::size_t offset, unsigned value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 255U);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8) & 255U);
}

Sprite test_sprite(unsigned width, unsigned height, unsigned red)
{
    Sprite sprite;
    sprite.bitmap.width = static_cast<std::uint16_t>(width);
    sprite.bitmap.height = static_cast<std::uint16_t>(height);
    sprite.bitmap.x = sprite.bitmap.y = 0;
    sprite.indices.assign(width * height, 1);
    sprite.bitmap.pixels.resize(width * height * 4U);
    sprite.depth.width = width;
    sprite.depth.height = height;
    sprite.depth.values.assign(width * height, 100);
    for (std::size_t i = 0; i < width * height; ++i) {
        sprite.bitmap.pixels[i * 4U] = static_cast<std::uint8_t>(red);
        sprite.bitmap.pixels[i * 4U + 1U] = static_cast<std::uint8_t>(i + 1U);
        sprite.bitmap.pixels[i * 4U + 2U] = 5;
        sprite.bitmap.pixels[i * 4U + 3U] = 255;
    }
    return sprite;
}

void depth_decode()
{
    // Generated 3x2 image, stride4. Distinct bytes reveal endianness; padding
    // and bottom-up orientation must not enter the visible samples.
    std::vector<std::uint8_t> bytes(30, 0);
    set16(bytes, 0, 3); set16(bytes, 2, 2); set16(bytes, 4, 4);
    set16(bytes, 14, 0x0102); set16(bytes, 16, 0x7f80); set16(bytes, 18, 0xfffe);
    set16(bytes, 20, 0xeeee); // bottom-row padding
    set16(bytes, 22, 0x1234); set16(bytes, 24, 0xabcd); set16(bytes, 26, 0xffff);
    set16(bytes, 28, 0xdddd); // top-row padding
    const DepthMap depth = decode_depth_map(bytes.data(), bytes.size());
    const std::uint16_t expected[] = {0x1234, 0xabcd, 0xffff, 0x0102, 0x7f80, 0xfffe};
    require(depth.width == 3 && depth.height == 2 && depth.values.size() == 6,
            "depth shape/padding mismatch");
    require(std::equal(depth.values.begin(), depth.values.end(), expected),
            "depth LE words or bottom-up orientation mismatch");
    bytes.assign(bytes.size(), 0);
    require(depth.values[1] == 0xabcd, "depth samples must own their data");
    expect_error([&] { decode_depth_map(0, 30); }, "header");
    expect_error([&] { decode_depth_map(bytes.data(), 13); }, "header");
    set16(bytes, 0, 3); set16(bytes, 2, 2); set16(bytes, 4, 2);
    expect_error([&] { decode_depth_map(bytes.data(), bytes.size()); }, "stride");
    set16(bytes, 4, 4);
    expect_error([&] { decode_depth_map(bytes.data(), bytes.size() - 1U); }, "truncated");
    bytes.push_back(0);
    expect_error([&] { decode_depth_map(bytes.data(), bytes.size()); }, "trailing");
    set16(bytes, 0, 0xffff);
    expect_error([&] { decode_depth_map(bytes.data(), bytes.size()); }, "positive");
}

void clipped_transparency_depth()
{
    Surface target(4, 3);
    const pinball360::Rgba8 clear = {9, 8, 7, 255};
    target.clear(clear, 250);
    Sprite sprite = test_sprite(3, 2, 100);
    sprite.indices[1] = 0;
    // All colors can be black, including nonzero indexed pixels. Only the
    // palette index determines keyed transparency, never RGB equality.
    sprite.bitmap.pixels[0] = 0;
    sprite.bitmap.pixels[1] = 0;
    sprite.bitmap.pixels[2] = 0;
    sprite.depth.values[2] = 300; // behind existing target
    sprite.depth.values[4] = 250; // tie
    DrawOptions draw;
    draw.clip = Rect(1, 0, 2, 3);
    draw_sprite(target, sprite, 0, 1, draw);
    require(target.pixels[(1U * 4U + 1U) * 4U] == 9 && target.depth[5] == 250,
            "transparent pixel must preserve both existing buffers");
    require(target.pixels[(1U * 4U + 2U) * 4U] == 9 && target.depth[6] == 250,
            "occluded pixel must preserve both existing buffers");
    require(target.pixels[(2U * 4U + 1U) * 4U] == 100 && target.depth[9] == 250,
            "LessEqual must accept an equal-depth sample");
    require(target.pixels[(2U * 4U + 2U) * 4U] == 100 && target.depth[10] == 100,
            "visible clipped sample was not drawn");
    require(target.pixels[(2U * 4U) * 4U] == 9, "clip left edge was ignored");
    target.clear(clear, 250);
    draw.clip = Rect();
    draw_sprite(target, sprite, -1, -1, draw);
    require(target.pixels[0] == 100 && target.pixels[1] == 5,
            "negative position must advance to the correct source row/column");
    require(target.depth[0] == 250 && target.depth[1] == 100,
            "negative clipping must keep depth aligned");
    require(target.pixels[2U * 4U] == 9, "negative clipped right edge was overwritten");
    target.clear(clear, 250);
    draw_sprite(target, sprite, 1, 0, draw);
    require(target.pixels[4] == 0 && target.pixels[5] == 0 && target.depth[1] == 100,
            "nonzero indexed black must remain visible");
    draw.transparent_zero = false;
    draw.depth_test = DepthTest::Always;
    draw.write_depth = false;
    draw_sprite(target, sprite, 1, 0, draw);
    require(target.pixels[8] == 100 && target.depth[2] == 250,
            "opaque resource blit with depth writes disabled failed");
}

void depth_order_and_extreme_clip()
{
    Surface first(3, 3), second(3, 3);
    Sprite front = test_sprite(2, 2, 10), back = test_sprite(2, 2, 20);
    front.depth.values.assign(4, 0x0102);
    back.depth.values.assign(4, 0x0201);
    draw_sprite(first, front, 1, 1);
    draw_sprite(first, back, 1, 1);
    draw_sprite(second, back, 1, 1);
    draw_sprite(second, front, 1, 1);
    require(first.pixels == second.pixels && first.depth == second.depth,
            "unequal-depth overlaps must be draw-order independent");
    require(first.pixels[(1U * 3U + 1U) * 4U] == 10,
            "front sprite must win a non-byte-symmetric depth comparison");
    DrawOptions draw;
    draw.depth_test = DepthTest::Less;
    draw_sprite(first, back, 1, 1, draw);
    require(first.pixels[(1U * 3U + 1U) * 4U] == 10, "Less comparison failed");
    draw.depth_test = DepthTest::Greater;
    draw_sprite(first, back, 1, 1, draw);
    require(first.pixels[(1U * 3U + 1U) * 4U] == 20, "Greater comparison failed");
    const std::vector<std::uint8_t> before = first.pixels;
    draw.clip = Rect((std::numeric_limits<int>::max)(), 0, 100, 3);
    draw_sprite(first, front, 0, 0, draw);
    draw.clip = Rect();
    draw_sprite(first, front, (std::numeric_limits<int>::max)(), 0, draw);
    draw_sprite(first, front, (std::numeric_limits<int>::min)(), 0, draw);
    draw.clip = Rect(0, 0, -1, 3);
    draw_sprite(first, front, 0, 0, draw);
    require(first.pixels == before, "empty/extreme clips must leave surface untouched");
    Sprite no_depth = front;
    no_depth.depth = DepthMap();
    draw = DrawOptions();
    draw.constant_depth = 7;
    draw_sprite(first, no_depth, 0, 0, draw);
    require(first.depth[0] == 7, "sprite without map must use explicit constant depth");
    front.indices.pop_back();
    expect_error([&] { draw_sprite(first, front, 0, 0); }, "storage");
    expect_error([&] { Surface invalid(0, 1); }, "positive");
}

void state_reset_preserves_static_depth()
{
    ResourceScene scene;
    scene.background = Surface(5, 2);
    const pinball360::Rgba8 clear = {3, 4, 5, 255};
    scene.background.clear(clear, 200);
    scene.sprites.push_back(test_sprite(2, 2, 70));
    SpriteInstance instance = {};
    instance.sprite_index = 0;
    instance.visible = true;
    scene.instances.push_back(instance);
    scene.render();
    require(scene.surface.depth[0] == 100, "first sprite depth was not drawn");
    scene.set_position(0, 3, 0);
    scene.render();
    require(scene.surface.depth[0] == 200 && scene.surface.pixels[0] == 3,
            "moving sprite must restore static pixels and depth at old position");
    require(scene.surface.depth[3] == 100 && scene.surface.pixels[12] == 70,
            "moving sprite must draw at new position");
    Sprite shifted = test_sprite(1, 1, 80);
    shifted.bitmap.x = 1;
    shifted.bitmap.y = 1;
    scene.sprites.push_back(shifted);
    scene.set_state(0, 1, 1);
    require(scene.instances[0].x == 4 && scene.instances[0].y == 1,
            "state changes must retain the image-origin delta for differently cropped frames");
    scene.render();
    require(scene.surface.pixels[(1U * 5U + 4U) * 4U] == 80,
            "new state must draw at its adjusted origin");
    scene.set_visible(0, false);
    scene.render();
    require(scene.surface.pixels == scene.background.pixels &&
            scene.surface.depth == scene.background.depth,
            "hiding sprite must restore entire static scene");
}

void original_rebound_reactions(ResourceScene& scene,
                                const pinball::table::TableModel& model)
{
    const Surface idle = scene.surface;
    const Surface background = scene.background;
    const int expected_x[] = {108, 238, 230, 97};
    const int expected_y[] = {289, 289, 108, 128};
    const unsigned expected_width[] = {19, 19, 16, 36};
    const unsigned expected_height[] = {45, 45, 31, 19};
    unsigned reactions = 0, visible = 0, bumpers = 0;
    for (std::size_t i = 0; i < scene.instances.size(); ++i) {
        const SpriteInstance& instance = scene.instances[i];
        if (i != 0)
            require(scene.instances[i - 1].object_ordinal < instance.object_ordinal,
                    "scene instances must preserve DAT object order");
        if (instance.visible) ++visible;
        if (instance.object_type == 1005) {
            ++bumpers;
            require(instance.visible && instance.state_sprite_indices.size() == 8,
                    "all seven bumper resources must retain their eight image states");
        }
        if (instance.object_type != 1000 && instance.object_type != 1010) continue;
        require(instance.resource_group >= 469 && instance.resource_group <= 472,
                "unexpected bitmap-bearing ordinary wall");
        const std::size_t ordinal = instance.resource_group - 469;
        require(instance.object_type == 1000 && instance.object_ordinal == 60 + ordinal &&
                model.group(instance.resource_group).name == "v_rebo" + std::to_string(ordinal + 1),
                "rebound object/group identity changed");
        require(!instance.visible && instance.state_index == 0 &&
                instance.state_sprite_indices.size() == 1,
                "rebound frame must be cached but hidden at initialization");
        require(instance.x == expected_x[ordinal] && instance.y == expected_y[ordinal],
                "rebound stored-origin placement changed");
        const Sprite& sprite = scene.sprites[instance.sprite_index];
        require(sprite.group_index == instance.resource_group &&
                sprite.bitmap.width == expected_width[ordinal] &&
                sprite.bitmap.height == expected_height[ordinal] &&
                sprite.depth.values.size() == sprite.indices.size(),
                "rebound image shape or paired depth changed");
        scene.set_state(i, instance.state_sprite_indices[0], 0);
        scene.set_visible(i, true);
        scene.render();
        require(scene.surface.pixels != idle.pixels,
                "original rebound hit image must visibly change the idle scene");
        scene.set_visible(i, false);
        scene.render();
        require(scene.surface.pixels == idle.pixels && scene.surface.depth == idle.depth,
                "hiding each rebound must exactly restore idle color and depth");
        require(scene.background.pixels == background.pixels &&
                scene.background.depth == background.depth,
                "reaction rendering must leave the static background immutable");
        ++reactions;
    }
    require(reactions == 4 && visible == 51 && bumpers == 7,
            "hidden rebound or existing visible physical inventory changed");

    // Every state retains its own original crop origin. Exercise complete
    // cycles, including bumper fired frames, then return to the idle frame.
    for (std::size_t i = 0; i < scene.instances.size(); ++i) {
        const std::vector<pinball::table::ImageState>& states =
            model.image_states(scene.instances[i].resource_group);
        for (std::size_t state = 0; state < states.size(); ++state) {
            scene.set_state(i, scene.instances[i].state_sprite_indices[state], state);
            const SpriteInstance& instance = scene.instances[i];
            require(instance.x == scene.layout.table_x + states[state].bitmap.x - scene.table_stored_x &&
                    instance.y == scene.layout.table_y + states[state].bitmap.y - scene.table_stored_y &&
                    instance.stored_x == states[state].bitmap.x &&
                    instance.stored_y == states[state].bitmap.y,
                    "resource-state cycle lost the original image crop offset");
        }
        scene.set_state(i, scene.instances[i].state_sprite_indices[0], 0);
    }
    scene.render();
    require(scene.surface.pixels == idle.pixels && scene.surface.depth == idle.depth,
            "full resource-state cycles must return to exact idle composition");
}

void original_multiplier_lamps(ResourceScene& scene,
                               const pinball::table::TableModel& model)
{
    const Surface idle=scene.surface,background=scene.background;
    const int expected_x[]={157,146,137,125},expected_y[]={72,77,81,84};
    const int target_x[]={132,140,149},target_y[]={69,65,61};
    const std::int16_t declared[]={1027,172,171,170,169};
    const std::vector<pinball::table::ShortRecord>& records=
        model.group(model.require_named_group("top_target_lights")).short_records;
    bool membership=false;
    for(std::size_t r=0;r<records.size();++r)
        if(records[r].field_type==10 && records[r].values.size()==5 &&
           std::equal(records[r].values.begin(),records[r].values.end(),declared))membership=true;
    require(membership,"source-declared multiplier lamp order changed");
    unsigned lamps=0,targets=0;
    for(std::size_t i=0;i<scene.instances.size();++i) {
        const SpriteInstance& instance=scene.instances[i];
        if(instance.resource_group>=438 && instance.resource_group<=440) {
            const std::size_t t=instance.resource_group-438;
            const Sprite& sprite=scene.sprites[instance.sprite_index];
            require(instance.object_type==1006 && instance.object_ordinal==223+t && instance.visible &&
                    instance.state_sprite_indices.size()==1 && instance.state_index==0 &&
                    instance.x==target_x[t] && instance.y==target_y[t] &&
                    sprite.bitmap.width==12 && sprite.bitmap.height==13,
                    "existing popup target identity/frame/origin changed");
            scene.set_visible(i,false);scene.render();
            require(scene.surface.pixels!=idle.pixels,"cached popup target must disappear when dropped");
            scene.set_visible(i,true);scene.render();
            require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                    "raising a popup target must restore exact idle composition");
            ++targets;
        }
        if(instance.object_type!=1002 || instance.resource_group<169 || instance.resource_group>172)continue;
        require(instance.resource_group>=169 && instance.resource_group<=172,
                "scene must not initialize unrelated TLight resources");
        const std::size_t lamp=instance.resource_group-169;
        const Sprite& image=scene.sprites[instance.sprite_index];
        require(instance.object_ordinal==121+lamp && !instance.visible && instance.state_index==0 &&
                instance.state_sprite_indices.size()==1 && instance.x==expected_x[lamp] &&
                instance.y==expected_y[lamp] && image.group_index==instance.resource_group &&
                image.bitmap.width==9 && image.bitmap.height==7 && image.depth.values.size()==63,
                "selected lamp identity/on-frame/depth/origin changed");
        scene.set_state(i,instance.state_sprite_indices[0],0);
        scene.set_visible(i,true);scene.render();
        const Surface lit=scene.surface;
        require(lit.pixels!=idle.pixels,"each original multiplier lamp must change visible pixels");
        std::size_t sample=0;
        while(sample<lit.depth.size() && lit.depth[sample]==idle.depth[sample] &&
              std::equal(lit.pixels.begin()+sample*4U,lit.pixels.begin()+sample*4U+4U,
                         idle.pixels.begin()+sample*4U))++sample;
        require(sample<lit.depth.size(),"lamp produced no composited sample");
        const DrawOptions original_draw=instance.draw;
        scene.instances[i].draw.clip=Rect(static_cast<int>(sample%lit.width),
                                          static_cast<int>(sample/lit.width),1,1);
        scene.render();
        for(std::size_t pixel=0;pixel<lit.depth.size();++pixel) {
            const Surface& expected=pixel==sample?lit:idle;
            require(scene.surface.depth[pixel]==expected.depth[pixel] &&
                    std::equal(scene.surface.pixels.begin()+pixel*4U,
                               scene.surface.pixels.begin()+pixel*4U+4U,
                               expected.pixels.begin()+pixel*4U),
                    "lamp clipping changed a protected color/depth sample");
        }
        scene.instances[i].draw=original_draw;
        scene.set_visible(i,false);scene.render();
        require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                "turning each lamp off must restore exact idle color/depth");
        require(scene.background.pixels==background.pixels && scene.background.depth==background.depth,
                "lamp/target drawing must preserve immutable background");
        ++lamps;
    }
    require(lamps==4 && targets==3,"multiplier bank image inventory changed");
    SceneLayout shifted=scene.layout;shifted.table_x=17;shifted.table_y=-5;
    ResourceScene translated=static_resource_scene(model,shifted);
    unsigned transformed=0;
    for(std::size_t i=0;i<translated.instances.size();++i) {
        const SpriteInstance& instance=translated.instances[i];
        if(instance.object_type!=1002 || instance.resource_group<169 || instance.resource_group>172)continue;
        const std::size_t lamp=instance.resource_group-169;
        require(!instance.visible && instance.x==expected_x[lamp]+17 && instance.y==expected_y[lamp]-5,
                "selected lamp did not retain configured table translation");
        ++transformed;
    }
    require(transformed==4,"translated scene lost selected lamp membership");
}

void original_wormhole_lights(ResourceScene& scene,const pinball::table::TableModel& model)
{
    const Surface idle=scene.surface,background=scene.background;
    const std::size_t arrow_groups[]={116,119,122};
    const int arrow_x[]={122,247,214},arrow_y[]={47,232,86};
    const unsigned width[]={10,12,9},height[]={8,12,8};
    const std::int16_t members[2][4]={{1027,125,126,127},{1027,116,119,122}};
    const char* bank_names[]={"worm_hole_lights","bsink_arrow_lights"};
    for(unsigned bank=0;bank<2;++bank) {
        bool matched=false;
        for(const pinball::table::ShortRecord& r:model.group(model.require_named_group(bank_names[bank])).short_records)
            if(r.field_type==10 && r.values==std::vector<std::int16_t>(members[bank],members[bank]+4))matched=true;
        require(matched,"wormhole lamp/arrow declaration order changed");
    }
    unsigned arrows=0,lamps=0;
    for(std::size_t i=0;i<scene.instances.size();++i) {
        const SpriteInstance& instance=scene.instances[i];
        if(instance.resource_group>=125 && instance.resource_group<=127) {
            require(!instance.visible && instance.state_index==0 && instance.state_sprite_indices.size()==1,
                    "wormhole lamp must cache one hidden original on-frame");
            scene.set_visible(i,true);scene.render();
            require(scene.surface.pixels!=idle.pixels,"wormhole lamp on-frame is not visible");
            scene.set_visible(i,false);scene.render();
            require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                    "wormhole lamp hide did not restore original scene");
            ++lamps;
        }
        const std::size_t* found=std::find(arrow_groups,arrow_groups+3,instance.resource_group);
        if(found==arrow_groups+3)continue;
        const std::size_t n=static_cast<std::size_t>(found-arrow_groups);
        require(instance.object_type==1002 && !instance.visible && instance.state_index==0 &&
                instance.state_sprite_indices.size()==3,"wormhole arrow needs three hidden original states");
        const DrawOptions draw=instance.draw;
        for(std::size_t state=0;state<3;++state) {
            const Sprite& image=scene.sprites[instance.state_sprite_indices[state]];
            require(image.group_index==instance.resource_group+state && image.bitmap.width==width[n] &&
                    image.bitmap.height==height[n] && image.depth.values.size()==image.indices.size(),
                    "wormhole arrow source state/depth identity changed");
            if(state)require(image.indices!=scene.sprites[instance.state_sprite_indices[state-1]].indices,
                             "wormhole arrow states lost their distinct original indices");
            scene.set_state(i,instance.state_sprite_indices[state],state);
            require(instance.x==arrow_x[n] && instance.y==arrow_y[n],"wormhole arrow state moved from its original header origin");
            scene.set_visible(i,true);scene.render();const Surface lit=scene.surface;
            require(lit.pixels!=idle.pixels,"original wormhole arrow frame has no visible samples");
            std::size_t pixel=0;
            while(pixel<lit.depth.size() && lit.depth[pixel]==idle.depth[pixel] &&
                  std::equal(lit.pixels.begin()+pixel*4U,lit.pixels.begin()+pixel*4U+4U,idle.pixels.begin()+pixel*4U))++pixel;
            require(pixel<lit.depth.size(),"wormhole arrow has no changed color/depth sample");
            scene.instances[i].draw.clip=Rect(static_cast<int>(pixel%lit.width),static_cast<int>(pixel/lit.width),1,1);
            scene.render();
            for(std::size_t p=0;p<lit.depth.size();++p) {
                const Surface& expected=p==pixel?lit:idle;
                require(scene.surface.depth[p]==expected.depth[p] &&
                        std::equal(scene.surface.pixels.begin()+p*4U,scene.surface.pixels.begin()+p*4U+4U,expected.pixels.begin()+p*4U),
                        "wormhole arrow clip changed protected color/depth");
            }
            scene.instances[i].draw=draw;scene.set_visible(i,false);scene.render();
            require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                    "hiding changed wormhole arrow frame did not restore scene exactly");
        }
        scene.set_state(i,instance.state_sprite_indices[0],0);++arrows;
    }
    require(arrows==3 && lamps==3,"wormhole arrow/lamp inventory changed");
    require(scene.background.pixels==background.pixels && scene.background.depth==background.depth,
            "wormhole light rendering changed immutable background");
}

void original_advanced_dependency_lamps(ResourceScene& scene,const pinball::table::TableModel& model)
{
    const std::size_t groups[]={134,135,136,137,138,139,140,141,142,144,145,146,161,164,165,166,167,168,115,147,148,227,228,179};
    const char* names[]={"lite16","lite17","lite18","lite19","lite20","lite21","lite22","lite23","lite24", "lite26","lite27","lite28","lite54","lite58","lite59","lite60","lite61","lite62",
        "lite1","lite29","lite30","lite195","lite196","lite77"};
    const std::size_t count=sizeof(groups)/sizeof(groups[0]);
    const Surface idle=scene.surface,background=scene.background;
    unsigned seen=0;
    for(std::size_t i=0;i<scene.instances.size();++i) {
        const SpriteInstance& instance=scene.instances[i];
        const std::size_t* found=std::find(groups,groups+count,instance.resource_group);
        if(found==groups+count)continue;
        const std::size_t n=static_cast<std::size_t>(found-groups);
        const Sprite& image=scene.sprites[instance.sprite_index];
        require(model.group(instance.resource_group).name==names[n] && instance.object_type==1002 &&
                !instance.visible && instance.state_index==0 && instance.state_sprite_indices.size()==1 &&
                image.depth.values.size()==image.indices.size(),"advanced dependency light identity/frame/depth changed");
        if(instance.resource_group==179)require(instance.x==108&&instance.y==259&&
            image.bitmap.width==15&&image.bitmap.height==14,"original tilt lamp shape/origin changed");
        Surface isolated(image.bitmap.width,image.bitmap.height);const Surface blank=isolated;
        draw_sprite(isolated,image,0,0);
        require(isolated.pixels!=blank.pixels,"advanced dependency light contains no visible original art");
        scene.set_visible(i,true);scene.render();scene.set_visible(i,false);scene.render();
        require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                "advanced dependency hide must restore original scene color/depth");
        ++seen;
    }
    require(seen==count,"advanced dependency light whitelist changed");
    require(scene.background.pixels==background.pixels && scene.background.depth==background.depth,
            "advanced dependency light modified immutable background");
}

void original_mission_lamps(ResourceScene& scene,const pinball::table::TableModel& model)
{
    std::vector<std::size_t> groups;
    const std::size_t outer[]={196,160,197,159,198,158,199,157,200,156,201,155,202,154,203,153,204,152};
    for(std::size_t group=232;group<=254;++group) {
        require(model.group(group).name==std::string("lite")+std::to_string(300+group-232),
                "original mission goal number/group mapping changed");
        groups.push_back(group);
    }
    for(std::size_t group=205;group<=213;++group)groups.push_back(group);
    groups.insert(groups.end(),outer,outer+18);
    const std::size_t standalone[]={143,163,229,230,231};
    groups.insert(groups.end(),standalone,standalone+5);
    const Surface idle=scene.surface,background=scene.background;
    unsigned seen=0;
    for(std::size_t i=0;i<scene.instances.size();++i) {
        const SpriteInstance& instance=scene.instances[i];
        if(std::find(groups.begin(),groups.end(),instance.resource_group)==groups.end())continue;
        const Sprite& image=scene.sprites[instance.sprite_index];
        require(instance.object_type==1002 && !instance.visible && instance.state_index==0 &&
                instance.state_sprite_indices.size()==1 && image.depth.values.size()==image.indices.size(),
                "mission/rank indicator must preserve hidden original frame/depth");
        // Test the original image independently as well as in the resource
        // scene, where other physical sprites may legitimately occlude it.
        Surface isolated(image.bitmap.width,image.bitmap.height);
        const Surface empty=isolated;
        draw_sprite(isolated,image,0,0);
        require(isolated.pixels!=empty.pixels,"mission/rank indicator contains no visible original art");
        scene.set_visible(i,true);scene.render();
        scene.set_visible(i,false);scene.render();
        require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                "mission/rank indicator hide must restore exact original scene");
        ++seen;
    }
    require(seen==55,"explicit mission/rank indicator membership changed");
    require(scene.background.pixels==background.pixels && scene.background.depth==background.depth,
            "mission/rank rendering modified immutable background");
}

void original_effect_lamps(ResourceScene& scene)
{
    const std::size_t groups[]={173,174,175,176,177,178,182,183,184,185,186,187,188,189,190,191,193,194,195,255,256,257,258,259,260};
    const int x[]={301,294,288,83,104,118,106,105,104,97,96,98,222,226,230,223,282,270,243,98,90,83,77,73,70};
    const int y[]={233,181,135,44,30,35,218,229,240,102,112,121,86,93,101,146,94,62,39,188,175,163,151,139,127};
    const unsigned w[]={19,18,17,8,8,9,10,10,10,9,9,9,9,10,9,9,17,14,16,27,24,21,18,15,15};
    const unsigned h[]={16,15,13,7,6,6,10,10,9,8,8,7,7,7,8,9,12,14,13,17,16,14,13,10,9};
    const std::size_t count=sizeof(groups)/sizeof(groups[0]);
    const Surface idle=scene.surface,background=scene.background;
    unsigned found_count=0;
    for(std::size_t i=0;i<scene.instances.size();++i) {
        const SpriteInstance& instance=scene.instances[i];
        const std::size_t* found=std::find(groups,groups+count,instance.resource_group);
        if(found==groups+count)continue;
        const std::size_t n=static_cast<std::size_t>(found-groups);
        const Sprite& sprite=scene.sprites[instance.sprite_index];
        require(instance.object_type==1002 && !instance.visible && instance.state_index==0 &&
                instance.state_sprite_indices.size()==1 && instance.x==x[n] && instance.y==y[n] &&
                sprite.bitmap.width==w[n] && sprite.bitmap.height==h[n] && sprite.depth.values.size()==sprite.indices.size(),
                "mapped effect lamp original frame/shape/origin/depth changed");
        scene.set_visible(i,true);scene.render();
        if(instance.resource_group==259) {
            unsigned opaque=0,front=0,background_front=0;
            for(unsigned py=0;py<sprite.bitmap.height;++py)for(unsigned px=0;px<sprite.bitmap.width;++px) {
                const std::size_t source=static_cast<std::size_t>(py)*sprite.bitmap.width+px;
                const std::size_t dest=static_cast<std::size_t>(instance.y+py)*idle.width+instance.x+px;
                if(!sprite.indices[source])continue;
                ++opaque;
                if(sprite.depth.values[source]<=idle.depth[dest])++front;
                if(sprite.depth.values[source]<=background.depth[dest])++background_front;
            }
            require(opaque==88 && front==0 && scene.surface.pixels==idle.pixels,
                    "fuel lamp259 must retain observed static-scene depth occlusion");
            std::cout<<"Fuel lamp259 depth-pass against immutable background: "<<background_front<<"/88\n";
        } else require(scene.surface.pixels!=idle.pixels,"mapped effect lamp has no visible on-state");
        scene.set_visible(i,false);scene.render();
        require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                "effect lamp off did not restore exact scene color/depth");
        ++found_count;
    }
    require(found_count==count,"mapped effect lamp whitelist changed");
    require(scene.background.pixels==background.pixels && scene.background.depth==background.depth,
            "effect lamp rendering modified immutable background");
}

void original_medal_lamps(ResourceScene& scene,const pinball::table::TableModel& model)
{
    const Surface idle=scene.surface,background=scene.background;
    const int y[]={167,156,147},height[]={9,9,8};
    const std::size_t bank=model.require_named_group("bumper_target_lights");
    bool declared=false;
    for(const pinball::table::ShortRecord& record:model.group(bank).short_records)
        if(record.field_type==10 && record.values==std::vector<std::int16_t>({1027,133,132,131}))declared=true;
    require(declared,"medal lamp declaration/order changed");
    unsigned count=0;
    for(std::size_t i=0;i<scene.instances.size();++i) {
        const SpriteInstance& instance=scene.instances[i];
        if(instance.resource_group<131 || instance.resource_group>133)continue;
        const std::size_t n=instance.resource_group-131;
        const Sprite& image=scene.sprites[instance.sprite_index];
        require(instance.object_type==1002 && instance.object_ordinal==83+n && !instance.visible &&
                instance.state_index==0 && instance.state_sprite_indices.size()==1 &&
                instance.x==168 && instance.y==y[n] && image.bitmap.width==10 &&
                image.bitmap.height==static_cast<unsigned>(height[n]) && image.depth.values.size()==image.indices.size(),
                "medal on-frame identity/origin/depth changed");
        scene.set_visible(i,true);scene.render();
        require(scene.surface.pixels!=idle.pixels,"medal on-frame must visibly change scene");
        scene.set_visible(i,false);scene.render();
        require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                "medal off must restore exact original idle color/depth");
        require(scene.background.pixels==background.pixels && scene.background.depth==background.depth,
                "medal light rendering modified immutable background");
        ++count;
    }
    require(count==3,"mapped medal lamp inventory changed");
}

void original_lane_and_bumper_images(ResourceScene& scene,
                                      const pinball::table::TableModel& model)
{
    const Surface idle=scene.surface,background=scene.background;
    const std::size_t groups[]={128,129,130,214,215,216};
    const int x[]={195,178,161,53,69,85},y[]={50,47,50,181,185,188};
    const unsigned widths[]={9,9,9,8,8,8},heights[]={6,7,6,7,7,7};
    const std::size_t ordinals[]={80,81,82,166,167,168};
    unsigned lane_lamps=0,bumpers=0,rollovers=0;
    for(std::size_t i=0;i<scene.instances.size();++i) {
        const SpriteInstance& instance=scene.instances[i];
        const std::size_t* found=std::find(groups,groups+6,instance.resource_group);
        if(found!=groups+6) {
            const std::size_t n=static_cast<std::size_t>(found-groups);
            const Sprite& image=scene.sprites[instance.sprite_index];
            require(instance.object_type==1002 && instance.object_ordinal==ordinals[n] &&
                    !instance.visible && instance.state_index==0 && instance.state_sprite_indices.size()==1 &&
                    instance.x==x[n] && instance.y==y[n] && image.bitmap.width==widths[n] &&
                    image.bitmap.height==heights[n] && image.depth.values.size()==image.indices.size(),
                    "mapped lane lamp identity/shape/origin/depth changed");
            scene.set_visible(i,true);scene.render();
            require(scene.surface.pixels!=idle.pixels,"mapped lane lamp must change original visible pixels");
            scene.set_visible(i,false);scene.render();
            require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                    "lane lamp off must restore exact idle color/depth");
            ++lane_lamps;
        }
        if(instance.object_type==1005) {
            require(instance.state_sprite_indices.size()==8,"bumper needs four idle/fired level pairs");
            for(std::size_t level=0;level<4;++level) {
                scene.set_state(i,instance.state_sprite_indices[level*2],level*2);scene.render();
                const Surface level_idle=scene.surface;
                scene.set_state(i,instance.state_sprite_indices[level*2+1],level*2+1);scene.render();
                require(scene.surface.pixels!=level_idle.pixels,"each original bumper level must have distinct fired art");
            }
            scene.set_state(i,instance.state_sprite_indices[0],0);scene.render();
            require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                    "bumper level/fire cycles must restore original idle frame and depth");
            ++bumpers;
        }
        if(instance.resource_group==520 || instance.resource_group==521 || instance.resource_group==522 ||
           instance.resource_group==528 || instance.resource_group==529 || instance.resource_group==530) {
            require(instance.object_type==1015 && instance.visible && instance.state_index==0 &&
                    instance.state_sprite_indices.size()==1,"mapped lane rollover image state changed");
            scene.set_visible(i,false);scene.render();
            scene.set_visible(i,true);scene.render();
            require(scene.surface.pixels==idle.pixels && scene.surface.depth==idle.depth,
                    "rollover entry/exit visibility must restore exact initial art");
            ++rollovers;
        }
    }
    require(lane_lamps==6 && bumpers==7 && rollovers==6,"mapped lane/bumper image inventory changed");
    require(scene.background.pixels==background.pixels && scene.background.depth==background.depth,
            "lane and bumper rendering changed immutable background");
    const std::size_t bank_groups[]={276,277};
    const std::int16_t members[2][4]={{1027,128,129,130},{1027,216,215,214}};
    for(unsigned bank=0;bank<2;++bank) {
        const std::vector<pinball::table::ShortRecord>& records=model.group(bank_groups[bank]).short_records;
        bool matched=false;
        for(std::size_t r=0;r<records.size();++r)
            if(records[r].field_type==10 && records[r].values.size()==4 &&
               std::equal(records[r].values.begin(),records[r].values.end(),members[bank]))matched=true;
        require(matched,"source lane lamp group order changed");
    }
}

void original_asset_integration(const std::string& path)
{
    const pinball::table::TableModel model = pinball::table::TableModel::load(path);
    const pinball::data::DatFile& dat = model.dat();
    pinball360::Palette256 palette;
    unsigned palettes = 0;
    for (std::size_t g = 0; g < dat.groups().size(); ++g) {
        for (std::size_t f = 0; f < dat.groups()[g].fields.size(); ++f) {
            if (dat.groups()[g].fields[f].type == 5) {
                const pinball::data::ByteView bytes = dat.payload(dat.groups()[g].fields[f]);
                palette = pinball360::decode_palette(bytes.data, bytes.size);
                ++palettes;
            }
        }
    }
    require(palettes == 1, "reference palette count changed");
    unsigned bitmaps = 0, depths = 0;
    for (std::size_t g = 0; g < dat.groups().size(); ++g) {
        for (std::size_t f = 0; f < dat.groups()[g].fields.size(); ++f) {
            if (dat.groups()[g].fields[f].type != 1) continue;
            const Sprite sprite = decode_sprite(dat, g, f, palette);
            ++bitmaps;
            if (!sprite.depth.values.empty()) ++depths;
            if (g == 355 || g == 363 || g == 372 || g == 405 || g == 473 || g == 501) {
                for (std::size_t i = 0; i < sprite.indices.size(); ++i)
                    require((sprite.indices[i] == 0) == (sprite.depth.values[i] == 65535U),
                            "reference physical sprite zero mask no longer matches far-depth mask");
            }
        }
    }
    require(bitmaps == 318 && depths == 300, "reference image/depth counts changed");
    SceneLayout layout;
    layout.height = 480; // Explicit full-raster inspection canvas, not a runtime claim.
    ResourceScene scene = static_resource_scene(model, layout);
    require(scene.surface.width == 600 && scene.surface.height == 480, "scene canvas mismatch");
    require(scene.instances.size() == 178, "reference physical/hit/lamp image inventory changed");
    bool left = false, right = false, plunger = false;
    std::size_t changed = 0;
    for (std::size_t i = 0; i < scene.surface.depth.size(); ++i) {
        if (scene.surface.depth[i] != scene.background.depth[i]) ++changed;
    }
    require(changed > 5000, "physical resources did not meaningfully composite over the table");
    for (std::size_t i = 0; i < scene.instances.size(); ++i) {
        const SpriteInstance& instance = scene.instances[i];
        if (instance.object_type == 1003) {
            require(instance.resource_group == 355 && instance.x == 124 && instance.y == 376,
                    "left flipper inferred transform changed");
            require(instance.state_sprite_indices.size() == 8, "left flipper state inventory changed");
            left = true;
        }
        if (instance.object_type == 1004) {
            require(instance.resource_group == 363 && instance.x == 198 && instance.y == 376,
                    "right flipper inferred transform changed");
            right = true;
        }
        if (instance.object_type == 1001) {
            require(instance.resource_group == 473 && instance.x == 324 && instance.y == 381,
                    "plunger inferred transform changed");
            plunger = true;
        }
        if(instance.object_type==1002)
            require(((instance.resource_group>=169 && instance.resource_group<=172) ||
                     (instance.resource_group>=125 && instance.resource_group<=148) ||
                     (instance.resource_group>=214 && instance.resource_group<=216) ||
                     (instance.resource_group>=173 && instance.resource_group<=179) ||
                     (instance.resource_group>=182 && instance.resource_group<=191) ||
                     (instance.resource_group>=193 && instance.resource_group<=213) ||
                     (instance.resource_group>=152 && instance.resource_group<=160) ||
                     (instance.resource_group>=227 && instance.resource_group<=260) ||
                     instance.resource_group==115 || instance.resource_group==116 ||
                     instance.resource_group==119 || instance.resource_group==122 || instance.resource_group==161 ||
                     (instance.resource_group>=163 && instance.resource_group<=168)) && !instance.visible,
                    "only mapped profile lamps may be cached, initially hidden");
    }
    require(left && right && plunger, "physical scene is missing flippers or plunger");
    original_rebound_reactions(scene, model);
    original_multiplier_lamps(scene, model);
    original_medal_lamps(scene, model);
    original_effect_lamps(scene);
    original_mission_lamps(scene, model);
    original_advanced_dependency_lamps(scene, model);
    original_wormhole_lights(scene, model);
    original_lane_and_bumper_images(scene, model);
    const Sprite flipper = decode_sprite(dat, 355, model.group(355).images[0].bitmap_field_index, palette);
    const Sprite bumper = decode_sprite(dat, 372, model.group(372).images[0].bitmap_field_index, palette);
    Surface scratch = scene.background;
    draw_sprite(scratch, flipper, 124, 376);
    unsigned front_count = 0, back_count = 0;
    for (std::size_t y = 0; y < bumper.bitmap.height; ++y) {
        for (std::size_t x = 0; x < bumper.bitmap.width; ++x) {
            const std::size_t src = y * bumper.bitmap.width + x;
            if (bumper.indices[src] == 0) continue;
            const std::size_t dst = (110 + y) * scratch.width + 170 + x;
            if (bumper.depth.values[src] < scratch.depth[dst]) ++front_count;
            if (bumper.depth.values[src] > scratch.depth[dst]) ++back_count;
        }
    }
    require(front_count == 360 && back_count == 118,
            "reference bumper/table overlap samples changed (orientation/transform/depth)");
    draw_sprite(scratch, bumper, 170, 110);
    for (std::size_t y = 0; y < bumper.bitmap.height; ++y) {
        for (std::size_t x = 0; x < bumper.bitmap.width; ++x) {
            const std::size_t src = y * bumper.bitmap.width + x;
            const std::size_t dst = (110 + y) * scratch.width + 170 + x;
            const bool visible = bumper.indices[src] != 0 &&
                bumper.depth.values[src] <= scene.background.depth[dst];
            require(scratch.depth[dst] == (visible ? bumper.depth.values[src] :
                                                       scene.background.depth[dst]),
                    "reference bumper depth occlusion mismatch");
        }
    }
    const std::vector<std::uint8_t> rendered = scene.surface.pixels;
    scene.render();
    require(scene.surface.pixels == rendered, "resource scene render must be deterministic");
    std::cout << "Asset integration passed: all 318 images, all300 paired depths, zero/far masks, 51 visible objects,"
                 " four hidden rebounds/one hundred twenty-three mapped hidden lamps, bumper level pairs, rollover/popup visibility and state placement,"
                 " flipper/plunger transforms, 360 front + 118 behind bumper samples.\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 2) {
        std::cerr << "Usage: pinball_scene_tests [PINBALL.DAT]\n";
        return 2;
    }
    try {
        depth_decode();
        clipped_transparency_depth();
        depth_order_and_extreme_clip();
        state_reset_preserves_static_depth();
        if (argc == 2) original_asset_integration(argv[1]);
        std::cout << "Scene tests passed: explicitLE depth/stride/orientation, clipped keyed transparency,"
                     " depth overlap/ties, extreme coordinates, frame reset.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene test failed: " << error.what() << '\n';
        return 1;
    }
}
