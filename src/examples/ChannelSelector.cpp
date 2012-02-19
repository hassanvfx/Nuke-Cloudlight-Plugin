//------------------------------------------------------------------------------
// ChannelSelector: This operator is a copy of the channel selection used in the
//                  default ViewerInput node when GPU processing is active.
//                  This version additionally supports software processing.
//
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.
//------------------------------------------------------------------------------

#include "DDImage/PixelIop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"

//------------------------------------------------------------------------------

using namespace DD::Image;

//------------------------------------------------------------------------------

namespace
{
  const char* const kapChannels[] = {
    "Luminance",
    "Matte overlay",
    "RGB",
    "R",
    "G",
    "B",
    "A",
    NULL
  };

  const int kChannelsDefaultIndex = 2;
}

//------------------------------------------------------------------------------

class ChannelSelectorOp : public DD::Image::PixelIop
{
public:
  ChannelSelectorOp(Node* lpNode);

private:

  virtual const char* gpuEngine_body() const;

  // You can also override gpuEngine_decl() if you want to declare objects for use in shaders.
  // For example: "uniform sampler2D $$lut;\n"


  virtual void append(DD::Image::Hash& lrHash);
  virtual void knobs(DD::Image::Knob_Callback f);
  virtual const char* Class() const;
  virtual const char* node_help() const { return "Selects channel(s) to pass through"; }

  virtual void pixel_engine(const Row& lrIn, int lY, int lX, int lR, ChannelMask lChannels, Row& lrOut);
  virtual void in_channels(int, ChannelSet& lrChannels) const {}

  void CopySingleChannel(const Row& lrIn, int lY, int lX, int lR, ChannelMask lChannels, Row& lrOut, Channel lSrcChan);
  void LuminanceEngine(const Row& lrIn, int lY, int lX, int lR, ChannelMask lChannels, Row& lrOut);
  void MatteOverlayEngine(const Row& lrIn, int lY, int lX, int lR, ChannelMask lChannels, Row& lrOut);


  int _channel;

  static const DD::Image::Op::Description _sDesc;

};

//------------------------------------------------------------------------------

ChannelSelectorOp::ChannelSelectorOp(Node* lpNode)
  : PixelIop(lpNode)
  , _channel(kChannelsDefaultIndex)
{
}

//------------------------------------------------------------------------------

void ChannelSelectorOp::pixel_engine(const Row& lrIn, int lY, int lX, int lR, ChannelMask lChannels, Row& lrOut)
{
  switch (_channel) {
    case 0:
      LuminanceEngine(lrIn, lY, lX, lR, lChannels, lrOut);
      break;
    case 1:
      MatteOverlayEngine(lrIn, lY, lX, lR, lChannels, lrOut);
      break;
    case 2:
    default:
      lrOut.copy(lrIn, lChannels, lX, lR);
      break;
    case 3:
    case 4:
    case 5:
    case 6:
      CopySingleChannel(lrIn, lY, lX, lR, lChannels, lrOut, static_cast<Channel> (Chan_Red + _channel - 3));
      break;
  }
}

//------------------------------------------------------------------------------

void ChannelSelectorOp::CopySingleChannel(const Row& lrIn, int lY, int lX, int lR, ChannelMask lChannels, Row& lrOut, Channel lSrcChan)
{
  const float* lpSrc = lrIn[lSrcChan] + lX;
  if (lpSrc == NULL)
    lrOut.erase(lChannels);
  else {
    foreach (lDestChan, lChannels) {
      float* lpDest = lrOut.writable(lDestChan) + lX;
      std::copy(lpSrc, lpSrc + lR - lX, lpDest);
    }
  }
}

//------------------------------------------------------------------------------

void ChannelSelectorOp::LuminanceEngine(const Row& lrIn, int lY, int lX, int lR, ChannelMask lChannels, Row& lrOut)
{
  const float* lpSrcRed   = lrIn[Chan_Red]   + lX;
  const float* lpSrcGreen = lrIn[Chan_Green] + lX;
  const float* lpSrcBlue  = lrIn[Chan_Blue]  + lX;

  if (lpSrcRed == NULL || lpSrcGreen == NULL || lpSrcBlue == NULL)
    lrOut.erase(Mask_RGBA);
  else {
    float* lpDestRed   = lrOut.writable(Chan_Red)   + lX;
    float* lpDestGreen = lrOut.writable(Chan_Green) + lX;
    float* lpDestBlue  = lrOut.writable(Chan_Blue)  + lX;
    const float* lpSrcRedEnd = lpSrcRed + lR - lX;

    while (lpSrcRed < lpSrcRedEnd) {
      float lLuminance = 0.2125f * *lpSrcRed++ + 0.7154f * *lpSrcGreen++ + 0.0721f * *lpSrcBlue++;
      *lpDestRed++   = lLuminance;
      *lpDestGreen++ = lLuminance;
      *lpDestBlue++  = lLuminance;
    }

    if (lChannels.contains(Chan_Alpha))
      lrOut.copy(lrIn, Chan_Alpha, lX, lR);
  }
}

//------------------------------------------------------------------------------

void ChannelSelectorOp::MatteOverlayEngine(const Row& lrIn, int lY, int lX, int lR, ChannelMask lChannels, Row& lrOut)
{
  const float* lpSrcRed   = lrIn[Chan_Red]   + lX;
  const float* lpSrcGreen = lrIn[Chan_Green] + lX;
  const float* lpSrcBlue  = lrIn[Chan_Blue]  + lX;
  const float* lpSrcAlpha = lrIn[Chan_Alpha] + lX;

  if (lpSrcRed == NULL || lpSrcGreen == NULL || lpSrcBlue == NULL || lpSrcAlpha == NULL)
    lrOut.erase(Mask_RGBA);
  else {
    float* lpDestRed   = lrOut.writable(Chan_Red)   + lX;
    float* lpDestGreen = lrOut.writable(Chan_Green) + lX;
    float* lpDestBlue  = lrOut.writable(Chan_Blue)  + lX;
    float* lpDestAlpha = lrOut.writable(Chan_Alpha) + lX;
    const float* lpSrcRedEnd = lpSrcRed + lR - lX;

    while (lpSrcRed < lpSrcRedEnd) {
      float lAlpha = *lpSrcAlpha++ *0.5f;
      float lSrcRed = *lpSrcRed++;
      *lpDestRed++   = lSrcRed + (1.0f - lSrcRed) * lAlpha;
      *lpDestGreen++ = *lpSrcGreen++ *(1.0f - lAlpha);
      *lpDestBlue++  = *lpSrcBlue++ *(1.0f - lAlpha);
      *lpDestAlpha++ = lAlpha;
    }
  }
}

//------------------------------------------------------------------------------

const char* ChannelSelectorOp::gpuEngine_body() const
{
  // Always declare variables beginning with "$$", which tells Nuke to generate a unique identifier for them in the
  // op instance so there won't be any clashes with ops in other parts of the tree.
  // You can also use identifiers such as "$gamma$", which Nuke will replace with the value of a corresponding knob,
  // such as one called "gamma" in the above case.

  // Return NULL from this function (which is the base class default) to indicate that it can't be processed on the GPU.

  switch (_channel) {
    case 0:
      return
        "float $$lum = OUT.r * 0.2125 + OUT.g * 0.7154 + OUT.b * 0.0721;\n"
        "OUT = vec4($$lum, $$lum, $$lum, OUT.a);\n";
    case 1:
      return
        "float $$alpha = OUT.a * 0.5;\n"
        "OUT = vec4(OUT.r + (1.0 - OUT.r) * $$alpha, OUT.g - OUT.g * $$alpha, OUT.b - OUT.b * $$alpha, $$alpha);\n";
    case 2:
    default:
      return "\n";
    case 3:
      return "OUT = vec4(OUT.r, OUT.r, OUT.r, OUT.a);\n";
    case 4:
      return "OUT = vec4(OUT.g, OUT.g, OUT.g, OUT.a);\n";
    case 5:
      return "OUT = vec4(OUT.b, OUT.b, OUT.b, OUT.a);\n";
    case 6:
      return "OUT = vec4(OUT.a, OUT.a, OUT.a, OUT.a);\n";
  }
}

//------------------------------------------------------------------------------

void ChannelSelectorOp::append(Hash& lrHash)
{
  // The command below stops changes to the op from forcing the tree to recalculate.
  // In GPU mode this helps because recalculation isn't required, as the op is applied every time by the shader.
  // Unfortunately there's currently no way to check the op's state to see whether it's being applied by the CPU or GPU.

  //lrHash = input0().hash();
}

//------------------------------------------------------------------------------

void ChannelSelectorOp::knobs(Knob_Callback f)
{
  Enumeration_knob(f, &_channel, kapChannels, "channel_selector", "channel");
  SetFlags(f, Knob::NO_ANIMATION | Knob::NO_UNDO);
}

//------------------------------------------------------------------------------

const char* ChannelSelectorOp::Class() const
{
  return _sDesc.name;
}

//------------------------------------------------------------------------------

static Op* BuildChannelSelector(Node* lpNode)
{
  return new ChannelSelectorOp(lpNode);
}

//------------------------------------------------------------------------------

const Op::Description ChannelSelectorOp::_sDesc("ChannelSelector", BuildChannelSelector);

//------------------------------------------------------------------------------
