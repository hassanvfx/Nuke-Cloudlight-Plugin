// LogGeo.C

/*
** Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.
*/

static const char* const CLASS = "LogGeo";
static const char* const HELP = "Move the XYZ of the points by raising the values to a power.";

#include "DDImage/ModifyGeo.h"
#include "DDImage/Scene.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"

using namespace DD::Image;

class LogGeo : public ModifyGeo
{
private:
  Vector3 log;
  bool swap;
  bool clamp_black;

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

  LogGeo(Node* node) : ModifyGeo(node)
  {
    log.x = log.y = log.z = 10.0;
    swap = false;
    clamp_black = false;
  }

  void knobs(Knob_Callback f)
  {
    ModifyGeo::knobs(f);
    XYZ_knob(f, &log.x, "log");
    Bool_knob(f, &swap, "swap");
    Bool_knob(f, &clamp_black, "clamp_black", "clamp black");
  }

  void get_geometry_hash()
  {
    // Get all hashes up-to-date
    ModifyGeo::get_geometry_hash();
    // Knobs that change the point locations:
    log.append(geo_hash[Group_Points]);
    geo_hash[Group_Points].append(swap);
    geo_hash[Group_Points].append(clamp_black);
  }

  void modify_geometry(int obj, Scene& scene, GeometryList& out)
  {
    PointList* points = out.writable_points(obj);
    const unsigned n = points->size();
    // Transform points:
    if (swap) {
      // POW
      for (unsigned i = 0; i < n; i++) {
        Vector3& v = (*points)[i];
        if (clamp_black) {
          if (v.x <= 0.0f)
            v.x = 0;
          else
            v.x = pow(v.x, log.x);
          if (v.y <= 0.0f)
            v.y = 0;
          else
            v.y = pow(v.y, log.y);
          if (v.z <= 0.0f)
            v.z = 0;
          else
            v.z = pow(v.z, log.z);
        }
        else {
          v.x = (v.x > 0.0f) ? pow(v.x, log.x) : -pow(-v.x, log.x);
          v.y = (v.y > 0.0f) ? pow(v.y, log.y) : -pow(-v.y, log.y);
          v.z = (v.z > 0.0f) ? pow(v.z, log.z) : -pow(-v.z, log.z);
        }
      }
    }
    else {
      // LOG
      for (unsigned i = 0; i < n; i++) {
        Vector3& v = (*points)[i];
        v.x = pow(log.x, v.x) - 1.0f;
        v.y = pow(log.y, v.y) - 1.0f;
        v.z = pow(log.z, v.z) - 1.0f;
      }
    }
  }
};

static Op* build(Node* node)
{
  return new LogGeo(node);
}
const Op::Description LogGeo::description(CLASS, build);

// end of LogGeo.C
