#include "score_display.hpp"
#include "table_model.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace pinball {
namespace hud {
namespace {

scene::Rect intersect(const scene::Rect& a,const scene::Rect& b)
{
    if(a.width<=0 || a.height<=0 || b.width<=0 || b.height<=0)
        return scene::Rect(0,0,0,0);
    const std::int64_t left=(std::max)(std::int64_t(a.x),std::int64_t(b.x));
    const std::int64_t top=(std::max)(std::int64_t(a.y),std::int64_t(b.y));
    const std::int64_t right=(std::min)(std::int64_t(a.x)+a.width,std::int64_t(b.x)+b.width);
    const std::int64_t bottom=(std::min)(std::int64_t(a.y)+a.height,std::int64_t(b.y)+b.height);
    if(right<=left || bottom<=top)return scene::Rect(0,0,0,0);
    return scene::Rect(static_cast<int>(left),static_cast<int>(top),
                       static_cast<int>(right-left),static_cast<int>(bottom-top));
}

bool contained(const scene::Rect& inner,const scene::Rect& outer)
{
    return inner.width>0 && inner.height>0 && inner.x>=outer.x && inner.y>=outer.y &&
        std::int64_t(inner.x)+inner.width<=std::int64_t(outer.x)+outer.width &&
        std::int64_t(inner.y)+inner.height<=std::int64_t(outer.y)+outer.height;
}

const std::vector<std::int16_t>& numeric_record(const table::TableModel& model,
                                              const char* name)
{
    const table::GroupInventory& group=model.group(model.require_named_group(name));
    const std::vector<std::int16_t>* found=0;
    for(std::size_t r=0;r<group.short_records.size();++r) {
        const table::ShortRecord& record=group.short_records[r];
        if(record.field_type!=10)continue;
        if(found)throw std::runtime_error("ambiguous HUD numeric layout");
        found=&record.values;
    }
    if(!found || found->size()!=5 || (*found)[0]<0)
        throw std::runtime_error("invalid HUD numeric layout");
    return *found;
}

scene::Rect numeric_box(const std::vector<std::int16_t>& record)
{
    return scene::Rect(record[1],record[2],record[3],record[4]);
}

pinball360::Palette256 palette(const data::DatFile& dat)
{
    const data::Field* selected=0;
    for(std::size_t g=0;g<dat.groups().size();++g)
        for(std::size_t f=0;f<dat.groups()[g].fields.size();++f) {
            const data::Field& field=dat.groups()[g].fields[f];
            if(field.type!=5)continue;
            if(selected)throw std::runtime_error("HUD needs one unambiguous palette");
            selected=&field;
        }
    if(!selected)throw std::runtime_error("HUD palette missing");
    const data::ByteView bytes=dat.payload(*selected);
    return pinball360::decode_palette(bytes.data,bytes.size);
}

void validate_surface(const scene::Surface& surface)
{
    const std::size_t limit=(std::numeric_limits<std::size_t>::max)();
    if(!surface.width || !surface.height ||
       surface.width>static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
       surface.height>static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
       static_cast<std::size_t>(surface.height)>limit/surface.width/4U)
        throw std::runtime_error("invalid HUD surface extent");
    const std::size_t count=static_cast<std::size_t>(surface.width)*surface.height;
    if(surface.pixels.size()!=count*4U || surface.depth.size()!=count)
        throw std::runtime_error("invalid HUD surface storage");
}

void restore(scene::Surface& target,const scene::Surface& background,
             const scene::Rect& rect,const scene::Rect& clip)
{
    const scene::Rect area=intersect(rect,clip);
    if(area.width<=0 || area.height<=0)return;
    const std::size_t bytes=static_cast<std::size_t>(area.width)*4U;
    for(int y=area.y;y<area.y+area.height;++y) {
        const std::size_t offset=(static_cast<std::size_t>(y)*target.width+area.x)*4U;
        std::copy(background.pixels.begin()+offset,background.pixels.begin()+offset+bytes,
                  target.pixels.begin()+offset);
    }
}

} // namespace

HudState::HudState():score(0),ball_number(1),player(1),game_over(false),visible(true) {}

ScoreDisplay::ScoreDisplay(const table::TableModel& model):layout_(),sidebar_(),digits_()
{
    const std::vector<std::int16_t>& score=numeric_record(model,"score1");
    const std::vector<std::int16_t>& ball=numeric_record(model,"ballcount1");
    const std::vector<std::int16_t>& player=numeric_record(model,"player_number1");
    if(score[0]!=ball[0] || score[0]!=player[0])
        throw std::runtime_error("HUD numeric boxes require a shared digit set");
    layout_.digit_group=static_cast<std::size_t>(score[0]);
    layout_.score=numeric_box(score);layout_.ball=numeric_box(ball);layout_.player=numeric_box(player);
    layout_.info=scene::Rect(0,0,0,0);
    const table::GroupInventory& background=model.group(model.require_named_group("background"));
    if(background.images.size()!=1)throw std::runtime_error("HUD sidebar needs one background image");
    const data::BitmapHeader& header=background.images[0].bitmap;
    sidebar_=scene::Rect(header.x,header.y,header.width,header.height);
    if(!contained(layout_.score,sidebar_) || !contained(layout_.ball,sidebar_) ||
       !contained(layout_.player,sidebar_))
        throw std::runtime_error("HUD numeric layout leaves the sidebar");
    std::size_t info_group=0;
    if(model.find_named_group("info_text_box",info_group)) {
        const table::GroupInventory& info=model.group(info_group);
        bool found=false;
        for(std::size_t r=0;r<info.short_records.size();++r) {
            const table::ShortRecord& record=info.short_records[r];
            if(record.field_type!=10 || record.values.empty() || record.values[0]!=1500)continue;
            if(found || record.values.size()!=5)throw std::runtime_error("invalid HUD info layout");
            layout_.info=numeric_box(record.values);found=true;
        }
        if(found && !contained(layout_.info,sidebar_))throw std::runtime_error("HUD info layout leaves sidebar");
    }
    if(layout_.digit_group>model.groups().size() || model.groups().size()-layout_.digit_group<10)
        throw std::runtime_error("HUD decimal digit groups missing");
    const pinball360::Palette256 colors=palette(model.dat());
    int maximum_width=0;
    for(unsigned digit=0;digit<10;++digit) {
        const std::size_t group=layout_.digit_group+digit;
        const std::vector<table::ImageState>& images=model.group(group).images;
        if(images.size()!=1)throw std::runtime_error("HUD digit group needs one image");
        digits_[digit]=scene::decode_sprite(model.dat(),group,images[0].bitmap_field_index,colors);
        const scene::Sprite& image=digits_[digit];
        if(image.bitmap.height>layout_.score.height || image.bitmap.height>layout_.ball.height ||
           image.bitmap.height>layout_.player.height)
            throw std::runtime_error("HUD digit is taller than its numeric box");
        maximum_width=(std::max)(maximum_width,static_cast<int>(image.bitmap.width));
    }
    const int right=layout_.score.x+layout_.score.width;
    const int width=(std::max)(layout_.score.width,10*maximum_width);
    const int left=(std::max)(sidebar_.x,right-width);
    layout_.score_restore=scene::Rect(left,layout_.score.y,right-left,layout_.score.height);
}

const HudLayout& ScoreDisplay::layout() const {return layout_;}
const scene::Sprite& ScoreDisplay::digit(unsigned value) const {return digits_.at(value);}

void ScoreDisplay::draw_number(scene::Surface& target,std::uint64_t value,
                               const scene::Rect& field,const scene::Rect& clip) const
{
    const std::string decimal=std::to_string(value);
    std::int64_t pen=std::int64_t(field.x)+field.width;
    scene::DrawOptions options;
    options.clip=clip;options.depth_test=scene::DepthTest::Always;options.write_depth=false;
    for(std::size_t i=decimal.size();i>0;--i) {
        const scene::Sprite& image=digits_[static_cast<unsigned>(decimal[i-1]-'0')];
        pen-=image.bitmap.width;
        if(pen<(std::numeric_limits<int>::min)() || pen>(std::numeric_limits<int>::max)())
            throw std::runtime_error("HUD digit position exceeds coordinate range");
        scene::draw_sprite(target,image,static_cast<int>(pen),field.y,options);
    }
}

void ScoreDisplay::render(scene::Surface& target,const scene::Surface& background,
                          const HudState& state,const scene::Rect& clip) const
{
    if(&target==&background)throw std::runtime_error("HUD needs a distinct immutable background");
    validate_surface(target);validate_surface(background);
    if(target.width!=background.width || target.height!=background.height)
        throw std::runtime_error("HUD background dimensions differ");
    if(state.score>1000000000ULL || state.ball_number>9 || state.player==0 || state.player>9)
        throw std::runtime_error("HUD state outside numeric display range");
    const scene::Rect canvas(0,0,static_cast<int>(target.width),static_cast<int>(target.height));
    const scene::Rect visible=intersect(intersect(clip,canvas),sidebar_);
    restore(target,background,layout_.score_restore,visible);
    restore(target,background,layout_.ball,visible);
    restore(target,background,layout_.player,visible);
    if(!state.visible)return;
    draw_number(target,state.score,layout_.score,intersect(visible,layout_.score_restore));
    if(state.ball_number)draw_number(target,state.ball_number,layout_.ball,intersect(visible,layout_.ball));
    draw_number(target,state.player,layout_.player,intersect(visible,layout_.player));
}

} // namespace hud
} // namespace pinball
