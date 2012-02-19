// AddTimeCode.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "AddTimeCode";
static const char* const HELP =
  "AddTimeCode:\n"
  "Adds a timecode to the metadata passed through.";
static const double MAX_FPS_VAL = 1000;

#include <limits.h>

#include "DDImage/Iop.h"
#include "DDImage/Knobs.h"
#include "DDImage/Row.h"
#include "DDImage/MetaData.h"
#include "DDImage/DDMath.h"

using namespace DD::Image;

class AddTimeCode : public Iop
{
  MetaData::Bundle _meta;

  int _fps;
  bool _fpsFromMeta;
  const char* _startCode;
  int _startFrame;
  bool _startSpecify;

public:
  void _validate(bool);
  void _request(int, int, int, int, ChannelMask, int);
  void engine(int y, int x, int r, ChannelMask, Row &);

  int split_input(int) const
  {
    return 1;
  }

  int maximum_inputs() const { return 1; }
  int minimum_inputs() const { return 1; }

  const char* Class() const { return CLASS; }
  static const Iop::Description d;
  const char* node_help() const { return HELP; }
  void knobs(Knob_Callback);

  AddTimeCode(Node* node) : Iop(node)
  {
    _startCode = "01:00:00:00";
    _fps = 24;
    _fpsFromMeta = true;
    _startSpecify = false;
    _startFrame = 1;
  }

  Iop* in() const
  {
    return input(0);
  }

  void append(Hash& hash)
  {
    hash.append(outputContext().frame());
  }

  int knob_changed(DD::Image::Knob* k)
  {
    if (k->name() == "useFrame") {
      knob("frame")->enable(k->get_value() != 0.0);
      return true;
    }

    if (k->name() == "metafps") {
      knob("fps")->enable(!k->get_value());
      return true;
    }

    return Iop::knob_changed(k);
  }

  const MetaData::Bundle& _fetchMetaData(const char* key)
  {
    _meta = Iop::_fetchMetaData(key);

    int ihh, imm, iss, iff;
    sscanf(_startCode, "%2i:%2i:%2i:%2i", &ihh, &imm, &iss, &iff);

    int startFrame = info_.first_frame();
    if (_startSpecify) {
      startFrame = _startFrame;
    }

    int fps;

    if (_fpsFromMeta) {
      double fpsAsFl = _meta.getDouble(MetaData::FRAME_RATE);
      if ((!isfinite(fpsAsFl)) || (fpsAsFl > MAX_FPS_VAL)) {
        fps = 24;
      }
      else {
        fps = int(fpsAsFl + .5);
      }
    }
    else
      fps = _fps;

    if (fps == 0)
      fps = 24;

    _meta.setData(MetaData::FRAME_RATE, fps);

    int frame = int(outputContext().frame()) - startFrame;
    int divisor = 60 * 60 * 100 * fps;

    int signBit = ((unsigned int)(frame)) >> ((sizeof(int) * CHAR_BIT) - 1);

    int quot = (frame + signBit) / divisor - signBit;
    int theRest = frame - (quot * divisor);

    frame = theRest;

    int offset = ihh;
    offset *= 60;
    offset += imm;
    offset *= 60;
    offset += iss;
    offset *= fps;
    offset += iff;

    int ff = frame + offset;

    int ss = ff / fps;
    ff %= fps;

    int mm = ss / 60;
    ss %= 60;

    int hh = mm / 60;
    mm %= 60;

    hh %= 100;

    char timecode[100];
    sprintf(timecode, "%02i:%02i:%02i:%02i", hh, mm, ss, ff);

    _meta.setData(MetaData::TIMECODE, timecode);

    return _meta;
  }
};

void AddTimeCode::_validate(bool)
{
  copy_info();
}

void AddTimeCode::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{
  in()->request(x, y, r, t, channels, count);
}

void AddTimeCode::engine(int y, int x, int r, ChannelMask channels, Row& out)
{
  out.get(*in(), y, x, r, channels);
}

void AddTimeCode::knobs(Knob_Callback f)
{
  String_knob(f, &_startCode, "startcode");
  Int_knob(f, &_fps, "fps");
  SetFlags(f, DD::Image::Knob::DISABLED);
  Bool_knob(f, &_fpsFromMeta, "metafps", "get FPS from metadata");
  Int_knob(f, &_startFrame, "frame", "start frame");
  SetFlags(f, DD::Image::Knob::DISABLED);
  Bool_knob(f, &_startSpecify, "useFrame", "use start frame?");
}

static Iop* build(Node* node) { return new AddTimeCode(node); }
const Iop::Description AddTimeCode::d(CLASS, "MetaData/Modify", build);
