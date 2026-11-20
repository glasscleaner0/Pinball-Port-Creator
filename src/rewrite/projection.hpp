#ifndef PINBALL_REWRITE_PROJECTION_HPP
#define PINBALL_REWRITE_PROJECTION_HPP
#include "table_model.hpp"
#include <array>
namespace pinball {
namespace visual {
// Reconstruction from numeric camera/center records, not verified XP code.
// The final two camera values fit radial-distance depth in sampled floor data.
struct Camera {
    std::array<double,12> transform;
    double focal, center_x, center_y, depth_origin, depth_scale;
    Camera();
};
struct ProjectedPoint {
    double x, y, distance, depth;
    bool in_front;
};
Camera camera_from_table(const table::TableModel& model);
ProjectedPoint project(const Camera& camera, double x, double y, double z);
// Invert the perspective image on a caller-selected constant world-Z plane.
// Returns false for a parallel ray or a point behind the camera.
bool unproject_plane(const Camera& camera, double x, double y, double z,
                     double& world_x, double& world_y);
}
}
#endif
