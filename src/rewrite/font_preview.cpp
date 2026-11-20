#include "bitmap_font.hpp"
#include "scene.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace {
std::string quoted(const std::string& input)
{
    std::ostringstream out;
    out << '"';
    for (std::size_t i = 0; i < input.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == '\\' || c == '"') out << '\\';
        if (c >= 32) out << c;
    }
    out << '"';
    return out.str();
}
}

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: pinball_font_preview <PINBALL2.MID> <output.bmp> [FONT.DAT]\n";
        return 2;
    }
    try {
        const pinball::font::BitmapFont font = pinball::font::BitmapFont::load(argv[1]);
        pinball::font::FontPaint paint;
        const pinball360::Rgba8 white = {255,255,255,255}, gray = {140,140,140,255};
        paint.colors[80] = white;
        paint.colors[105] = gray;
        pinball::scene::Surface surface(640, argc == 4 ? 350 : 250);
        pinball::font::draw_text(surface, font, "SPACE CADET 0123456789", 12, 12, paint);
        for (std::size_t i = 0; i < font.glyphs().size(); ++i)
            pinball::font::draw_glyph(surface, font.glyphs()[i],
                12 + static_cast<int>(i % 16U) * 38, 55 + static_cast<int>(i / 16U) * 30, paint);
        pinball::font::GlyphAtlas atlas;
        if (argc == 4) {
            atlas = pinball::font::load_atlas(argv[3]);
            pinball::font::draw_text(surface, font, "FONT DAT ORDINALS", 12, 245, paint);
            pinball::font::FontPaint atlas_paint;
            atlas_paint.colors[28] = white;
            atlas_paint.colors[53] = gray;
            const pinball360::Rgba8 light = {200,200,200,255};
            atlas_paint.colors[55] = light;
            for (std::size_t i = 0; i < atlas.glyphs.size(); ++i) {
                const int x = 12 + static_cast<int>(i) * 58;
                pinball::font::draw_glyph(surface, atlas.glyphs[i].glyph, x, 280, atlas_paint);
                std::ostringstream label;
                label << i;
                pinball::font::draw_text(surface, font, label.str(), x, 310, paint);
            }
        }
        pinball::scene::write_bmp(argv[2], surface);
        const std::string trace_path = std::string(argv[2]) + ".json";
        std::ofstream trace(trace_path.c_str());
        if (!trace) throw std::runtime_error("cannot open font preview trace");
        std::map<unsigned, std::size_t> histogram;
        for (std::size_t i = 0; i < font.glyphs().size(); ++i)
            for (std::size_t p = 0; p < font.glyphs()[i].indices.size(); ++p)
                ++histogram[font.glyphs()[i].indices[p]];
        trace << "{\n  \"kind\": \"font_asset_preview\",\n  \"style\": \"diagnostic white/gray index lookup; original runtime style unverified\",\n"
              << "  \"row_order\": \"PINBALL2.MID stored rows top-down; FONT.DAT bitmap rows decoded bottom-up\",\n"
              << "  \"gap\": " << font.gap() << ", \"unknown_header_value\": " << font.unknown_header_value()
              << ", \"height\": " << font.height() << ", \"glyph_count\": " << font.glyphs().size()
              << ",\n  \"grid\": {\"first_ascii\":32, \"columns\":16, \"rows\":6, \"cell_width\":38, \"cell_height\":30, \"origin\":[12,55]},\n"
              << "  \"index_counts\": {";
        for (std::map<unsigned, std::size_t>::const_iterator i = histogram.begin(); i != histogram.end(); ++i) {
            if (i != histogram.begin()) trace << ", ";
            trace << '"' << i->first << "\":" << i->second;
        }
        trace << "},\n  \"atlas_count\": " << atlas.glyphs.size()
              << ",\n  \"atlas_mapping\": \"generic ordinals; appearance resembles digits0..9; no explicit codepoint mapping metadata\",\n"
              << "  \"atlas_notices\": [";
        for (std::size_t i = 0; i < atlas.notices.size(); ++i) {
            if (i) trace << ", ";
            trace << quoted(atlas.notices[i]);
        }
        trace << "]\n}\n";
        trace.flush();
        if (!trace) throw std::runtime_error("font preview trace write failed");
        std::cout << "Rendered " << font.glyphs().size() << " raw indexed ASCII glyphs and " << atlas.glyphs.size()
                  << " DAT atlas ordinals with explicit diagnostic colors.\n"
                  << "Original runtime color/index style remains unverified. Stored font rows are shown top-down.\n"
                  << "Wrote " << argv[2] << " and " << trace_path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Font preview failed: " << error.what() << '\n';
        return 1;
    }
}
