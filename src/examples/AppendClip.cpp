// AppendClip.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/Knob.h"
#include "DDImage/DDMath.h"
#include <stdio.h>
#include <string.h>
//#include "DoubleKnob.h"

using namespace DD::Image;

static const char* const CLASS = "AppendClip";
static const char* const HELP = "Append one clip to another.";

class AppendClip : public Iop
{
  int fadeIn, fadeOut, crossDis;
  int firstFrame;
  int thisFrame;

  int input0;
  int input1;
  float weight0;
  float weight1;

protected:

  void _validate(bool);
  void _request(int, int, int, int, ChannelMask, int);
  void engine(int y, int x, int r, ChannelMask, Row &);
  const OutputContext& inputContext(int, int, OutputContext&) const;

public:

  AppendClip(Node* node) : Iop(node)
  {
    fadeIn = fadeOut = crossDis = 0;
    firstFrame = 1;
    // init so uses_input sort of works:
    input0 = input1 = 0;
    weight0 = 1;
    weight1 = 0;
  }
  int minimum_inputs() const { return 1; }
  int maximum_inputs() const { return 10000; }
  float uses_input(int i) const;
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }

  static Iop::Description d;

  void setOutputContext(const OutputContext& c)
  {
    Iop::setOutputContext(c);
    thisFrame = int(rint(outputContext().frame()));
  }

  void append(Hash& hash)
  {
    hash.append(thisFrame);
  }

  // Disconnected input other than first one get a null:
  Op* default_input(int i) const
  {
    return i ? 0 : Iop::default_input(i);
  }
};

static Iop* build(Node* node) { return new AppendClip(node); }
Iop::Description AppendClip::d(CLASS, "Image/Clip/Append", build);

float AppendClip::uses_input(int i) const
{
  if (i == input0 && weight0 > .01f)
    return weight0;
  if (i == input1 && weight1 > .01f)
    return weight1;
  return .01f;
}

void AppendClip::knobs(Knob_Callback f)
{
  Int_knob( f, &fadeIn, "fadeIn", "Fade In");
  Text_knob( f, "frames");
  Int_knob( f, &fadeOut, "fadeOut", "Fade Out");
  Text_knob( f, "frames");
  Int_knob( f, &crossDis, "dissolve", "Cross Dissolve");
  Text_knob( f, "frames");
  Int_knob( f, &firstFrame, "firstFrame", "First Frame");
  SetFlags(f, Knob::EARLY_STORE);
  Knob* k = Int_knob(f, 0, "lastFrame", "Last Frame");
  if (k)
    k->disable();
}

const OutputContext& AppendClip::inputContext(int in, int, OutputContext& context) const
{
  int f = thisFrame;
  f -= firstFrame;
  for (int i = 0; i < in; i++) {
    Iop* iop = (Iop*)(input_op(i));
    if (!iop)
      continue;
    iop->validate(false);
    f -= (iop->last_frame() - iop->first_frame() + 1) - crossDis;
  }
  Iop* iop = (Iop*)(input_op(in));
  if (iop) {
    iop->validate(false);
    f += iop->first_frame();
  }
  context = outputContext();
  context.setFrame(f);
  return context;
}

void AppendClip::_validate(bool for_real)
{
  // figure out total length:
  int f, i;
  f = firstFrame;
  input0 = -1;
  for (i = 0; i < inputs(); i++) {
    Iop* iop = (Iop*)(input(i));
    if (!iop)
      continue;
    iop->validate(for_real);
    int g = f + iop->last_frame() - iop->first_frame() + 1;
    if (input0 < 0 && thisFrame < g) {
      input0 = i;
      for (input1 = i + 1; input1 < inputs() && !input(input1); input1++)
        ;
      weight0 = 1;
      weight1 = 0;
      if (input1 < inputs() && thisFrame >= g - crossDis) {
        float w = float(g - thisFrame) / (crossDis + 1);
        w = (3 - 2 * w) * w * w;
        weight0 = w;
        weight1 = 1 - w;
      }
    }
    f = g - crossDis;
  }
  int lastFrame = f + crossDis - 1;
  knob("lastFrame")->set_value(lastFrame);
  if (input0 < 0) { // after the last frame
    input0 = input1 = inputs() - 1;
    weight0 = 1;
    weight1 = 0;
  }
  if (fadeIn && thisFrame < firstFrame + fadeIn) {
    float w = float(thisFrame - firstFrame + 1) / (fadeIn + 1);
    if (w < 0)
      w = 0;
    else
      w = w * w;
    weight0 *= w;
    weight1 *= w;
  }
  if (fadeOut && thisFrame > lastFrame - fadeOut) {
    float w = float(lastFrame - thisFrame + 1) / (fadeOut + 1);
    if (w < 0)
      w = 0;
    else
      w = w * w;
    weight0 *= w;
    weight1 *= w;
  }
  // fix problems with null inputs:
  if (!input(input0))
    input0 = 0;
  if (input1 >= inputs() || !input(input1))
    input1 = 0;
  copy_info(input0);
  if (weight0 == 1) {
    set_out_channels(Mask_None, input0);
  }
  else {
    set_out_channels(Mask_All, input0);
    if (weight1)
      merge_info(input1);
  }
  info_.first_frame(firstFrame);
  info_.last_frame(lastFrame);
}

void AppendClip::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{
  if (weight0)
    input(input0)->request(x, y, r, t, channels, count);
  if (weight1)
    input(input1)->request(x, y, r, t, channels, count);
}

void AppendClip::engine(int y, int x, int r, ChannelMask channels, Row& out)
{
  if (!weight0) {
    out.erase(channels);
    return;
  }
  input(input0)->get(y, x, r, channels, out);
  if (weight1) {
    Row in(x, r);
    input(input1)->get(y, x, r, channels, in);
    foreach (z, channels) {
      const float* A = out[z] + x;
      const float* B = in[z] + x;
      float* C = out.writable(z) + x;
      float* E = C + (r - x);
      while (C < E)
        *C++ = *A++ *weight0 + *B++ *weight1;
    }
  }
  else if (weight0 < 1) {
    foreach (z, channels) {
      const float* A = out[z] + x;
      float* C = out.writable(z) + x;
      float* E = C + (r - x);
      while (C < E)
        *C++ = *A++ *weight0;
    }
  }
}
