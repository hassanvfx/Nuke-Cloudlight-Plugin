// Checkerboard2.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "CheckerBoard2";
static const char* const HELP =
  "Generates a checkerboard image, useful as a placeholder for a texture "
  "or background. Boxes are rounded to the nearest pixel, so the proxy "
  "version may not exactly match the full-size one.";

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/Knob.h"
#include "DDImage/DDMath.h"

using namespace DD::Image;

class CheckerBoard2 : public Iop
{

  double boxsize[2];
  float color[4][4];
  float linecolor[4];
  double linewidth[2];
  float centerlinecolor[4];
  double centerlinewidth[2];
  FormatPair formats;
  int boxw;
  int boxh;
  int lw, lh, clw, clh;
  int centerx, centery;

public:

  CheckerBoard2(Node* node) : Iop(node)
  {
    inputs(0);
    // init everything to various grays:
    for (int i = 0; i < 3; i++) {
      color[0][i] = color[2][i] = .1f;
      color[1][i] = color[3][i] = .5f;
      linecolor[i] = centerlinecolor[i] = 1;
    }
    // init alpha to 1 everywhere:
    color[0][3] = color[1][3] = color[2][3] = color[3][3] = 1;
    linecolor[3] = centerlinecolor[3] = 1;
    // make centerline yellow
    centerlinecolor[2] = 0;
    boxsize[0] = boxsize[1] = 64;
    // init widthes:
    linewidth[0] = linewidth[1] = 0;
    centerlinewidth[0] = centerlinewidth[1] = 3;
  }

  void knobs(Knob_Callback f)
  {
    Format_knob(f, &formats, "format");
    Obsolete_knob(f, "full_format", "knob format $value");
    Obsolete_knob(f, "proxy_format", 0);
    WH_knob(f, boxsize, IRange(1, 100), "boxsize", "size");
    SetFlags(f, Knob::SLIDER);
    AColor_knob(f, color[0], "color0", "color 0");
    AColor_knob(f, color[1], "color1", "color 1");
    AColor_knob(f, color[2], "color2", "color 2");
    AColor_knob(f, color[3], "color3", "color 3");
    AColor_knob(f, linecolor, "linecolor", "line color");
    WH_knob(f, linewidth, IRange(0, 10), "linewidth", "line width");
    SetFlags(f, Knob::SLIDER);
    AColor_knob(f, centerlinecolor, "centerlinecolor", "centerline color");
    WH_knob(f, centerlinewidth, IRange(0, 10), "centerlinewidth", "centerline width");
    SetFlags(f, Knob::SLIDER);
  }

  void _validate(bool)
  {
    info_.full_size_format(*formats.fullSizeFormat());
    info_.format(*formats.format());
    info_.channels(Mask_RGBA);
    info_.set(format());
    boxw = MAX(fast_rint(boxsize[0]), 1L);
    boxh = MAX(fast_rint(boxsize[1]), 1L);
    lw = (linewidth[0] > 0) ? MAX(fast_rint(linewidth[0]), 1L) : 0;
    lh = (linewidth[1] > 0) ? MAX(fast_rint(linewidth[1]), 1L) : 0;
    clw = (centerlinewidth[0] > 0) ? MAX(fast_rint(centerlinewidth[0]), 1L) : 0;
    clh = (centerlinewidth[1] > 0) ? MAX(fast_rint(centerlinewidth[1]), 1L) : 0;
    centerx = (info_.format().x() + info_.format().r()) / 2 - lw / 2;
    centery = (info_.format().y() + info_.format().t()) / 2 - lh / 2;
  }

  void engine(int y, int xx, int r, ChannelMask channels, Row& row)
  {
    float* p[4];
    p[0] = row.writable(Chan_Red);
    p[1] = row.writable(Chan_Green);
    p[2] = row.writable(Chan_Blue);
    p[3] = row.writable(Chan_Alpha);

    int Y = y - centery;
    int y0 = -(clh / 2 - lh / 2);
    if (Y >= y0 && Y < y0 + clh) {
      // in centerline, color it in:
      for (int x = xx; x < r; x++)
        for (int i = 0; i < 4; i++)
          p[i][x] = centerlinecolor[i];
      return;
    }

    Y %= 2 * boxh;
    if (Y < 0)
      Y += 2 * boxh;
    if (Y < lh || (Y >= boxh && Y < boxh + lh)) {

      // in a horizontal line, color it in:
      for (int x = xx; x < r; x++)
        for (int i = 0; i < 4; i++)
          p[i][x] = linecolor[i];

    }
    else {

      Y = Y >= boxh ? 3 : 0;
      for (int x = xx; x < r; x++) {
        int X = (x - centerx) % (2 * boxw);
        if (X < 0)
          X += 2 * boxw;
        if (X < lw) {
          // in a vertical line
          for (int i = 0; i < 4; i++)
            p[i][x] = linecolor[i];
          continue;
        }
        if (X >= boxw) {
          if (X < boxw + lw) {
            // in a vertical line
            for (int i = 0; i < 4; i++)
              p[i][x] = linecolor[i];
            continue;
          }
          X = Y ^ 1;
        }
        else {
          X = Y;
        }
        for (int i = 0; i < 4; i++)
          p[i][x] = color[X][i];
      }
    }

    // draw vertical centerline:
    int x0 = centerx - (clw / 2 - lw / 2);
    int x1 = x0 + clw;
    if (x0 < xx)
      x0 = xx;
    if (x1 > r)
      x1 = r;
    for (; x0 < x1; x0++) {
      for (int i = 0; i < 4; i++)
        p[i][x0] = centerlinecolor[i];
    }
  }

  const char* Class() const { return CLASS; }
  const char* displayName() const { return "CheckerBoard"; }
  const char* node_help() const { return HELP; }
  static const Description desc;

};

static Iop* constructor(Node* node) { return new CheckerBoard2(node); }
const Iop::Description CheckerBoard2::desc(CLASS, "Image/CheckerBoard", constructor);
