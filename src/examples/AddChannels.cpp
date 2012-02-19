// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "AddChannels";

static const char* const HELP =
  "Adds channels to the image. If they are not in the input they are "
  "created and filled with black or the defined color.";

#include "DDImage/NoIop.h"
#include "DDImage/Knobs.h"
#include "DDImage/Row.h"

using namespace DD::Image;

class AddChannels : public NoIop
{
  ChannelSet channels;
  ChannelSet channels2;
  ChannelSet channels3;
  ChannelSet channels4;
  float color[4];
public:
  void _validate(bool);
  void engine(int y, int x, int r, ChannelMask channels, Row& row);
  void _request(int x, int y, int r, int t, ChannelMask channels, int count);
  AddChannels(Node* node) : NoIop(node)
  {
    channels = Mask_None;
    channels2 = channels3 = channels4 = Mask_None;
    color[0] = color[1] = color[2] = color[3] = 0;
  }
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static Iop::Description d;
};

void AddChannels::_validate(bool for_real)
{
  copy_info();
  ChannelSet newchan = channels;
  newchan += (channels2);
  newchan += (channels3);
  newchan += (channels4);
  set_out_channels(newchan);
  info_.turn_on(newchan);
}

void AddChannels::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{
  input0().request(x, y, r, t, channels, count);
}

void AddChannels::knobs(Knob_Callback f)
{
  ChannelMask_knob(f, &channels, "channels");
  ChannelMask_knob(f, &channels2, "channels2", "and");
  ChannelMask_knob(f, &channels3, "channels3", "and");
  ChannelMask_knob(f, &channels4, "channels4", "and");
  AColor_knob(f, color, "color");
  Tooltip(f, "Color to fill in any new channels. Existing channels are "
             "not changed."
             "\n@i;Colors do not "
             "work correctly in the current version except for rgba! Use "
             "only gray shades for other channels.");
}

void AddChannels::engine(int y, int x, int r, ChannelMask channels, Row& row)
{
  input0().get(y, x, r, channels, row);
  ChannelSet m(channels);
  m -= (input0().channels());
  foreach (z, m) {
    int i = z - 1;
    if (i > 3)
      i = 3;
    if (!color[i])
      continue;
    float* TO = row.writable(z) + x;
    float* END = TO + r - x;
    while (TO < END)
      *TO++ = color[i];
  }
}

static Iop* build(Node* node) { return new AddChannels(node); }
Iop::Description AddChannels::d(CLASS, 0, build);
