#include "message_display.hpp"
#include "table_model.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pinball {
namespace hud {
namespace {
scene::Rect intersect(const scene::Rect& a, const scene::Rect& b)
{
    if (a.width<=0 || a.height<=0 || b.width<=0 || b.height<=0)
        return scene::Rect(0,0,0,0);
    const std::int64_t x=(std::max)(std::int64_t(a.x),std::int64_t(b.x));
    const std::int64_t y=(std::max)(std::int64_t(a.y),std::int64_t(b.y));
    const std::int64_t right=(std::min)(std::int64_t(a.x)+a.width,std::int64_t(b.x)+b.width);
    const std::int64_t bottom=(std::min)(std::int64_t(a.y)+a.height,std::int64_t(b.y)+b.height);
    if (right<=x || bottom<=y) return scene::Rect(0,0,0,0);
    return scene::Rect(static_cast<int>(x),static_cast<int>(y),
                       static_cast<int>(right-x),static_cast<int>(bottom-y));
}
bool contained(const scene::Rect& inner, const scene::Rect& outer)
{
    return inner.width>0 && inner.height>0 && inner.x>=outer.x && inner.y>=outer.y &&
        std::int64_t(inner.x)+inner.width<=std::int64_t(outer.x)+outer.width &&
        std::int64_t(inner.y)+inner.height<=std::int64_t(outer.y)+outer.height;
}
void validate_layout(const MessageLayout& layout)
{
    const scene::Rect& sidebar=layout.sidebar;
    if (sidebar.x<0 || sidebar.y<0 || sidebar.width<=0 || sidebar.height<=0 ||
        std::int64_t(sidebar.x)+sidebar.width>(std::numeric_limits<int>::max)() ||
        std::int64_t(sidebar.y)+sidebar.height>(std::numeric_limits<int>::max)() ||
        !contained(layout.info,sidebar) || !contained(layout.mission,sidebar))
        throw std::runtime_error("invalid message-box layout");
    const scene::Rect overlap=intersect(layout.info,layout.mission);
    if (overlap.width>0 && overlap.height>0)
        throw std::runtime_error("message boxes overlap");
}
scene::Rect text_box(const table::TableModel& model,const char* name)
{
    const table::GroupInventory& group=model.group(model.require_named_group(name));
    const std::vector<std::int16_t>* selected=0;
    for (std::size_t i=0;i<group.short_records.size();++i) {
        const table::ShortRecord& record=group.short_records[i];
        if (record.field_type!=10 || record.values.empty() || record.values[0]!=1500) continue;
        if (selected || record.values.size()!=5)
            throw std::runtime_error("ambiguous or malformed DAT message box");
        selected=&record.values;
    }
    if (!selected) throw std::runtime_error("DAT message-box layout missing");
    return scene::Rect((*selected)[1],(*selected)[2],(*selected)[3],(*selected)[4]);
}
MessageLayout model_layout(const table::TableModel& model)
{
    MessageLayout result;
    result.info=text_box(model,"info_text_box");
    result.mission=text_box(model,"mission_text_box");
    const table::GroupInventory& group=model.group(model.require_named_group("background"));
    if (group.images.size()!=1) throw std::runtime_error("message sidebar needs one background");
    const data::BitmapHeader& h=group.images[0].bitmap;
    result.sidebar=scene::Rect(h.x,h.y,h.width,h.height);
    return result;
}
font::ColorLookup model_colors(const table::TableModel& model)
{
    const data::DatFile& dat=model.dat();
    const data::Field* selected=0;
    for (std::size_t g=0;g<dat.groups().size();++g)
        for (std::size_t f=0;f<dat.groups()[g].fields.size();++f) {
            const data::Field& field=dat.groups()[g].fields[f];
            if (field.type!=5) continue;
            if (selected) throw std::runtime_error("message font palette is ambiguous");
            selected=&field;
        }
    if (!selected) throw std::runtime_error("message font palette missing");
    const data::ByteView bytes=dat.payload(*selected);
    return pinball360::decode_palette(bytes.data,bytes.size);
}
void validate_surface(const scene::Surface& surface)
{
    const std::size_t limit=(std::numeric_limits<std::size_t>::max)();
    if (!surface.width || !surface.height ||
        surface.width>static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
        surface.height>static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
        static_cast<std::size_t>(surface.height)>limit/surface.width/4U)
        throw std::runtime_error("invalid message surface extent");
    const std::size_t samples=static_cast<std::size_t>(surface.width)*surface.height;
    if (surface.pixels.size()!=samples*4U || surface.depth.size()!=samples)
        throw std::runtime_error("invalid message surface storage");
}
std::string normalized(const std::string& input,const font::BitmapFont& font)
{
    std::string result;
    result.reserve(input.size());
    for (std::size_t i=0;i<input.size();++i) {
        const unsigned c=static_cast<unsigned char>(input[i]);
        if (c=='\r' && i+1<input.size() && input[i+1]=='\n') { ++i; result+='\n'; }
        else if (c=='\n' || font.contains(c)) result+=static_cast<char>(c);
        else throw std::runtime_error("message text requires ASCII font glyphs or LF/CRLF");
    }
    return result;
}
struct Line { std::size_t begin,end; Line(std::size_t b,std::size_t e):begin(b),end(e) {} };
std::vector<Line> wrapped_lines(const std::string& text,const font::BitmapFont& font,
                                const scene::Rect& box)
{
    std::vector<Line> result;
    const std::size_t maximum=static_cast<unsigned>(box.height)/font.height();
    std::size_t cursor=0;
    while (cursor<text.size() && result.size()<maximum) {
        const std::size_t begin=cursor;
        std::size_t end=cursor,last_space=std::string::npos;
        unsigned used=0;
        while (end<text.size() && text[end]!='\n') {
            const unsigned c=static_cast<unsigned char>(text[end]);
            const unsigned advance=font.glyph(c).width+font.gap();
            if (advance>static_cast<unsigned>(box.width)-used) break;
            used+=advance;
            if (c==' ') last_space=end;
            ++end;
        }
        const bool overflow=end<text.size() && text[end]!='\n';
        if (overflow && last_space!=std::string::npos) end=last_space;
        if (end==begin && text[begin]!='\n' && last_space==std::string::npos)
            break; // first glyph cannot fit
        result.push_back(Line(begin,end));
        cursor=end;
        while (cursor<text.size() && text[cursor]==' ') ++cursor;
        if (cursor<text.size() && text[cursor]=='\n') ++cursor;
        // An unbroken word continues on the following line at the first glyph
        // which did not fit. All arithmetic above stays bounded by box.width.
    }
    return result;
}
void restore(scene::Surface& target,const scene::Surface& background,const scene::Rect& area)
{
    if (area.width<=0 || area.height<=0) return;
    const std::size_t bytes=static_cast<std::size_t>(area.width)*4U;
    for (int row=0;row<area.height;++row) {
        const std::size_t offset=(static_cast<std::size_t>(area.y+row)*target.width+area.x)*4U;
        std::copy(background.pixels.begin()+offset,background.pixels.begin()+offset+bytes,
                  target.pixels.begin()+offset);
    }
}
} // namespace

MessageState::MessageState():info(),mission(),visible(true) {}
MessageDisplay::MessageDisplay(const table::TableModel& model,const font::BitmapFont& font)
    :MessageDisplay(model_layout(model),font,model_colors(model)) {}
MessageDisplay::MessageDisplay(const MessageLayout& layout,const font::BitmapFont& font,
                               const font::ColorLookup& colors)
    :layout_(layout),font_(font),glyphs_()
{
    validate_layout(layout_);
    if (!font_.height()) throw std::runtime_error("message font has zero height");
    for (unsigned c=32;c<128;++c) {
        const font::IndexedGlyph& source=font_.glyph(c);
        scene::Sprite& target=glyphs_[c-32];
        target.bitmap.width=source.width;target.bitmap.height=source.height;
        target.bitmap.x=target.bitmap.y=0;
        target.indices=source.indices;
        const std::size_t count=static_cast<std::size_t>(source.width)*source.height;
        if (count>(std::numeric_limits<std::size_t>::max)()/4U ||
            count>target.bitmap.pixels.max_size()/4U || count!=source.indices.size())
            throw std::runtime_error("message glyph extent exceeds storage");
        target.bitmap.pixels.resize(count*4U);
        for (std::size_t p=0;p<count;++p) {
            const pinball360::Rgba8 color=colors[source.indices[p]];
            target.bitmap.pixels[p*4U]=color.r;target.bitmap.pixels[p*4U+1U]=color.g;
            target.bitmap.pixels[p*4U+2U]=color.b;target.bitmap.pixels[p*4U+3U]=color.a;
        }
    }
}
const MessageLayout& MessageDisplay::layout() const { return layout_; }
const font::BitmapFont& MessageDisplay::bitmap_font() const { return font_; }
const scene::Sprite& MessageDisplay::glyph(unsigned character) const
{
    if (character<32 || character>127) throw std::out_of_range("message glyph outside ASCII font");
    return glyphs_[character-32];
}
void MessageDisplay::render(scene::Surface& target,const scene::Surface& background,
                            const MessageState& state,const scene::Rect& clip) const
{
    if (&target==&background) throw std::runtime_error("message background must be distinct");
    validate_surface(target);validate_surface(background);
    if (target.width!=background.width || target.height!=background.height)
        throw std::runtime_error("message background dimensions differ");
    const std::string text[2]={normalized(state.info,font_),normalized(state.mission,font_)};
    const scene::Rect boxes[2]={layout_.info,layout_.mission};
    const std::vector<Line> lines[2]={wrapped_lines(text[0],font_,boxes[0]),wrapped_lines(text[1],font_,boxes[1])};
    const scene::Rect canvas(0,0,static_cast<int>(target.width),static_cast<int>(target.height));
    const scene::Rect visible=intersect(intersect(clip,canvas),layout_.sidebar);
    for (unsigned box=0;box<2;++box) {
        scene::DrawOptions draw;
        draw.clip=intersect(visible,boxes[box]);draw.depth_test=scene::DepthTest::Always;
        draw.write_depth=false;draw.transparent_zero=true;
        restore(target,background,draw.clip);
        if (!state.visible) continue;
        for (std::size_t row=0;row<lines[box].size();++row) {
            int x=boxes[box].x;
            const int y=boxes[box].y+static_cast<int>(row*font_.height());
            const Line& line=lines[box][row];
            for (std::size_t i=line.begin;i<line.end;++i) {
                const unsigned c=static_cast<unsigned char>(text[box][i]);
                const scene::Sprite& image=glyph(c);
                if (image.bitmap.width) scene::draw_sprite(target,image,x,y,draw);
                x+=image.bitmap.width+font_.gap();
            }
        }
    }
}
} // namespace hud
} // namespace pinball
