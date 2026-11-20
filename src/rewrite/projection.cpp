#include "projection.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace pinball {
namespace visual {
namespace {
void finite(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("nonfinite camera value");
}
void validate(const Camera& camera) {
    for (std::size_t i=0;i<camera.transform.size();++i) finite(camera.transform[i]);
    finite(camera.focal); finite(camera.center_x); finite(camera.center_y);
    finite(camera.depth_origin); finite(camera.depth_scale);
    if (camera.focal==0 || camera.depth_scale<=0)
        throw std::invalid_argument("camera focal/depth scale is invalid");
}
const std::vector<float>& record(const table::GroupInventory& group,
                                 float tag, std::size_t size) {
    const std::vector<float>* found=0;
    for (std::size_t i=0;i<group.float_records.size();++i) {
        const std::vector<float>& values=group.float_records[i].values;
        if (!values.empty() && values[0]==tag) {
            if (found || values.size()!=size)
                throw std::runtime_error("ambiguous or unsupported camera record");
            found=&values;
        }
    }
    if (!found) throw std::runtime_error("missing camera record");
    return *found;
}
}
Camera::Camera():transform(),focal(1),center_x(0),center_y(0),depth_origin(0),depth_scale(1) {
    transform[0]=transform[5]=transform[10]=1;
}
Camera camera_from_table(const table::TableModel& model) {
    const table::GroupInventory& info=model.group(model.require_named_group("camera_info"));
    if (info.float_records.size()!=1 || info.float_records[0].values.size()!=15)
        throw std::runtime_error("camera_info requires one 15-value record");
    const std::vector<float>& values=info.float_records[0].values;
    Camera result;
    for (std::size_t i=0;i<12;++i) result.transform[i]=values[i];
    result.focal=values[12]; result.depth_origin=values[13]; result.depth_scale=values[14];
    const std::vector<float>& center=record(model.group(model.require_named_group("table")),700,3);
    result.center_x=center[1]; result.center_y=center[2];
    validate(result);
    return result;
}
ProjectedPoint project(const Camera& camera,double x,double y,double z) {
    validate(camera); finite(x);finite(y);finite(z);
    const std::array<double,12>& m=camera.transform;
    const double a=m[0]*x+m[1]*y+m[2]*z+m[3];
    const double b=m[4]*x+m[5]*y+m[6]*z+m[7];
    const double c=m[8]*x+m[9]*y+m[10]*z+m[11];
    finite(a);finite(b);finite(c);
    ProjectedPoint result={0,0,0,0,c>0};
    if (!result.in_front) return result;
    result.x=camera.center_x+camera.focal*(a/c);
    result.y=camera.center_y+camera.focal*(b/c);
    // Scale before squaring to avoid overflow for large but finite positions.
    const double scale=(std::fmax)(std::fabs(a),(std::fmax)(std::fabs(b),std::fabs(c)));
    result.distance=scale*std::sqrt((a/scale)*(a/scale)+(b/scale)*(b/scale)+(c/scale)*(c/scale));
    result.depth=(result.distance-camera.depth_origin)*camera.depth_scale;
    finite(result.x);finite(result.y);finite(result.distance);finite(result.depth);
    return result;
}
bool unproject_plane(const Camera& camera,double x,double y,double z,
                     double& world_x,double& world_y) {
    validate(camera);finite(x);finite(y);finite(z);
    const double u=(x-camera.center_x)/camera.focal;
    const double v=(y-camera.center_y)/camera.focal;
    const std::array<double,12>& m=camera.transform;
    const double a=m[0]-u*m[8], b=m[1]-u*m[9];
    const double c=m[4]-v*m[8], d=m[5]-v*m[9];
    const double e=u*(m[10]*z+m[11])-(m[2]*z+m[3]);
    const double f=v*(m[10]*z+m[11])-(m[6]*z+m[7]);
    finite(a);finite(b);finite(c);finite(d);finite(e);finite(f);
    const double scale=(std::fmax)((std::fmax)(std::fabs(a),std::fabs(b)),
                                  (std::fmax)(std::fabs(c),std::fabs(d)));
    if(scale==0)return false;
    const double an=a/scale,bn=b/scale,cn=c/scale,dn=d/scale;
    const double en=e/scale,fn=f/scale;
    const double determinant=an*dn-bn*cn;
    if (std::fabs(determinant)<=std::numeric_limits<double>::epsilon()*16) return false;
    const double wx=(en*dn-bn*fn)/determinant, wy=(an*fn-en*cn)/determinant;
    finite(wx);finite(wy);
    if (!project(camera,wx,wy,z).in_front) return false;
    world_x=wx;world_y=wy;
    return true;
}
}
}
