// Saturation.C

// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.
// Permission is granted to reuse portions or all of this code for the
// purpose of implementing Nuke plugins, or to demonstrate or document
// the methods needed to implemente Nuke plugins.

static const char* const HELP =
  "This Iop changes the saturation (color intensity) of the incoming "
  "image data. If 'saturation' is set to 0, the resulting image will be "
  "gray only (R=G=B).\n"
  "Also look at HueShift, which does arbitrary 3x3 transformations of "
  "color space with not much more calculations than this uses.";

// We will derive from PixelIop, as this is an operator that changes
// single input pixels into single output pixels, with no crosstalk
// between them:
#include "DDImage/PixelIop.h"

// This class gives us access to a single row of image data:
#include "DDImage/Row.h"

// Here we have our UI elements:
#include "DDImage/Knobs.h"

// The Nuke Wrapper gives us masks and channel selectors:
#include "DDImage/NukeWrapper.h"

// and of couse the cross platform math:
#include "DDImage/DDMath.h"

using namespace DD::Image;

// SaturationIop is derived from PixelIop. PixelIops must implement
// the pixel_engine as their engine call, as well as validate.

class SaturationIop : public PixelIop
{

  // this is where the knobs store the user selected saturation:
  double saturation;

  // and user-selected conversion mode:
  int mode;

public:

  // initialize all members
  SaturationIop(Node* node) : PixelIop(node)
  {
    saturation = 1.0;
    mode = 0;
  }

  // This tells the PixelIop what channels to get to calculate a given
  // set of output channels. In order to calculate any color channel, it
  // needs all the color channels, because red (for instance) depends on
  // the green and blue.
  void in_channels(int input_number, ChannelSet& channels) const
  {
    // Must turn on the other color channels if any color channels are requested:
    ChannelSet done;
    foreach (z, channels) {
      if (colourIndex(z) < 3) { // it is red, green, or blue
        if (!(done & z)) { // save some time if we already turned this on
          done.addBrothers(z, 3); // add all three to the "done" set
        }
      }
    }
    channels += done; // add the colors to the channels we need
  }

  // user interface
  void knobs(Knob_Callback f);

  // Set the output channels and then call the base class validate.
  // If saturation is 1, the image won't change. By saying there are no
  // changed channels, Nuke's caching will completely skip this operator,
  // saving time. Also the GUI indicator will turn off, which (I hope)
  // is useful and informative...
  void _validate(bool for_real)
  {
    if (saturation != 1)
      set_out_channels(Mask_All);
    else
      set_out_channels(Mask_None);
    PixelIop::_validate(for_real);
  }

  // work horse
  void pixel_engine(const Row& in, int y, int x, int r, ChannelMask channels, Row& out);

  // The constructor for this object tells Nuke about it's existence.
  // Making this a class member and not just a static variable may be
  // necessary for some systems to call the constructor when the plugin
  // is loaded, though this appears to not be necessary for Linux or Windows.
  static const Iop::Description d;

  // let Nuke know the command name used to create this op:
  const char* Class() const { return d.name; }

  // Provide something for the [?] button in the control panel:
  const char* node_help() const { return HELP; }
};

// Create a new Saturation Iop. This implementation adds a Nuke Wrapper around
// the PixelIop. The Wrapper manages mask and channels selection for us, so we
// can concentrate on the Saturation only.
// The channels of the NukeWrapper are preset to only be rgb. By default it will
// use all channels, which is not what is wanted for a color operation like saturation:

static Iop* build(Node* node)
{
  return (new NukeWrapper(new SaturationIop(node)))->channels(Mask_RGB);
}

// The Description class gives Nuke access to information about the
// plugin.  The first element is the name under which the plugin will
// be known. The second argument is the recomended position in the
// main pulldown menu (this is ignored in modern versions of Nuke, you
// must add "menu" commands to your menu.tcl file to see the
// command). The third argument is the function Nuke calls to create
// the new Iop.

const Iop::Description SaturationIop::d("Saturation", "Color/Saturation", build);

enum {
  REC709 = 0, CCIR601, AVERAGE, MAXIMUM
};

static const char* mode_names[] = {
  "Rec 709", "Ccir 601", "Average", "Maximum", 0
};

// Create and manage additional UI elemnts in the Node panel. The
// NukeWrapper will add the mask selector, and Nuke will add the usual
// Node UI elements such as the name:

void SaturationIop::knobs(Knob_Callback f)
{

  // this knob provides a single slider to modify a value of type double.
  // 'f' must be the same as in the original call
  // '&saturation' points to the storage of the actual value. This value is
  //   only valid during engine calls!
  // 'IRange' detemines the slider range, in this case from 0 to 4 inclusive
  // "saturation" is not only the label on the slider in the UI, but also the
  //   name of the value when saving Nuke scripts.
  Double_knob(f, &saturation, IRange(0, 4), "saturation");

  // generate a pulldown choice for the grayscale conversion mode
  Enumeration_knob(f, &mode, mode_names, "mode", "luminance math");
}

// These helper functions convert RGB into Luminance:

static inline float y_convert_rec709(float r, float g, float b)
{
  return r * 0.2125f + g * 0.7154f + b * 0.0721f;
}

static inline float y_convert_ccir601(float r, float g, float b)
{
  return r * 0.299f + g * 0.587f + b * 0.114f;
}

static inline float y_convert_avg(float r, float g, float b)
{
  return (r + g + b) / 3.0f;
}

static inline float y_convert_max(float r, float g, float b)
{
  if (g > r)
    r = g;
  if (b > r)
    r = b;
  return r;
}

// Now this is where we actually modify pixel data.
//
// Warning: This function may be called by many different threads at
// the same time for different lines in the image. Do not modify any
// non local variable!  Lines will be called up in a random order at
// random times!

void SaturationIop::pixel_engine(const Row& in, int y, int x, int r,
                                 ChannelMask channels, Row& out)
{
  ChannelSet done;
  foreach (z, channels) { // visit every channel asked for
    if (done & z)
      continue;             // skip if we did it as a side-effect of another channel

    // If the channel is not a color, we return it unchanged:
    if (colourIndex(z) >= 3) {
      out.copy(in, z, x, r);
      continue;
    }

    // Find the rgb channels that belong to the set this channel is in.
    // Add them all to "done" so we don't run them a second time:
    Channel rchan = brother(z, 0);
    done += rchan;
    Channel gchan = brother(z, 1);
    done += gchan;
    Channel bchan = brother(z, 2);
    done += bchan;

    // pixel_engine is called with the channels indicated by in_channels()
    // already filled in. So we can just read them here:
    const float* rIn = in[rchan] + x;
    const float* gIn = in[gchan] + x;
    const float* bIn = in[bchan] + x;

    // We want to write into the channels. This is done with a different
    // call that returns a non-const float* pointer. We must call this
    // *after* getting the in pointers into local variables. This is
    // because in and out may be the same row structure, and calling
    // these may change the pointers from const buffers (such as a cache
    // line) to allocated writable buffers:
    float* rOut = out.writable(rchan) + x;
    float* gOut = out.writable(gchan) + x;
    float* bOut = out.writable(bchan) + x;

    // Pointer to when the loop is done:
    const float* END = rIn + (r - x);

    if (!saturation) {
      // do the zero case faster:
      switch (mode) {
        case REC709:
          while (rIn < END)
            *rOut++ = *gOut++ = *bOut++ = y_convert_rec709(*rIn++, *gIn++, *bIn++);
          break;
        case CCIR601:
          while (rIn < END)
            *rOut++ = *gOut++ = *bOut++ = y_convert_ccir601(*rIn++, *gIn++, *bIn++);
          break;
        case AVERAGE:
          while (rIn < END)
            *rOut++ = *gOut++ = *bOut++ = y_convert_avg(*rIn++, *gIn++, *bIn++);
          break;
        case MAXIMUM:
          while (rIn < END)
            *rOut++ = *gOut++ = *bOut++ = y_convert_max(*rIn++, *gIn++, *bIn++);
          break;
      }
    }
    else {
      // saturation is non-zero, thus we must interpolate it:
      float y;
      float fSaturation = float(saturation);
      switch (mode) {
        case REC709:
          while (rIn < END) {
            y = y_convert_rec709(*rIn, *gIn, *bIn);
            *rOut++ = lerp(y, *rIn++, fSaturation);
            *gOut++ = lerp(y, *gIn++, fSaturation);
            *bOut++ = lerp(y, *bIn++, fSaturation);
          }
          break;
        case CCIR601:
          while (rIn < END) {
            y = y_convert_ccir601(*rIn, *gIn, *bIn);
            *rOut++ = lerp(y, *rIn++, fSaturation);
            *gOut++ = lerp(y, *gIn++, fSaturation);
            *bOut++ = lerp(y, *bIn++, fSaturation);
          }
          break;
        case AVERAGE:
          while (rIn < END) {
            y = y_convert_avg(*rIn, *gIn, *bIn);
            *rOut++ = lerp(y, *rIn++, fSaturation);
            *gOut++ = lerp(y, *gIn++, fSaturation);
            *bOut++ = lerp(y, *bIn++, fSaturation);
          }
          break;
        case MAXIMUM:
          while (rIn < END) {
            y = y_convert_max(*rIn, *gIn, *bIn);
            *rOut++ = lerp(y, *rIn++, fSaturation);
            *gOut++ = lerp(y, *gIn++, fSaturation);
            *bOut++ = lerp(y, *bIn++, fSaturation);
          }
          break;
      }
    }
  }
}

// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.
