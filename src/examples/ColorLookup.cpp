// ColorLookupIop.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

const char* const HELP =
  "Output is the value of the color lookup curve indexed by the input color";

#include "DDImage/ColorLookup.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/DDMath.h"
#include "DDImage/LookupCurves.h"
#include "DDImage/NukeWrapper.h"

using namespace DD::Image;

static const char* const CLASS = "ColorLookup";

static const CurveDescription defaults[] = {
  { "master", "y C 0 1" },
  { "red",    "y C 0 1" },
  { "green",  "y C 0 1" },
  { "blue",   "y C 0 1" },
  { "alpha",  "y C 0 1" },
  { 0 }
};

class ColorLookupIop : public ColorLookup
{
  LookupCurves lut;
  float range;
  float range_knob;
  float source_value[4];
  float target_value[4];
  //bool identity[5];

public:
  ColorLookupIop(Node* node) : ColorLookup(node), lut(defaults)
  {
    //identity[0..4] = true;
    range = range_knob = 1;
    source_value[0] = source_value[1] = source_value[2] = source_value[3] = 0.0f;
    target_value[0] = target_value[1] = target_value[2] = target_value[3] = 0.0f;
  }
  float lookup(int z, float value) const;
  void _validate(bool);
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;

  void pixel_engine(const Row& in, int y, int x, int r,
                    ChannelMask channels, Row& out);
};

void ColorLookupIop::pixel_engine(const Row& in, int y, int x, int r,
                                  ChannelMask channels, Row& out)
{
  if (range == 1.0f) {
    ColorLookup::pixel_engine(in, y, x, r, channels, out);
  }
  else {
    foreach (z, channels) {
      const float* FROM = in[z] + x;
      float* TO = out.writable(z) + x;
      const float* END = FROM + (r - x);
      while (FROM < END)
        *TO++ = *FROM++ / range;
    }
    ColorLookup::pixel_engine(out, y, x, r, channels, out);
  }
}

#if 0
static bool identity(const Animation* a, float range)
{
  #if 1
  for (float v = 0; v <= range; v += range / 8)
    if (a->getValue(v) != v)
      return false;
  #else
  for (int i = 0; i < a->size(); i++) {
    const Animation_Key& k = a->key(i);
    if (k.x != k.y)
      return false;
    if (k.interpolation & Animation::USER_SET_SLOPE) {
      if (k.lslope != 1 || k.rslope != 1)
        return false;
    }
  }
  #endif
  return true;
}
#endif

void ColorLookupIop::_validate(bool for_real)
{
  if (range_knob <= 0)
    range = 1.0f;
  else
    range = range_knob;
  //for (int i = 0; i < 5; i++) identity[i] = lut.isIdentity(i, range);
  ColorLookup::_validate(for_real);
}

float ColorLookupIop::lookup(int z, float value) const
{
  value *= range;
  /*if (!identity[0])*/ value = float(lut.getValue(0, value));
  /*if (!identity[z+1])*/ value = float(lut.getValue(z + 1, value));
  return value;
}

const char* setRgbScript =
  "source = nuke.thisNode().knob('source')\n"
  "target = nuke.thisNode().knob('target')\n"
  "lut = nuke.thisNode().knob('lut')\n"
  "lut.setValueAt(target.getValue(0), source.getValue(0), 1)\n"
  "lut.setValueAt(target.getValue(1), source.getValue(1), 2)\n"
  "lut.setValueAt(target.getValue(2), source.getValue(2), 3)\n";

const char* setRgbaScript =
  "source = nuke.thisNode().knob('source')\n"
  "target = nuke.thisNode().knob('target')\n"
  "lut = nuke.thisNode().knob('lut')\n"
  "lut.setValueAt(target.getValue(0), source.getValue(0), 1)\n"
  "lut.setValueAt(target.getValue(1), source.getValue(1), 2)\n"
  "lut.setValueAt(target.getValue(2), source.getValue(2), 3)\n"
  "lut.setValueAt(target.getValue(3), source.getValue(3), 4)\n";

const char* setAScript =
  "source = nuke.thisNode().knob('source')\n"
  "target = nuke.thisNode().knob('target')\n"
  "lut = nuke.thisNode().knob('lut')\n"
  "lut.setValueAt(target.getValue(3), source.getValue(3), 4)\n";


void ColorLookupIop::knobs(Knob_Callback f)
{
  Obsolete_knob(f, "layer", "knob channels $value");
  Float_knob(f, &range_knob, IRange(1, 16), "range");
  Tooltip(f, "Values between 0 and this will use a lookup table and thus be much faster");
  LookupCurves_knob(f, &lut, "lut");
  Newline(f);
  AColor_knob(f, source_value, IRange(0, 4), "source");
  SetFlags(f, Knob::NO_ANIMATION | Knob::NO_RERENDER | Knob::DO_NOT_WRITE);
  Tooltip(f, "Pick a source color for adding points.");
  AColor_knob(f, target_value, IRange(0, 4), "target");
  SetFlags(f, Knob::NO_ANIMATION | Knob::NO_RERENDER | Knob::DO_NOT_WRITE);
  Tooltip(f, "Pick a destination color for adding points.");
  Newline(f);
  PyScript_knob(f, setRgbScript, "setRGB", "Set RGB");
  Tooltip(f, "Add points on the r, g, b curves mapping source to target.");
  PyScript_knob(f, setRgbaScript, "setRGBA", "Set RGBA");
  Tooltip(f, "Add points on the r, g, b, and a curves mapping source to target.");
  PyScript_knob(f, setAScript, "setA", "Set A");
  Tooltip(f, "Add points on the a curve mapping source to target.");
  Divider(f);
}

static Iop* build(Node* node)
{
  return (new NukeWrapper(new ColorLookupIop(node)))->channels(Mask_RGBA);
}
const Iop::Description ColorLookupIop::d(CLASS, "Color/Correct/Lookup", build);
