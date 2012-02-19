// Keymix.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "Keymix";
static const char* const HELP =
  "Copies channels from A to B only where the Mask input is non-zero.";

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"

using namespace DD::Image;

static const char* const bbox_names[] = {
  "union", "B\tB side", "A\tA side", 0
};

class Keymix : public Iop
{
public:

  ChannelSet channels;
  Channel maskChannel;
  bool invertMask;
  float mix;
  // what to do with bbox:
  enum { UNION, BBOX, ABOX };
  int bbox_type;

  Keymix(Node* node) : Iop(node)
  {
    inputs(3);
    channels = Mask_All;
    maskChannel = Chan_Alpha;
    invertMask = false;
    mix = 1;
    bbox_type = UNION;
  }

  const char* input_label(int n, char*) const
  {
    switch (n) {
      case 0: return "B";
      case 1: return "A";
      default: return "mask";
    }
  }

  void _validate(bool)
  {
    copy_info();
    merge_info(1, channels);
    merge_info(2, maskChannel);
    if (input(2)->black_outside() && !invertMask)
      info_.black_outside(input0().black_outside());

    ChannelSet outchans(channels);
    outchans &= input1().channels();

    set_out_channels(outchans);
    // That figures out the true union, change to what user wants:
    switch (bbox_type) {
      case UNION:
        // we can do better if we know mask is limited:
        if (input(2)->black_outside() && !invertMask) {
          Box abox = input1().info();
          abox.intersect(input1().info());
          info_.set(input0().info());
          info_.merge(abox);
        }
        break;
      case BBOX:
        info_.set(input0().info());
        break;
      case ABOX:
        info_.set(input1().info());
        break;
    }
  }

  void _request(int x, int y, int r, int t, ChannelMask mask, int count)
  {
    input0().request(x, y, r, t, mask, count);
    ChannelSet copied = mask;
    copied &= (channels);
    if (!copied || mix <= 0)
      return;
    input1().request(x, y, r, t, copied, count);
    input(2)->request(x, y, r, t, maskChannel, count);
  }

  void engine(int y, int x, int r, ChannelMask mask, Row& out)
  {
    input0().get(y, x, r, mask, out);
    ChannelSet copied = mask;
    copied &= (channels);
    if (!copied || mix <= 0)
      return;

    // get the mask first so we can check if it is zero:
    Row maskrow(x, r);
    input(2)->get(y, x, r, maskChannel, maskrow);

    int X = x;
    int R = r;
    if (maskrow.is_zero(maskChannel)) {
      if (!invertMask)
        return;
    }
    else if (!invertMask) {
      // lets restrict the range in an attempt to speed it up as much
      // as possible:
      while (X < R && maskrow[maskChannel][X] <= 0)
        ++X;
      while (R > X && maskrow[maskChannel][R - 1] <= 0)
        --R;
    }
    else {
      while (X < R && !(maskrow[maskChannel][X] < 1))
        ++X;
      while (R > X && !(maskrow[maskChannel][R - 1] < 1))
        --R;
    }
    if (X >= R)
      return;

    // row is allocated at full x,r width rather than X,R to try to avoid
    // memory fragmentation from allocating random sizes:
    Row arow(x, r);
    input1().get(y, X, R, copied, arow);

    foreach (z, copied) {
      const float* AFROM = arow[z];
      const float* BFROM = out[z];
      float* TO = out.writable(z);
      // copy unchanged portions on each end:
      if (TO != BFROM) {
        if (X > x)
          memcpy(TO + x, BFROM + x, (X - x) * sizeof(float));
        if (R < r)
          memcpy(TO + R, BFROM + R, (r - R) * sizeof(float));
      }
      // Do the middle part:
      const float* MASK = maskrow[maskChannel];
      if (mix < 1) {
        if (invertMask) {
          for (int xx = X; xx < R; ++xx) {
            float v = (1 - MASK[xx]) * mix;
            if (v <= 0)
              TO[xx] = BFROM[xx];
            else if (v < 1)
              TO[xx] = AFROM[xx] * v + BFROM[xx] * (1 - v);
            else
              TO[xx] = AFROM[xx];
          }
        }
        else {
          for (int xx = X; xx < R; ++xx) {
            float v = MASK[xx] * mix;
            if (v <= 0)
              TO[xx] = BFROM[xx];
            else if (v < 1)
              TO[xx] = AFROM[xx] * v + BFROM[xx] * (1 - v);
            else
              TO[xx] = AFROM[xx];
          }
        }
      }
      else {
        if (invertMask) {
          for (int xx = X; xx < R; ++xx) {
            float v = MASK[xx];
            if (v <= 0)
              TO[xx] = AFROM[xx];
            else if (v < 1)
              TO[xx] = AFROM[xx] * (1 - v) + BFROM[xx] * v;
            else
              TO[xx] = BFROM[xx];
          }
        }
        else {
          for (int xx = X; xx < R; ++xx) {
            float v = MASK[xx];
            if (v <= 0)
              TO[xx] = BFROM[xx];
            else if (v < 1)
              TO[xx] = AFROM[xx] * v + BFROM[xx] * (1 - v);
            else
              TO[xx] = AFROM[xx];
          }
        }
      }
    }
  }

  void knobs(Knob_Callback f)
  {
    Input_ChannelMask_knob(f, &channels, 1, "channels");
    Tooltip(f, "Channels to copy from A. Other channels are copied unchanged from B");
    Input_Channel_knob(f, &maskChannel, 1, 2, "maskChannel", "mask channel");
    Tooltip(f, "Channel to use from mask input");
    Bool_knob(f, &invertMask, "invertMask", "invert");
    Tooltip(f, "Flip meaning of the the mask channel");
    Float_knob(f, &mix, "mix");
    Tooltip(f, "Dissolve between B-only at 0 and the full keymix at 1");
    Enumeration_knob(f, &bbox_type, bbox_names, "bbox", "Set BBox to");
    Tooltip(f, "Clip one input to match the other if wanted");
  }

  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Description d;

};

static Iop* build(Node* node) { return new Keymix(node); }
const Iop::Description Keymix::d(CLASS, 0, build);
