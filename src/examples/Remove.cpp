// Remove.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const RCLASS = "Remove";

static const char* const HELP = "Removes color channels from the image.";

/* Remove channels from the input. This is really simple to impelement
   as it does nothing except change the info so the channels are not
   there.
 */

#include "DDImage/NoIop.h"
#include "DDImage/Knobs.h"

using namespace DD::Image;

class Remove : public NoIop
{
  ChannelSet channels;
  ChannelSet channels2;
  ChannelSet channels3;
  ChannelSet channels4;

  int operation; // 0 = remove, 1 = keep
public:
  void _validate(bool);
  Remove(Node* node) : NoIop(node)
  {
    channels = Mask_All;
    channels2 = channels3 = channels4 = Mask_None;
    operation = 0;
  }
  virtual void knobs(Knob_Callback);
  const char* Class() const { return RCLASS; }
  const char* node_help() const { return HELP; }
  static Iop::Description d;
};

void Remove::_validate(bool for_real)
{
  copy_info();
  ChannelSet c = channels;
  c += (channels2);
  c += (channels3);
  c += (channels4);
  if (operation) {
    info_.channels() &= (c);
    set_out_channels(info_.channels()); //?
  }
  else {
    info_.turn_off(c);
    set_out_channels(c); //?
  }
}

static const char* const enums[] = {
  "remove", "keep", 0
};

void Remove::knobs(Knob_Callback f)
{
  Enumeration_knob(f, &operation, enums, "operation");
  Tooltip(f, "Remove: the named channels are deleted\n"
             "Keep: all but the named channels are deleted");
  Obsolete_knob(f, "action", "knob operation $value");
  Input_ChannelMask_knob(f, &channels, 0, "channels");
  Input_ChannelMask_knob(f, &channels2, 0, "channels2", "and");
  Input_ChannelMask_knob(f, &channels3, 0, "channels3", "and");
  Input_ChannelMask_knob(f, &channels4, 0, "channels4", "and");
}

static Iop* build(Node* node) { return new Remove(node); }
Iop::Description Remove::d(RCLASS, "Color/Remove", build);
