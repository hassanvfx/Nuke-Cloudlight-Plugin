// Position.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

// Moves the input by an integer number of pixels.

// Notice that the controls are an X/Y position controlled by the user
// and a 0,0 position that the user cannot move. This is so the plugin
// can figure out the translation between 0,0 and the output
// position. Without this proxy scaling produces X,Y positions that are
// not useful for moving the corner of the image to, because they
// produce positions that point at a certain pixel. Moving the
// lower-left corner here is not always correct as the lower-left
// corner is not always the same point relative to the image when
// you compare the proxy and full-size images. This can be seen if
// you imagine them having different aspect ratios.

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/Format.h"
#include "DDImage/Matrix4.h"

using namespace DD::Image;

static const char* const CLASS = "Position";
static const char* const HELP = "Moves the input by an integer number of pixels.";



class Position : public Iop
{
  void _validate(bool);
  virtual void _request(int, int, int, int, ChannelMask, int);
  virtual void engine(int y, int x, int r, ChannelMask, Row & t);
  double x, y;
  double x0, y0;
  int dx, dy;
  Matrix4 matrix_;
public:
  Position(Node* node) : Iop(node) { x = y = x0 = y0 = 0; }
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;
  Matrix4* matrix() { return &matrix_; }
  int slowness() const { return 1; } // this is a really fast operator...
};

void Position::_validate(bool)
{
  // Figure out the integer translations. Floor(x+.5) is used so that
  // values always round the same way even if negative and even if .5
  dx = int(floor(this->x - x0 + .5));
  dy = int(floor(this->y - y0 + .5));
  // move the bounding box:
  copy_info();
  info_.move(dx, dy);
  // create the transformation matrix for the GUI:
  matrix_.translation(x, y);
}

void Position::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{
  // adjust the input viewport:
  x -= dx;
  r -= dx;
  y -= dy;
  t -= dy;
  // get that rectangle:
  input0().request(x, y, r, t, channels, count);
}

void Position::engine(int Y, int X, int R, ChannelMask channels, Row& row)
{
  // move the row over so it reads the input area:
  row.offset(-dx);

  // read the data:
  row.get(input0(), Y - dy, X - dx, R - dx, channels);

  // move the data back
  row.offset(dx);
}

void Position::knobs(Knob_Callback f)
{
  XY_knob(f, &x, "translate");
  XY_knob(f, &x0, 0, INVISIBLE);
  Tooltip(f, "translate\n"
             "This is rounded to the nearest number of pixels so no filtering is done.");
}

static Iop* build(Node* node) { return new Position(node); }
const Iop::Description Position::d(CLASS, "Transform/Position", build);
