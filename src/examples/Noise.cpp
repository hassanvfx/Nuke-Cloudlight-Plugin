// Noise.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "Noise";
static const char* const HELP =
  "Draw various types of noise into the image, all based on the Perlin noise function.";

#include <stdio.h>
#include "DDImage/DrawIop.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"
#include "DDImage/DDMath.h"
#include "DDImage/noise.h"
#include "DDImage/Vector3.h"
#include "DDImage/Matrix4.h"

using namespace DD::Image;

enum { FBM, TURBULENCE };
const char* const types[] = {
  "fBm", "turbulence", 0
};

class Noise : public DrawIop
{
  int type;
  double xsize, ysize, zsize;
  int octaves;
  int real_octaves;
  bool nyquist;
  double lacunarity;
  double gain;
  float gamma;
  double rotx, roty;
  Matrix4 matrix;
  Matrix4 invmatrix;
  bool uniform;
public:
  Noise(Node* node) : DrawIop(node)
  {
    xsize = ysize = 350.0;
    zsize = 0.0;
    type = FBM;
    octaves = 10;
    nyquist = true;
    lacunarity = 2;
    gain = .5;
    gamma = .5;
    rotx = roty = 30;
    matrix.makeIdentity();
  }

  virtual void knobs(Knob_Callback f)
  {
    input_knobs(f);
    Enumeration_knob(f, &type, types, "type");
    Tooltip(f, "Noise type. Currently supported are <i>fBm</i> (Fractional "
               "Brownian Motion) and <i>turbulence</i>, which is similar to "
               "<i>fBm</i>, but based on absolute noise values.");
    Scale_knob(f, &xsize, IRange(1, 1000), "size", "x/ysize");
    Tooltip(f, "Lowest noise frequency");
    Float_knob(f, &zsize, IRange(0, 5), "zoffset", "z");
    Tooltip(f, "This knob must be animated if you want the noise to change "
               "over time. The expression '<i>frame/10</i>' will make it "
               "change completely in 10 frames.");
    Obsolete_knob(f, "Speed", "knob zoffset \"\\{frame/$value}\"");
    Int_knob(f, &octaves, IRange(1, 10), "octaves");
    Tooltip(f, "Number of Perlin noise functions to add");
    Obsolete_knob(f, "Octaves", "knob octaves $value");
    Bool_knob(f, &nyquist, "nyquist", "clip at Nyquist limit");
    Tooltip(f, "Limit the number of octaves so the highest frequency is "
               "larger than one pixel. You may need to turn this off if "
               "animating the size of the noise as the changes can be visible.");
    Double_knob(f, &lacunarity, IRange(1, 10), "lacunarity");
    Tooltip(f, "Each octave multiplies frequency by this amount");
    // this was here first...
    Obsolete_knob(f, "Lacunarity", "knob lacunarity $value");
    // ... then this added to correct that correction
    Obsolete_knob(f, "lucanarity", "knob lacunarity $value");
    Double_knob(f, &gain, IRange(.1, 1), "gain");
    Tooltip(f, "Each octave multiplies amplitude by this amount");
    Obsolete_knob(f, "Gain", "knob gain $value");
    Float_knob(f, &gamma, "gamma");

    Tab_knob(f, 0, "Transform");
    Transform2d_knob(f, &matrix, "transform", TO_PROXY);
    Float_knob(f, &rotx, IRange(0, 90), "xrotate");
    Tooltip(f, "Rotation about X axis in 3D noise space. Setting this to "
               "zero will reveal artifacts in the Perlin noise generator.");
    Float_knob(f, &roty, IRange(0, 90), "yrotate");
    Tooltip(f, "Rotation about Y axis in 3D noise space. Setting this to "
               "zero will reveal artifacts in the Perlin noise generator.");

    Obsolete_knob(f, "X Size", "knob size.w $value");
    Obsolete_knob(f, "Y Size", "knob size.h $value");
    Obsolete_knob(f, "offset", "knob translate $value");
    Obsolete_knob(f, "X Offset", "knob translate.x $value");
    Obsolete_knob(f, "Y Offset", "knob translate.y $value");

    output_knobs(f);
  }

  void _validate(bool for_real)
  {
    DrawIop::_validate(for_real);
    Matrix4 m = matrix;
    m.scale(xsize, ysize, 1);
    m.rotateY(radians(roty));
    m.rotateX(radians(rotx));
    uniform = false;
    real_octaves = octaves;
    float det = m.determinant();
    if (!det || octaves < 0) { uniform = true;
                               return; }
    invmatrix = m.inverse(det);
    if (fabs(lacunarity) > 1 && nyquist) {
      const Vector3& v1 = *(Vector3*)(invmatrix[0]); // xformed x axis
      const Vector3& v2 = *(Vector3*)(invmatrix[1]); // xformed y axis
      float size = MIN(v1.length(), v2.length());
      int o = int(ceil(-log(size * 2) / log(fabs(lacunarity)))) + 1;
      if (o < 1)
        o = 1;
      if (o < octaves)
        real_octaves = o;
      //printf("o = %d\n", o);
    }
  }

  bool draw_engine(int y, int ix, int r, float* buffer)
  {
    if (uniform) {
      float V = type == FBM ? 0.5f : 0.2f; // not sure what turbulence value is
      if (gamma <= 0.0001)
        V = 0;
      else
        V = powf(V, 1 / gamma);
      for (int x = ix; x < r; x++)
        buffer[x] = V;
      return true;
    }
    Vector3 a = invmatrix.transform(Vector3(ix, y, zsize));
    Vector3 b = invmatrix.transform(Vector3(r, y, zsize));
    Vector3 d = (b - a) / float(r - ix);
    int x = ix;
    switch (type) {
      case FBM:
        while (x < r) {
          Vector3 v = a + d * float(x - ix);
          buffer[x++] = float((fBm(v.x, v.y, v.z, real_octaves, lacunarity, gain) + 1) / 2);
        }
        break;
      case TURBULENCE:
        while (x < r) {
          Vector3 v = a + d * float(x - ix);
          buffer[x++] = float(turbulence(v.x, v.y, v.z, real_octaves, lacunarity, gain));
        }
        break;
    }
    if (gamma != 1) {
      if (gamma <= 0.0001) {
        for (x = ix; x < r; x++)
          buffer[x] = buffer[x] >= 1.0f;
      }
      else if (gamma == 0.5f) {
        for (x = ix; x < r; x++) {
          float v = buffer[x];
          if (v > 0)
            buffer[x] = v * v;
        }
      }
      else {
        float ig = 1 / gamma;
        for (x = ix; x < r; x++) {
          float v = buffer[x];
          if (v > 0)
            buffer[x] = powf(v, ig);
        }
      }
    }
    return true;
  }

  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;
};

static Iop* build(Node* node) { return new Noise(node); }
const Iop::Description Noise::d(CLASS, "Draw/Noise", build);
