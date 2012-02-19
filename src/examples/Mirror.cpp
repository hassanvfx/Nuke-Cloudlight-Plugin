// Mirror.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

// Flips the image around the center of the Format image area.

// Notice that this implements the build_transform() call. This is used
// to flip the transformation over for any handles before this in the
// tree.

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Format.h"
#include "DDImage/ViewerContext.h"

using namespace DD::Image;

static const char* const CLASS = "Mirror";
static const char* const HELP = "Flips the image around the center of the Format image area.";

class Mirror : public Iop
{
  bool horizontal_;
  bool vertical_;
  int X, R, Y, T;
protected:
  void _validate(bool);
  virtual void _request(int, int, int, int, ChannelMask, int);
  void engine(int y, int x, int r, ChannelMask, Row & row);
  void build_handles(ViewerContext*);
public:
  Mirror(Node* node) : Iop(node) { horizontal_ = vertical_ = false; }
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }

  static const Iop::Description d;
};

void Mirror::_validate(bool)
{
  copy_info();

  // this swaps the corners of the bounding box if the image is mirrored,
  // taking care to resize should the bounding box is smaller than the image
  int height = format().height();
  int width  = format().width();
  if (horizontal_) {
    int t = info_.r();
    info_.r(width - info_.x());
    info_.x(width - t);
  }
  if (vertical_) {
    int t = info_.t();
    info_.t(height - info_.y());
    info_.y(height - t);
    info_.ydirection(info_.ydirection() * -1);
  }
}

void Mirror::build_handles(ViewerContext* ctx)
{
  validate(false);

  // do the translation only in when the viewer is in 2D and not in 3D mode
  if (!node_disabled() && ctx->viewer_mode() == VIEWER_2D) {
    // Apply the matrix so handles draw after are correct:
    Matrix4 saved_matrix = ctx->modelmatrix;
    Matrix4& m = ctx->modelmatrix;
    if (horizontal_) {
      m.scale(-1, 1);
      m.translate(-format().width(), 0);
    }
    if (vertical_) {
      m.scale(1, -1);
      m.translate(0, -format().height());
    }
    add_input_handle(0, ctx);
    ctx->modelmatrix = saved_matrix;
  }
  else
    add_input_handle(0, ctx);
}

void Mirror::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{

  int height = format().height();
  int width  = format().width();

  if (horizontal_) {
    int T = r;
    r = width - x;
    x = width - T;
  }
  if (vertical_) {
    int T = t;
    t = height - y;
    y = height - T;
  }

  input0().request(x, y, r, t, channels, count);

}

// if the vertical_ flag is true, write each row of pixels in reverse order to
// mirror about a vertical axis.
// if the horizontal_ flag is true, ask for the height-y row and write it into
// the y row, to mirror the image about a horizontal axis.
void Mirror::engine(int y, int x, int r, ChannelMask channels, Row& row)
{
  int height = format().height();
  int width  = format().width();

  if (vertical_)
    y = height - 1 - y;

  if (horizontal_) {
    Row pixels_in(width - r, width - x);
    pixels_in.get(input0(), y, width - r, width - x, channels);
    foreach(z, channels) {
      if (pixels_in.is_zero(z)) {
        row.erase(z);
      }
      else {
        float* outptr = row.writable(z) + x;
        for (int i = x; i < r; i++) {
          *outptr++ = pixels_in[z][width - 1 - i];
        }
      }
    }
  }
  else {
    row.get(input0(), y, x, r, channels);
  }
}

#include "DDImage/Knobs.h"

void Mirror::knobs(Knob_Callback f)
{
  Bool_knob(f, &horizontal_, "Horizontal");
  Bool_knob(f, &vertical_, "Vertical");
}

static Iop* build(Node* node) { return new Mirror(node); }
const Iop::Description Mirror::d(CLASS, "Transform/Mirror", build);
