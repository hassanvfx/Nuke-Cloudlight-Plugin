// cloudPhong.C

// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/DDWindows.h"
#include "DDImage/IllumShader.h"

#include "DDImage/VertexContext.h"
#include "DDImage/LightOp.h"
#include "DDImage/CameraOp.h"
#include "DDImage/ViewerContext.h"
#include "DDImage/Scene.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"
#include "DDImage/gl.h"

using namespace DD::Image;

static const char* const HELP = 
  "Shader that does cloudPhong mapping (interpolation of normals)"
  "The mapE, mapD, mapS inputs are used to modulate the emissive, diffuse, and specular components respectively.\n"
  "The mapSh input is used to modulate the shininess value. This is done by choosing the shininess channel and using "
  "the black and white values to map to the min shininess and max shininess parameters.";

enum {
  SHININESS_RED_CHAN = 0,
  SHININESS_GREEN_CHAN,
  SHININESS_BLUE_CHAN,
  SHININESS_ALPHA_CHAN,
  SHININESS_LUMINANCE_CHAN,
  SHININESS_AVERAGE_CHAN,
  SHININESS_MAX_ENUM
};

const char* const shininess_chan_choices[] = {
  "red", "green", "blue", "alpha", "luminance", "average rgb", 0
};

class cloudPhong : public IllumShader
{
private:
  Vector3 color_;
  Vector3 diffuse_;
  Vector3 specular_;
  Vector3 emission_;
Vector3 surfaceShader_;
    
  double minShininess_;
  double maxShininess_;
  int shininessChan_;

public:
  const char* node_help() const
  {
    return HELP;
  }
  static const Description description;
  const char* Class() const
  {
    return description.name;
  }

  cloudPhong(Node* node) : IllumShader(node)
  {
    for (int z = 0; z < 4; z++)
      channel[z] = Channel(z + Chan_Red);

    color_.set(1.0f, 1.0f, 1.0f);
    diffuse_.set(0.18f, 0.18f, 0.18f);
    specular_.set(0.8f, 0.8f, 0.8f);
    emission_.set(0.0f, 0.0f, 0.0f);
    surfaceShader_.set(1.0f, 1.0f, 1.0f);
      
    minShininess_ = 10.0;
    maxShininess_ = 10.0;
    shininessChan_ = SHININESS_LUMINANCE_CHAN;
  }

  int minimum_inputs() const
  {
    return 5;
  }
  int maximum_inputs() const
  {
    return 5;
  }
    
   

  /*! For input 0 it uses the default Material input0.
     Input 1 is allowed to be NULL so we can test whether it's connected.
   */
  /*virtual*/
  Op* default_input(int input) const
  {
    if (input == 0)
      return Material::default_input(input);
    return 0;
  }

  /*! Input 0 has no label, input 1 is 'map'. */
  /*virtual*/
  const char* input_label(int input, char* buffer) const
  {
    switch (input) {
      default: return "";
      case 1: return "mapD";
      case 2: return "mapE";
      case 3: return "mapS";
      case 4: return "mapSh";
    }
  }

  /*! Add surface channels to info.
   */
  /*virtual*/
  void _validate(bool for_real)
  {
    Material::_validate(for_real);

    // Build surface ChannelMask from channel selector:
    surface_channels = Mask_None;
    for (int i = 0; i < 4; i++)
      surface_channels += channel[i];

    info_.turn_on(surface_channels);

    // Validate the image input:
    if (input(1))
      input1().validate(for_real);
    if (input(2))
      input(2)->validate(for_real);
    if (input(3))
      input(3)->validate(for_real);
    if (input(4))
      input(4)->validate(for_real);
  }

  /*! Add surface channels to request.
   */
  /*virtual*/
  void _request(int x, int y, int r, int t, ChannelMask channels, int count)
  {
    ChannelSet c1(channels);
    c1 += surface_channels;
    Material::_request(x, y, r, t, c1, count);
    // Request RGB from map input:
    if (input(1)) {
      const Box& b = input1().info();
      input1().request(b.x(), b.y(), b.r(), b.t(), Mask_RGB, count);
    }
    if (input(2)) {
      const Box& b = input(2)->info();
      input(2)->request(b.x(), b.y(), b.r(), b.t(), Mask_RGB, count);
    }
    if (input(3)) {
      const Box& b = input(3)->info();
      input(3)->request(b.x(), b.y(), b.r(), b.t(), Mask_RGB, count);
    }
    if (input(4)) {
      const Box& b = input(4)->info();
      input(4)->request(b.x(), b.y(), b.r(), b.t(), Mask_RGBA, count);
    }
  }

  void knobs(Knob_Callback f)
  {
    IllumShader::knobs(f);
    Color_knob(f, &color_.x, IRange(0, 4), "color3");
    Color_knob(f, &emission_.x, IRange(0, 4), "emission");
    SetFlags(f, Knob::LOG_SLIDER);
    Color_knob(f, &diffuse_.x, IRange(0, 4), "diffuse");
    Color_knob(f, &specular_.x, IRange(0, 4), "specular");
    Double_knob(f, &minShininess_, IRange(2, 100), "min_shininess", "min shininess");
    Double_knob(f, &maxShininess_, IRange(2, 100), "max_shininess", "max shininess");
    Enumeration_knob(f, &shininessChan_, shininess_chan_choices, "shininess_channel", "shininess channel");
    Tooltip(f, "Select which channel to use to map to the min shininess and max shininess parameters.");

    Obsolete_knob(f, "shininess", "knob min_shininess $value");
    Obsolete_knob(f, "layer", "knob channels $value");
    Obsolete_knob(f, "ambient", "knob channels $value");
      
      
    Color_knob(f, &surfaceShader_.x, IRange(0, 4), "surfaceShader");
  }

  void surface_shader(Vector3& P, Vector3& V, Vector3& N,
                      const VertexContext& vtx, Pixel& surface)
  {
#ifdef USE_SURFACE_ANGLE_CHECK
    // Skip if angle is away from camera:
    if (N.dot(V) < 0.0f) {
      surface[channel[0]] = emission_.x;
      surface[channel[1]] = emission_.y;
      surface[channel[2]] = emission_.z;
      return;
    }
#endif

    float shininess, minShininess, maxShininess;
    minShininess = minShininess_;
    maxShininess = maxShininess_;
    if (minShininess_ > maxShininess_)
      maxShininess = minShininess_;

    // modulate the shininess with input 4
    if (input(4)) {
      {
        float svalue;
        Pixel mapShininess(Mask_RGBA);
        mapShininess.copyInterestRatchet(surface);
        vtx.sample(input(4), mapShininess);
        switch (shininessChan_) {
          case SHININESS_RED_CHAN:
          default:
            svalue = clamp(mapShininess[Chan_Red]);
            break;
          case SHININESS_GREEN_CHAN:
            svalue = clamp(mapShininess[Chan_Green]);
            break;
          case SHININESS_BLUE_CHAN:
            svalue = clamp(mapShininess[Chan_Blue]);
            break;
          case SHININESS_ALPHA_CHAN:
            svalue = clamp(mapShininess[Chan_Alpha]);
            break;
          case SHININESS_LUMINANCE_CHAN:
            svalue = clamp(mapShininess[Chan_Red] * 0.299 + mapShininess[Chan_Green] * 0.587 + mapShininess[Chan_Blue] * 0.114);
            break;
          case SHININESS_AVERAGE_CHAN:
            svalue = clamp((mapShininess[Chan_Red] + mapShininess[Chan_Green] + mapShininess[Chan_Blue]) / 3);
            break;
        }
        // map svalue to min/max shininess
        shininess = minShininess + svalue * (maxShininess - minShininess);
      }
    }
    // take the average
    else {
      shininess = (minShininess + maxShininess) * .5;
    }

    // Calculate each light's contribution:
    Pixel light_color(Mask_RGB); // Light's color
    light_color.copyInterestRatchet(surface);
    Vector3 Cd(0, 0, 0); // Accumulated diffuse
    Vector3 Ck(0, 0, 0); // Accumulated specular
    float D, shade, n_dot_l, r_dot_v;
    Vector3 L, R;
    const unsigned n = vtx.scene()->lights.size();
    for (unsigned i = 0; i < n; i++) {
      LightContext& ltx = *(vtx.scene()->lights[i]);
      ltx.light()->get_L_vector(ltx, P, N, L, D);
      shade = ltx.light()->get_shadowing(ltx, P);

      ltx.light()->get_color(ltx, P, N, L, D, light_color);
      const Vector3& Cl = (Vector3 &)light_color[Chan_Red];

      // Diffuse - only calc if light's a point source:
      if (ltx.light()->is_delta_light()) {
        n_dot_l = N.dot(-L);
        if (n_dot_l > 0.0f)
          Cd += Cl * n_dot_l * shade;
      }

      // Specular:
      R = L - N * (L.dot(N) * 2.0f);
#ifdef USE_FAST_NORMALIZE
      R.fast_normalize();
#else
      R.normalize();
#endif
      r_dot_v = R.dot(V);
      if (r_dot_v > 0.0f && r_dot_v < M_PI_2)
        Ck += Cl * (float)powf(r_dot_v, shininess) * shade;
    }

    // Weight the final diffuse color
    Cd = Cd * diffuse_;

    // Weight the final specular color:
    Ck = Ck * specular_;

    Pixel mapDiffuse(Mask_RGB);
    mapDiffuse.copyInterestRatchet(surface);
    if (input(1)) {
      vtx.sample(input(1), mapDiffuse);
    }
    else {
      mapDiffuse[Chan_Red] = 1.0f;
      mapDiffuse[Chan_Green] = 1.0f;
      mapDiffuse[Chan_Blue] = 1.0f;
    }

    Pixel mapEmission(Mask_RGB);
    mapEmission.copyInterestRatchet(surface);
    if (input(2)) {
      vtx.sample(input(2), mapEmission);
    }
    else {
      mapEmission[Chan_Red] = 1.0f;
      mapEmission[Chan_Green] = 1.0f;
      mapEmission[Chan_Blue] = 1.0f;
    }

    Pixel mapSpecular(Mask_RGB);
    mapSpecular.copyInterestRatchet(surface);
    if (input(3)) {
      vtx.sample(input(3), mapSpecular);
    }
    else {
      mapSpecular[Chan_Red] = 1.0f;
      mapSpecular[Chan_Green] = 1.0f;
      mapSpecular[Chan_Blue] = 1.0f;
    }
      
      //Take vertex color as foundation
      
    surface[channel[0]] = (vtx.r()*(500*surfaceShader_.x))*( mapEmission[Chan_Red] + surface[channel[0]] + Cd.x * color_.x * mapDiffuse[Chan_Red] + Ck.x * mapSpecular[Chan_Red] + vtx.ambient.x);
    surface[channel[1]] = (vtx.g()*(500*surfaceShader_.y))*( mapEmission[Chan_Green] + surface[channel[1]] + Cd.y * color_.y * mapDiffuse[Chan_Green] + Ck.y * mapSpecular[Chan_Green] + vtx.ambient.y);
    surface[channel[2]] = (vtx.b()*(500*surfaceShader_.z))*( mapEmission[Chan_Blue] + surface[channel[2]] + Cd.z * color_.z * mapDiffuse[Chan_Blue ] + Ck.z * mapSpecular[Chan_Blue] + vtx.ambient.z);
    surface[channel[3]] = 1.0f;
     
     
      
  }

  bool shade_GL(ViewerContext* ctx, GeoInfo& geo)
  {
    // Let input set itself up first
    input0().shade_GL(ctx, geo);
    // do input 1 if input 0 is not connected
    if (input(1) != 0 && input(0) == default_input(0))
      input1().shade_GL(ctx, geo);

    // Don't bother with any custom shading:
    if (ctx->lights().size() == 0)
      return true;

    // Specify the default material type:
    Vector4 tmp;
    tmp.set(diffuse_ * color_, 1);
    glMaterialfv(GL_FRONT, GL_DIFFUSE,  (GLfloat*)tmp.array());

    tmp.set(specular_, 1);
    glMaterialfv(GL_FRONT, GL_SPECULAR, (GLfloat*)tmp.array());

    tmp.set(emission_, 1);
    glMaterialfv(GL_FRONT, GL_EMISSION, (GLfloat*)tmp.array());

    // take the average of the min/max shininess for ogl
    float minShininess, maxShininess;
    minShininess = minShininess_;
    maxShininess = maxShininess_;
    if (minShininess_ > maxShininess_)
      maxShininess = minShininess_;
    glMaterialf(GL_FRONT, GL_SHININESS, float((minShininess + maxShininess) * 0.5));

    glGetErrors("cloudPhong shader");

    return true;
  }

  bool set_texturemap(ViewerContext* ctx, bool gl)
  {
    // use input 1 if input 0 is not connected
    if (input(1) != 0 && input(0) == default_input(0))
      return input1().set_texturemap(ctx, gl);
    else
      return input0().set_texturemap(ctx, gl);
  }

};

static Op* build(Node* node)
{
  return new cloudPhong(node);
}
const Op::Description cloudPhong::description("cloudPhong", build);

// end of cloudPhong.C
