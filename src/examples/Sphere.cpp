// Sphere.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "Sphere";
static const char* const HELP = "Generates a 3D sphere";

#include "DDImage/SourceGeo.h"
#include "DDImage/Scene.h"
#include "DDImage/Triangle.h"
#include "DDImage/Knobs.h"
#include "DDImage/Knob.h"
#include "DDImage/Channel3D.h"
#include <assert.h>

using namespace DD::Image;

class Sphere : public SourceGeo
{
private:
  double radius;
  int columns, rows;
  double my_u_extent, my_v_extent;
  bool close_top, close_bottom;
  // local matrix that Axis_Knob fills in
  Matrix4 _local;
  bool fix;
  Knob* _pAxisKnob;

protected:
  void _validate(bool for_real)
  {
    // Clamp the mesh size to reasonable numbers:
    columns = MIN(MAX(columns, 3), 200);
    rows    = MIN(MAX(rows,    3), 200);
    my_u_extent = clamp( my_u_extent, 0.001, 360.0 );
    my_v_extent = clamp( my_v_extent, 0.001, 180.0 );
    SourceGeo::_validate(for_real);
  }

public:
  static const Description description;
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }

  Sphere(Node* node) : SourceGeo(node)
  {
    radius = 1.0;
    rows = columns = 30;
    close_top = close_bottom = true;
    my_u_extent = 360.0;
    my_v_extent = 180.0;
    _local.makeIdentity();
    fix = false;
    _pAxisKnob = NULL;
  }

  void knobs(Knob_Callback f)
  {
    SourceGeo::knobs(f);
    Int_knob(f, &rows, "rows", "rows/columns");
    Int_knob(f, &columns, "columns", "");
    Double_knob(f, &radius, "radius");
    Double_knob(f, &my_u_extent, "u_extent", "u extent" );
    Double_knob(f, &my_v_extent, "v_extent", "v extent" );
    Newline(f);
    Bool_knob(f, &close_top, "close_top", "close top");
    Bool_knob(f, &close_bottom, "close_bottom", "close bottom");
    Obsolete_knob(f, "create_uvs", 0);
    Obsolete_knob(f, "create_normals", 0);

    Divider( f);
    // transform knobs
    _pAxisKnob = Axis_knob(f, &_local, "transform");

    if (_pAxisKnob != NULL) {
      if (GeoOp::selectable() == true)
        _pAxisKnob->enable();
      else
        _pAxisKnob->disable();
    }

    // This knob is set by knob_default so that all new instances execute
    // the "fix" code, which rotates the sphere 180 degrees so that the
    // seam is on the far side from the default camera position.
    Bool_knob(f, &fix, "fix", INVISIBLE);
  }

  /*! The will handle the knob changes.
   */
  int knob_changed(Knob* k)
  {
    if (k != NULL) {
      if (strcmp(k->name(), "selectable") == 0) {
        if (GeoOp::selectable() == true)
          _pAxisKnob->enable();
        else
          _pAxisKnob->disable();
        return 1;
      }
    }

    return SourceGeo::knob_changed(k);
  }

  // Hash up knobs that affect the Sphere:
  void get_geometry_hash()
  {
    SourceGeo::get_geometry_hash();   // Get all hashes up-to-date

    // Knobs that change the geometry structure:
    geo_hash[Group_Primitives].append(columns);
    geo_hash[Group_Primitives].append(rows);
    geo_hash[Group_Primitives].append(close_top);
    geo_hash[Group_Primitives].append(close_bottom);

    // Knobs that change the point locations:
    geo_hash[Group_Points].append(radius);
    geo_hash[Group_Points].append(columns);
    geo_hash[Group_Points].append(rows);
    geo_hash[Group_Points].append(close_top);
    geo_hash[Group_Points].append(close_bottom);

    // Knobs that change the vertex attributes:
    geo_hash[Group_Attributes].append(my_u_extent);
    geo_hash[Group_Attributes].append(my_v_extent);

    geo_hash[Group_Matrix].append(_local.a00);
    geo_hash[Group_Matrix].append(_local.a01);
    geo_hash[Group_Matrix].append(_local.a02);
    geo_hash[Group_Matrix].append(_local.a03);

    geo_hash[Group_Matrix].append(_local.a10);
    geo_hash[Group_Matrix].append(_local.a11);
    geo_hash[Group_Matrix].append(_local.a12);
    geo_hash[Group_Matrix].append(_local.a13);

    geo_hash[Group_Matrix].append(_local.a20);
    geo_hash[Group_Matrix].append(_local.a21);
    geo_hash[Group_Matrix].append(_local.a22);
    geo_hash[Group_Matrix].append(_local.a23);

    geo_hash[Group_Matrix].append(_local.a30);
    geo_hash[Group_Matrix].append(_local.a31);
    geo_hash[Group_Matrix].append(_local.a32);
    geo_hash[Group_Matrix].append(_local.a33);
  }

  // Apply the concat matrix to all the GeoInfos.
  void geometry_engine(Scene& scene, GeometryList& out)
  {
    SourceGeo::geometry_engine(scene, out);

    // multiply the node matrix
    for (unsigned i = 0; i < out.size(); i++)
      out[i].matrix = _local * out[i].matrix;
  }

  void create_geometry(Scene& scene, GeometryList& out)
  {
    int obj = 0;

    unsigned num_points = (close_bottom ? 1 : 0) + (rows - 1) * columns + (close_top ? 1 : 0);

    //=============================================================
    // Build the primitives:
    if (rebuild(Mask_Primitives)) {
      out.delete_objects();
      out.add_object(obj);

      // Create poly primitives:
      // Bottom endcap:
      if (close_bottom) {
        int j1 = 1;
        for (int i = 0; i < columns; i++) {
          int i0 = i % columns;
          int i1 = (i + 1) % columns;
          out.add_primitive(obj, new Triangle(0, i1 + j1, i0 + j1));
        }
      }

      // Create the center poly mesh:
      for (int j = 0; j < rows - 2; j++) {
        int j0 = j * columns + (close_bottom ? 1 : 0);
        int j1 = (j + 1) * columns + (close_bottom ? 1 : 0);
        for (int i = 0; i < columns; i++) {
          int i0 = i % columns;
          int i1 = (i + 1) % columns;
          // Create 2 triangles:
          out.add_primitive(obj, new Triangle(i0 + j0, i1 + j0, i0 + j1));
          out.add_primitive(obj, new Triangle(i0 + j1, i1 + j0, i1 + j1));
        }
      }

      // Top endcap:
      if (close_top) {
        int top_point = num_points - 1;
        int j0 = (close_bottom ? 1 : 0) + (rows - 2) * columns;
        for (int i = 0; i < columns; i++) {
          int i0 = i % columns;
          int i1 = (i + 1) % columns;
          out.add_primitive(obj, new Triangle(i0 + j0, i1 + j0, top_point));
        }
      }

      // Force points and attributes to update:
      set_rebuild(Mask_Points | Mask_Attributes);
    }

    //=============================================================
    // Create points and assign their coordinates:
    if (rebuild(Mask_Points)) {
      // Generate points:
      PointList* points = out.writable_points(obj);
      points->resize(num_points);

      // Assign the point locations:
      int p = 0;
      // Bottom center:
      if (close_bottom)
        (*points)[p++].set(0.0f, -radius, 0.0f);
      // Middle mesh:
      float drho = M_PI / rows;
      float dtheta = (2.0 * M_PI) / columns;
      float fix = this->fix ? -1 : 1;
      for (int j = 1; j < rows; j++) {
        float rho = j * drho;
        for (int i = 0; i < columns; i++) {
          float theta = i * dtheta;
          float x = fix * sinf(theta) * sinf(rho);
          float y = -cosf(rho);
          float z = fix * cosf(theta) * sinf(rho);
          (*points)[p].set(x * radius, y * radius, z * radius);
          ++p;
        }
      }
      // Top center:
      if (close_top)
        (*points)[p].set(0.0f, radius, 0.0f);
    }

    //=============================================================
    // Assign the normals and uvs:
    if (rebuild(Mask_Attributes)) {
      GeoInfo& info = out[obj];
      //---------------------------------------------
      // NORMALS:
      const Vector3* PNTS = info.point_array();
      Attribute* N = out.writable_attribute(obj, Group_Points, "N", NORMAL_ATTRIB);
      assert(N);
      for (unsigned p = 0; p < num_points; p++)
        N->normal(p) = PNTS[p] / radius;

      //---------------------------------------------
      // UVs:
      const Primitive** PRIMS = info.primitive_array();

      Attribute* uv = out.writable_attribute(obj, Group_Vertices, "uv", VECTOR4_ATTRIB);
      assert(uv);
      float ds = (360.0f / float(my_u_extent)) / float(columns); // U change per column
      float ss = 0.5f - (360.0f / float(my_u_extent)) / 2.0f;     // Starting U
      float dt = (180.0f / float(my_v_extent)) / float(rows);     // V change per row
      float st = 0.5 - (180.0 / my_v_extent) / 2.0;              // Starting V
      float s, t;                                             // Current UV
      t = st;
      // Bottom center:
      if (close_bottom) {
        s = ss;
        for (int i = 0; i < columns; i++) {
          unsigned v = (*PRIMS++)->vertex_offset();
          uv->vector4(v++).set(   s, 0.0f, 0.0f, 1.0f);
          uv->vector4(v++).set(s + ds, t + dt, 0.0f, 1.0f);
          uv->vector4(v++).set(   s, t + dt, 0.0f, 1.0f);
          s += ds;
        }
        t += dt;
      }

      // Create the poly mesh in center:
      for (int j = 0; j < rows - 2; j++) {
        s = ss;
        for (int i = 0; i < columns; i++) {
          unsigned v = (*PRIMS++)->vertex_offset();
          uv->vector4(v++).set(   s,    t, 0.0f, 1.0f);
          uv->vector4(v++).set(s + ds,    t, 0.0f, 1.0f);
          uv->vector4(v++).set(   s, t + dt, 0.0f, 1.0f);
          v = (*PRIMS++)->vertex_offset();
          uv->vector4(v++).set(   s, t + dt, 0.0f, 1.0f);
          uv->vector4(v++).set(s + ds,    t, 0.0f, 1.0f);
          uv->vector4(v++).set(s + ds, t + dt, 0.0f, 1.0f);
          s += ds;
        }
        t += dt;
      }

      // Top endcap:
      if (close_top) {
        s = ss;
        for (int i = 0; i < columns; i++) {
          unsigned v = (*PRIMS++)->vertex_offset();
          uv->vector4(v++).set(   s,    t, 0.0f, 1.0f);
          uv->vector4(v++).set(s + ds,    t, 0.0f, 1.0f);
          uv->vector4(v++).set(   s, 1.0f, 0.0f, 1.0f);
          s += ds;
        }
      }
    }
  }

  // virtual
  void build_handles(ViewerContext* ctx)
  {
    // call build_matrix_handle to multiply the context model matrix with the local matrix so the
    // nodes above it will display correctly
    build_matrix_handles(ctx, _local);
  }
};

static Op* build(Node* node) { return new Sphere(node); }
const Op::Description Sphere::description(CLASS, build);

// end of Sphere.C
