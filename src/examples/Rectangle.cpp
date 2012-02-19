// Rectangle.C

// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.
// Permission is granted to reuse portions or all of this code for the
// purpose of implementing Nuke plugins, or to demonstrate or document
// the methods needed to implemente Nuke plugins.

static const char* const CLASS = "Rectangle";
static const char* const HELP =
  "Rectangle in a solid color, with antialiased edge if the coordinates "
  "are not integers.";

// this header file contains our base class 'DrawIop'
#include "DDImage/DrawIop.h"

// Knobs are the user interface elements that appear in the node panels
#include "DDImage/Knobs.h"

// this is a collection of math functions, and cross platform
// compatibility of math:
#include "DDImage/DDMath.h"

using namespace DD::Image;

// The Rectangle operator is derived from DD::Image::DrawIop. DrawIop
// is a base class for operations that draw a black and white image
// (such as a shapes and text).

class RectangleIop : public DrawIop
{
  // bounding box of the final rectangle
  double x, y, r, t;
  // softness of the rectangle in horizontal and vertical direction
  double soft_x, soft_y;
public:
  void _validate(bool);
  bool draw_engine(int y, int x, int r, float* buffer);
  // make sure that all members are initialised
  RectangleIop(Node* node) : DrawIop(node)
  {
    x = y = r = t = 0;
    soft_x = soft_y = 0;
  }
  virtual void knobs(Knob_Callback);

  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;
};

// The knobs function creates and maintains the user interface elementes in
// the Node panels and the interactive handles in the Nuke viewer:

void RectangleIop::knobs(Knob_Callback f)
{

  // allow DrawIop to add its own knobs to handle input
  input_knobs(f);

  // this knob provides controls for the position and size of our rectangle.
  // It also manages the rectangular handle box in all connected viewers.
  BBox_knob(f, &x, "area");

  // This knob manages user input for the rectangles edge softness
  WH_knob(f, &soft_x, "softness");

  // allow DrawIop to add its own knobs to handle output
  output_knobs(f);
}

// _validate (note the underscore!) in DrawIop's should set the
// desired drawing area as a bounding box. If there is nothing to draw
// (for example, the user requested a zero width rectangle), _validate
// should call disable(). As long as an Iop is disabled, Nuke will not
// perform any engine calls to this node.

void RectangleIop::_validate(bool for_real)
{
  // don't bother calling the engine in degenerate cases
  if (x >= r || y >= t) {
    set_out_channels(Mask_None);
    copy_info();
    return;
  }
  set_out_channels(Mask_All);
  // make sure that we get enough pixels to build our rectangle
  DrawIop::_validate(for_real,
                     int(floor(x)),
                     int(floor(y)),
                     int(ceil(r)),
                     int(ceil(t)));
}

// This is the finally the function that does some actual drawing.
//
// Warning: This function may be called by many different threads at
// the same time for different lines in the image. Do not modify any
// non local variable!  Lines will be called up in a random order at
// random times!
//
// This is the plugin's chance to put useful drawings into the pixel
// buffer.  All drawing is done in floating point space for a single
// component. Color may be added later by other operators in the
// graph.

bool RectangleIop::draw_engine(int Y, int X, int R, float* buffer)
{
  // lets see if there is anything to draw at all
  if (Y < (int)floor(y))
    return false;
  if (Y >= (int)ceil(t))
    return false;
  // calculate the vertical multiplier:
  float m = 1;
  if ( soft_y >= 0.0 ) {
    // if this line is within the softned falloff, change the multiplier
    if (Y < y + soft_y) {
      float T = (Y + 1 - y) / (soft_y + 1);
      if (T < 1)
        m *= (3 - 2 * T) * T * T;
    }
    // same for the 'upper' lines in the image (bottom left is 0,0)
    if (Y > t - soft_y - 1) {
      float T = (t - Y) / (soft_y + 1);
      if (T < 1)
        m *= (3 - 2 * T) * T * T;
    }
  }
  // now fill the line with data
  for (; X < R; X++) {
    float m1 = m;
    // first, calculate the multiplier for the left side falloff
    if (X + 1 <= x || X >= r)
      m1 = 0;
    else if (X < x + soft_x && soft_x >= 0) {
      float T = (X + 1 - x) / (soft_x + 1);
      if (T < 1)
        m1 *= (3 - 2 * T) * T * T;
    }
    // now do the same for the right side falloff
    if (X > r - soft_x - 1 && soft_x >= 0) {
      float T = (r - X) / (soft_x + 1);
      if (T < 1)
        m1 *= (3 - 2 * T) * T * T;
    }
    // finally, we can fill the buffer with the calculated value
    buffer[X] = m1;
  }
  return true;
}

// Nuke will call this function to add a new Rectangle operator to the scene
// graph. Should you want to use the Nuke Wrapper, this would be the place to
// add it.
static Op* build(Node* node) { return new RectangleIop(node); }

// Here is an example of how to use the Nuke licensing scheme. This
// license will work. Using a value other than License::this_system_id
// will make it fail. The simplest way to use this is to check
// this_system_id agaist a list of allowed ones before setting this.
static License* license()
{
  static License l = {
    License::this_system_id, 0, 0, 0
  };
  return &l;
}

// The Description class gives Nuke access to information about the plugin.
// The first element is the name under which the plugin will be known. The
// second argument is the constructor callback. The optional third argument
// is the License structure that you can use to make sure the caller is
// authorized to use your plugin.

const Op::Description RectangleIop::d(CLASS, build, license());
