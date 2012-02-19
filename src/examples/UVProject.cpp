// UVProject.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/GeoOp.h"
#include "DDImage/Scene.h"
#include "DDImage/CameraOp.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"
#include "DDImage/Mesh.h"
#include "DDImage/Channel3D.h"

#include <assert.h>

using namespace DD::Image;

static const char* const CLASS = "UVProject";
static const char* const HELP = "Project uv's onto points and vertices.";

// Point/Vertex:
enum { POINT_LIST = 0, VERTEX_LIST };
static const char* const object_types[] = {
  "points", "vertices", 0
};

// Projection type:
enum {
  OFF = 0, PERSPECTIVE, PLANAR, SPHERICAL, CYLINDRICAL
};
static const char* const proj_types[] = {
  "off", "perspective", "planar", "spherical", "cylindrical", 0
};


class UVProject : public GeoOp
{
private:
  int projection;
  double u_scale, v_scale;
  bool u_invert, v_invert;
  int plane;
  Matrix4 xform;
  Matrix4 projectxform;
  const char* uv_attrib_name;
  float inv_u_scale, inv_v_scale;

protected:
  void _validate(bool for_real)
  {
    // Validate the inputs:
    input0()->validate(for_real);

    // Check if input 1 is connected and get the camera xform from it
    Op* op = Op::input(1);
    if (dynamic_cast<CameraOp*>(op)) {
      op->validate(for_real);
      CameraOp* cam = (CameraOp*)op;
      projectxform.translation(0.5f, 0.5f, 0.0f);
      projectxform.scale(0.5f, cam->film_width() / cam->film_height() * 0.5f, 1.0f);
      projectxform *= cam->projection();
      xform = cam->imatrix();
    }
    else if (dynamic_cast<AxisOp*>(op)) {
      op->validate(for_real);
      xform = ((AxisOp*)op)->imatrix();
      projectxform.makeIdentity();
    }
    else {
      xform.makeIdentity();
      projectxform.makeIdentity();
    }

    inv_u_scale = (float)(1.0 / u_scale);
    inv_v_scale = (float)(1.0 / v_scale);

    // Calculate the geometry hashes:
    GeoOp::_validate(for_real);
  }

public:
  static const Description description;
  const char* Class() const
  {
    return CLASS;
  }
  const char* node_help() const
  {
    return HELP;
  }

  UVProject(Node* node) : GeoOp(node)
  {
    projection = PERSPECTIVE;
    u_scale = v_scale = 1.0;
    u_invert = v_invert = false;
    plane = PLANE_XY;
    uv_attrib_name = "uv";
  }

  int minimum_inputs() const
  {
    return 2;
  }
  int maximum_inputs() const
  {
    return 2;
  }

  bool test_input(int input, Op* op) const
  {
    if (input == 1)
      return dynamic_cast<AxisOp*>(op) != 0;
    return GeoOp::test_input(input, op);
  }

  Op* default_input(int input) const
  {
    if (input == 1)
      return 0;
    return GeoOp::default_input(input);
  }

  const char* input_label(int input, char* buffer) const
  {
    switch (input) {
      case 0:
        return 0;
      case 1:
        return "axis/cam";
      default:
        return 0;
    }
  }

  void knobs(Knob_Callback f)
  {
    GeoOp::knobs(f);
    Enumeration_knob(f, &projection, proj_types, "projection", "projection");
    Obsolete_knob(f, "destination", 0);
    Enumeration_knob(f, &plane, plane_orientation_modes, "plane", "plane");
    Bool_knob(f, &u_invert, "u_invert", "invert u");
    Bool_knob(f, &v_invert, "v_invert", "invert v");
    Double_knob(f, &u_scale, IRange(0, 2), "u_scale", "u scale");
    Double_knob(f, &v_scale, IRange(0, 2), "v_scale", "v scale");
    String_knob(f, &uv_attrib_name, "uv_attrib_name", "attrib name");
  }

  int knob_changed(Knob* k)
  {
    knob("plane")->enable(projection > PERSPECTIVE);
    knob("u_scale")->enable(projection > PERSPECTIVE);
    knob("v_scale")->enable(projection > PERSPECTIVE);
    return 1;
  }

  /*! Hash up knobs that affect the primitive attributes. */
  void get_geometry_hash()
  {
    // Get all hashes up-to-date
    GeoOp::get_geometry_hash();
    if (projection == OFF)
      return;
    // Hash up knobs that affect the UV attributes
    Hash knob_hash;
    knob_hash.reset();
    // Take transform into account:
    xform.append(knob_hash);
    // Take projection matrix into consideration if perspective mode
    if (projection == PERSPECTIVE)
      projectxform.append(knob_hash);
    // Hash rest of local knobs:
    knob_hash.append(projection);
    knob_hash.append(plane);
    knob_hash.append(u_invert);
    knob_hash.append(v_invert);
    knob_hash.append(u_scale);
    knob_hash.append(v_scale);
    knob_hash.append(uv_attrib_name);
    // Take point hash into account to force uv upating when positions change
    knob_hash.append(geo_hash[Group_Points]);

    // Change the point or vertex attributes hash:
    geo_hash[Group_Attributes].append(knob_hash);
  }

  /*! Assign UV attribute to point or vertex attribute list. */
  void geometry_engine(Scene& scene, GeometryList& out)
  {
    input0()->get_geometry(scene, out);
    if (projection == OFF)
      return;

    // Call the engine on all the caches:
    for (unsigned i = 0; i < out.objects(); i++) {
      GeoInfo& info = out[i];

      Matrix4 m;
      if (info.matrix == Matrix4::identity())
        m = xform;
      else
        m = xform * info.matrix;

      // Remove UV vertex attribute, as this takes precedence over a point attribute
      info.delete_group_attribute(Group_Vertices, uv_attrib_name, VECTOR4_ATTRIB);
      // Create a point attribute
      Attribute* uv = out.writable_attribute(i, Group_Points, uv_attrib_name, VECTOR4_ATTRIB);
      assert(uv);

      // Project point location and save in UV attribute
      const Vector3* PNTS = info.point_array();
      for (unsigned i = 0; i < info.points(); i++)
        project_point(m.transform(*PNTS++), uv->vector4(i));
    }
  }

  void project_point(const Vector3& in, Vector4& out);
};

#define M_TWOPI M_PI * 2.0
#define DEG2RAD M_PI / 180.0
#define RAD2DEG 180.0 / M_PI

/*! Take the point location and project it back through the camera.
    Where it ends up in the camera aperture is the UV coordinate.
 */
void UVProject::project_point(const Vector3& in, Vector4& out)
{
  float a, b;
  switch (projection) {
    default:
    case PERSPECTIVE:
      out = projectxform.transform(in, 1);
      break;
    case PLANAR:
      switch (plane) {
        case PLANE_XY:
          a = in.x;
          b = in.y;
          break;
        case PLANE_YZ:
          a = in.z;
          b = in.y;
          break;
        case PLANE_ZX:
          a = in.x;
          b = in.z;
          break;
      }
      out.set(a * inv_u_scale + 0.5f, b * inv_v_scale + 0.5f, 0, 1);
      break;
    case SPHERICAL: {
      // latitude
      double phi = acos(-in.y);
      // longitude
      double theta = -atan2(-in.x, in.z);
      // Right side
      if (theta <= 0.0)
        theta += M_TWOPI;
      out.set((theta / M_TWOPI) * 0.25f * inv_u_scale, (phi / M_PI - 0.5) * inv_v_scale + 0.5f, 0, 1);
      break;
    }
    case CYLINDRICAL: {
      // longitude
      double theta = -atan2(-in.x, in.z);
      // Right side
      if (theta <= 0.0)
        theta += M_TWOPI;
      out.set((theta / M_TWOPI) * 0.25f * inv_u_scale, in.y * 0.5f * inv_v_scale + 0.5f, 0, 1);
      break;
    }
  }
  if (u_invert)
    out.x = out.w - out.x;
  if (v_invert)
    out.y = out.w - out.y;
}

static Op* build(Node* node)
{
  return new UVProject(node);
}
const Op::Description UVProject::description(CLASS, build);

// end of UVProject.C
