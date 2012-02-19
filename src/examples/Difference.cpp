// DifferenceIop.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "Difference";

static const char* const HELP =
  "Keyer to produce the difference between two images as a matte.";

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/DDMath.h"

using namespace DD::Image;

class DifferenceIop : public Iop
{
  double offset;
  double gain;
  Channel channel;
public:
  DifferenceIop(Node* node) : Iop(node) { inputs(2);
                                          offset = 0.0;
                                          gain = 1.0;
                                          channel = Chan_Alpha; }
  void _validate(bool);
  void _request(int, int, int, int, ChannelMask, int);
  void engine(int y, int x, int r, ChannelMask channels, Row& out);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  virtual void knobs(Knob_Callback);
  static const Iop::Description d;
};

void DifferenceIop::_validate(bool for_real)
{
  copy_info();
  set_out_channels(mask(channel));
  info_.turn_on(channel);
}

void DifferenceIop::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{
  // If the output is not requested this does nothing:
  if (!intersect(channels, channel)) {
    input0().request(x, y, r, t, channels, count);
    return;
  }
  ChannelSet c1(channels);
  c1 -= (channel);
  c1 += (Mask_RGB);
  input0().request(x, y, r, t, c1, count);
  input1().request(x, y, r, t, Mask_RGB, count);
}

void DifferenceIop::engine(int y, int x, int r, ChannelMask channels, Row& row)
{
  // If the output is not requested this does nothing:
  if (!intersect(channels, channel)) {
    row.get(input0(), y, x, r, channels);
    return;
  }

  // get the colors and all unchanged channels from first input:
  ChannelSet c1(channels);
  c1 -= (channel);
  c1 += (Mask_RGB);
  row.get(input0(), y, x, r, c1);

  // get the colors from the second input:
  Row inA(x, r);
  inA.get(input1(), y, x, r, Mask_RGB);

  const float* AR = inA[Chan_Red] + x;
  const float* AG = inA[Chan_Green] + x;
  const float* AB = inA[Chan_Blue] + x;
  const float* BR = row[Chan_Red] + x;
  const float* BG = row[Chan_Green] + x;
  const float* BB = row[Chan_Blue] + x;
  float* outptr = row.writable(channel) + x;
  float* END = outptr + (r - x);

  while (outptr < END) {
    float dr = *AR++ - *BR++;
    float dg = *AG++ - *BG++;
    float db = *AB++ - *BB++;
    float d = dr * dr + dg * dg + db * db;
    *outptr++ = clamp(float(d * gain - offset));
  }

}

void DifferenceIop::knobs(Knob_Callback f)
{
  Double_knob(f, &offset, "offset");
  Double_knob(f, &gain, "gain");
  Channel_knob(f, &channel, 1, "output");
}

static Iop* build(Node* node) { return new DifferenceIop(node); }
const Iop::Description DifferenceIop::d(CLASS, "Keyer/Difference", build);
