#include "scene.hpp"
#include "table_model.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
std::uint32_t dimension(const char* text)
{
    const std::string value(text);
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("canvas dimensions must be positive decimal integers");
    char* end = 0;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (*end || parsed == 0 || parsed > 16384UL)
        throw std::runtime_error("canvas dimension must be between 1 and 16384");
    return static_cast<std::uint32_t>(parsed);
}
}

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 5) {
        std::cerr << "Usage: pinball_scene_preview <PINBALL.DAT> <output.bmp> [width height]\n";
        return 2;
    }
    try {
        const pinball::table::TableModel model = pinball::table::TableModel::load(argv[1]);
        pinball::scene::SceneLayout layout;
        if (argc == 5) { layout.width = dimension(argv[3]); layout.height = dimension(argv[4]); }
        const pinball::scene::ResourceScene scene = pinball::scene::static_resource_scene(model, layout);
        pinball::scene::write_bmp(argv[2], scene.surface);
        const std::string trace = std::string(argv[2]) + ".json";
        pinball::scene::write_scene_trace(trace, scene);
        std::cout << "Static resource scene: " << scene.surface.width << 'x' << scene.surface.height
                  << ", " << scene.instances.size() << " physical objects, " << scene.sprites.size()
                  << " decoded state images.\n"
                  << "Wrote " << argv[2] << " and transformation trace " << trace << '\n';
        for (std::size_t i = 0; i < scene.limitations.size(); ++i)
            std::cout << scene.limitations[i] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene preview failed: " << error.what() << '\n';
        return 1;
    }
}
