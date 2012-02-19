// IDistort.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

/*! \class IDistort IDistort.C

   This class implements a plug-in to the DDImage library that warps one image
   based on an image of u, v vector coordinates. Each pixel in the destination
   image has a vector [u, v] associated with it. This vector tells the image
   where to pull it's pixel from the input image.

   This class started out as the VectorBlur class.

   \author Doug Roble
   \date May 2nd, 2001  File creation.
 */

// Standard plug-in include files.

#include <stdio.h>
#include "DDImage/Iop.h"
#include "DDImage/NukeWrapper.h"
#include "DDImage/Row.h"
#include "DDImage/Tile.h"
#include "DDImage/Pixel.h"
#include "DDImage/Filter.h"
#include "DDImage/Knobs.h"
#include "DDImage/Vector2.h"
#include "DDImage/DDMath.h"

using namespace DD::Image;

// This is the name that NUKE will use to store this operator in the
// scripts. So that NUKE can locate the plugin, this must also be the
// name of the compiled plugin (with .so/.dll/.dylib added to the end):

static const char* const CLASS = "IDistort";

// This text will be displayed in a popup help box on the node's panel:
static const char* const HELP =
  "IDistort: Moves pixels around in an image.\n"
  "------------------------------------------\n"
  "IDistort uses two channels to figure out "
  "where each pixel in the resulting image should "
  "come from in the input channels.\n"
  "Use the Copy operator to merge the two distortion "
  "channels in with your image channels and select "
  "the two channels in the U and V selection boxes.\n"
  "Remember, the U and V values are offsets for "
  "where a pixel will come from. So if pixel 51,23 has a "
  "U and V value of -1, 5, the pixel's value will come "
  "from 50,28 of the input channels.";

// Definition of the new operator class.

class IDistort : public Iop
{
  Channel uv[2];
  double offset;
  double u_scale, v_scale;
  Channel blur_channel;
  double blur_xscale, blur_yscale;
  Channel alpha_channel;
  bool invert_alpha;
  bool premultiplied;
  Filter filter;

public:

  IDistort (Node* node) : Iop (node)
  {
    uv[0] = uv[1] = Chan_Black;
    offset = 0;
    u_scale = v_scale = 1;
    blur_channel = Chan_Black;
    blur_xscale = blur_yscale = 1;
    alpha_channel = Chan_Black;
    invert_alpha = false;
    premultiplied = false;
  }

  ~IDistort () { }

  void _validate(bool)
  {
    filter.initialize();
    copy_info();
  }

  void in_channels(int, ChannelSet& m) const
  {
    m += (uv[0]);
    m += (uv[1]);
    m += (blur_channel);
    m += (alpha_channel);
  }

  // Requests the *ENTIRE* bounding box. This may use a lot of memory!
  void _request(int x, int y, int r, int t, ChannelMask channels, int count)
  {
    ChannelSet c1(channels);
    in_channels(0, c1);
    input0().request(input0().info().x(),
                     input0().info().y(),
                     input0().info().r(),
                     input0().info().t(),
                     c1,
                     count * 2);
  }

  void engine ( int y, int x, int r, ChannelMask channels, Row& out );

  virtual void knobs ( Knob_Callback f )
  {
    Input_Channel_knob ( f, uv, 2, 0, "uv", "UV channels");
    Tooltip(f, "The values in these channels are added to the pixel "
               "coordinate to get the source pixel.");
    Double_knob(f, &offset, IRange(0, 1), "uv_offset", "UV offset");
    Tooltip(f, "This is subtracted from the uv channels, to set "
               "a non-zero center point for renderers that cannot output "
               "negative numbers.");
    WH_knob(f, &u_scale, "uv_scale", "UV scale");
    Tooltip(f, "Multiply the uv channels by this");
    Input_Channel_knob(f, &blur_channel, 1, 0, "blur", "blur channel");
    Tooltip(f, "Values in this channel are added to the size of the "
               "area to sample, to add extra blur or diffusion to the "
               "distortion.");
    WH_knob(f, &blur_xscale, "blur_scale", "blur scale");
    Tooltip(f, "Multiply the blur values by this");
    Input_Channel_knob(f, &alpha_channel, 1, 0, "maskChannel", "mask channel");
    Obsolete_knob(f, "alpha", "knob maskChannel $value");
    Obsolete_knob(f, "mask", "knob maskChannel $value");
    Tooltip(f, "Areas where the mask channel are black will not be changed.");
    Bool_knob(f, &invert_alpha, "invert_mask", "invert");
    Tooltip(f, "Invert the mask so white areas are not changed.");
    Bool_knob(f, &premultiplied, "premultiplied");
    Tooltip(f, "Check this if the uv and blur channels have been premultiplied"
               " by the alpha channel, such as when output by a renderer.");
    filter.knobs(f);
  }

  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description description;
};

static Iop* IDistortCreate(Node* node)
{
  return (new NukeWrapper (new IDistort(node)))->noMix()->noMask();
}
const Iop::Description IDistort::description ( CLASS, 0, IDistortCreate );

/*! For each line in the area passed to request(), this will be called. It must
   calculate the image data for a region at vertical position y, and between
   horizontal positions x and r, and write it to the passed row
   structure. Usually this works by asking the input for data, and modifying
   it.

   In the IDistort class, this implementation of the engine method examines
   each pixel. The vector associated with each pixel tells the pixel where to
   get it's input. Unfortunately, that is not enough - what if the pixel is
   really sampling a much larger area?

   So, for each pixel we look at the neighboring pixel's vectors too. From
   this, we calculate the vectors at the pixel's corners! Now, using these
   vectors, we form a polygon that will be used to sample the input image. The
   pixels under the polygon are filtered/averaged to produce the new pixel
   value. One assumption - the polygons should be proper polygons - no
   intersecting edges please.
 */
void IDistort::engine ( int y, int x, int r, ChannelMask channels, Row& out )
{
  // Because we don't change the bounding box, we will always be asked
  // for areas inside it. But we do need a pixel above and to the right,
  // and these may be outside the bounding box. These are done by
  // clamping the x+1 and y+1 coordinates to the input bbox below.
  ChannelSet c1(channels);
  in_channels(0, c1);
  Tile tile(input0(), x, y, r + 1, y + 2, c1);
  if (aborted())
    return;
  // missing channels will crash, use black instead:
  Channel uu = uv[0];
  Channel vv = uv[1];
  if (!intersect(tile.channels(), uu))
    uu = Chan_Black;
  if (!intersect(tile.channels(), vv))
    vv = Chan_Black;

  // Get pointers to the various channels:
  const float* const U0 = tile[uu][y];
  const float* const V0 = tile[vv][y];
  const float* const U1 = tile[uu][tile.clampy(y + 1)];
  const float* const V1 = tile[vv][tile.clampy(y + 1)];
  const float* const Um1 = tile[uu][tile.clampy(y - 1)];
  const float* const Vm1 = tile[vv][tile.clampy(y - 1)];

  const float* blur  = blur_channel  ? tile[blur_channel][y] : 0;
  const float* alpha = alpha_channel ? tile[alpha_channel][y] : 0;

  const bool invert_alpha = this->invert_alpha;

  // Copy all the doubles to local floats so optimizer can assumme
  // they are not changed:
  const float offset = float(this->offset);
  const float u_scale = float(this->u_scale);
  const float v_scale = float(this->v_scale);
  const float blur_xscale = float(this->blur_xscale);
  const float blur_yscale = float(this->blur_yscale);

  foreach(z, channels) out.writable(z);
  InterestRatchet interestRatchet;
  Pixel pixel(channels);
  pixel.setInterestRatchet(&interestRatchet);

  if (alpha && this->premultiplied) {
    for (; x < r; x++) {
      if (aborted())
        break;

      Vector2 center;
      float a = alpha[x];

      if (invert_alpha)
        a = 1 - a;

      if (a <= 0 || !offset) {
        // this will introduce distortion in the black areas so that the
        // user can tell if they incorrectly turned on premultiplied:

        input0().sample(U0[x] * u_scale + x + .5f, V0[x] * v_scale + y + .5f, 1, 1, pixel);

        foreach (z, channels)
          ((float*)(out[z]))[x] = pixel[z];

        continue;
      }
      else if (a < 1) {
        center.x = (U0[x] / a - offset) * u_scale * a + x + .5f;
        center.y = (V0[x] / a - offset) * v_scale * a + y + .5f;
      }
      else {
        center.x = (U0[x] - offset) * u_scale + x + .5f;
        center.y = (V0[x] - offset) * v_scale + y + .5f;
      }

      int x1 = x + 1;
      if (x1 >= info_.r())
        x1 = info_.r() - 1;

      Vector2 du((U0[x1] - U0[x]) * u_scale + 1,
                 (V0[x1] - V0[x]) * v_scale);
      Vector2 dv((U1[x] - U0[x]) * u_scale,
                 (V1[x] - V0[x]) * v_scale + 1);

      if (blur) {
        du.x = fabsf(du.x) + blur[x] * blur_xscale;
        dv.y = fabsf(dv.y) + blur[x] * blur_yscale;
      }

      input0().sample(center, du, dv, &filter, pixel);

      foreach (z, channels)
        ((float*)(out[z]))[x] = pixel[z];
    }
  }
  else if ( alpha ) {
    for (; x < r; x++) {
      if ( aborted() )
        break;

      Vector2 center;
      float a = alpha[x];

      if ( invert_alpha )
        a = 1 - a;

      if ( a <= 0 ) {
        // If the alpha is zero (or less) simply copy the color without
        // using any offset.

        input0().sample(x + .5f, y + .5f, 1, 1, pixel);

        foreach (z, channels)
          ((float*)(out[z]))[x] = pixel[z];
        continue;
      }

      // Use the alpha ( and make sure to clamp it ) to scale the vectors?
      // This seems odd, Doug 12/8/2006

      if ( a > 1 )
        a = 1;

      center.x = ( U0[x] - offset ) * u_scale * a + x + .5f;
      center.y = ( V0[x] - offset ) * v_scale * a + y + .5f;

      int x1 = x + 1;
      if ( x1 >= info_.r() )
        x1 = info_.r() - 1;

      Vector2 du ( ( U0[x1] - U0[x] ) * u_scale * a + 1,
                   ( V0[x1] - V0[x] ) * v_scale * a );
      Vector2 dv ( ( U1[x] - U0[x] ) * u_scale * a,
                   ( V1[x] - V0[x] ) * v_scale * a + 1 );

      if ( blur ) {
        du.x = fabsf(du.x) + blur[x] * blur_xscale * a;
        dv.y = fabsf(dv.y) + blur[x] * blur_yscale * a;
      }

      input0().sample ( center, du, dv, &filter, pixel );

      foreach ( z, channels )
        ((float*)(out[z]))[x] = pixel[z];
    }
  }
  else {
    for (; x < r; x++) {
      if ( aborted() )
        break;

      // Compute where the center of this pixel is displaced to. It's easy
      // just add the u,v vector to the pixel center's location.

      Vector2 center ( ( U0[x] - offset ) * u_scale + x + .5f,
                       ( V0[x] - offset ) * v_scale + y + .5f );

      // Compute the index of the neighboring pixels.

      int x1 = x + 1;

      if ( x1 >= info_.r() )
        x1 = info_.r() - 1;

      int xm1 = x - 1;

      if ( xm1 <= info_.x() )
        xm1 = info_.x() + 1;

      // Now compute both the "gradients" of the displacement vectors on
      // either side of the pixel.

      Vector2 du ( ( U0[x1] - U0[x] ) * u_scale + 1,
                   ( V0[x1] - V0[x] ) * v_scale );

      Vector2 dum1 ( ( U0[x] - U0[xm1] ) * u_scale + 1,
                     ( V0[x] - V0[xm1] ) * v_scale + 1 );

      Vector2 dv ( ( U1[x] - U0[x] ) * u_scale,
                   ( V1[x] - V0[x] ) * v_scale + 1 );

      Vector2 dvm1 ( ( U0[x] - Um1[x] ) * u_scale,
                     ( V0[x] - Vm1[x] ) * v_scale + 1 );

      // To guard against discontinuities in the vector field, use the
      // gradients with the smaller length.

      if ( dum1.lengthSquared() < du.lengthSquared() )
        du = dum1;

      if ( dvm1.lengthSquared() < dv.lengthSquared() )
        dv = dvm1;

      if ( blur ) {
        du.x = fabsf(du.x) + blur[x] * blur_xscale;
        dv.y = fabsf(dv.y) + blur[x] * blur_yscale;
      }

      input0().sample ( center, du, dv, &filter, pixel );

      foreach (z, channels)
        ((float*)(out[z]))[x] = pixel[z];
    }
  }
}
