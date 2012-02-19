// Grade.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

const char* const HELP =
  "<p>Applies a linear ramp followed by a gamma function to each color channel.</p>"
  "<p>  A = multiply * (gain-lift)/(whitepoint-blackpoint)<br>"
  "  B = offset + lift - A*blackpoint<br>"
  "  output = pow(A*input + B, 1/gamma)</p>"
  "The <i>reverse</i> option is also provided so that you can copy-paste this node to "
  "invert the grade. This will do the opposite gamma correction followed by the "
  "opposite linear ramp.";

#include "DDImage/PixelIop.h"
#include "DDImage/Row.h"
#include "DDImage/DDMath.h"
#include "DDImage/NukeWrapper.h"
#include <string.h>

using namespace DD::Image;

static const char* const CLASS = "Grade";

class GradeIop : public PixelIop
{
  float blackpoint[4];
  float whitepoint[4];
  float black[4];
  float white[4];
  float add[4];
  float multiply[4];
  float gamma[4];
  bool reverse;
  bool black_clamp;
  bool white_clamp;
public:
  GradeIop(Node* node) : PixelIop(node)
  {
    for (int n = 0; n < 4; n++) {
      black[n] = blackpoint[n] = add[n] = 0.0f;
      white[n] = whitepoint[n] = multiply[n] = 1.0f;
      gamma[n] = 1.0f;
    }
    reverse = false;
    black_clamp = true;
    white_clamp = false;
  }
  // indicate that channels only depend on themselves:
  void in_channels(int, ChannelSet& channels) const { }
  void pixel_engine(const Row &in, int y, int x, int r, ChannelMask, Row &);
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;

  void _validate(bool for_real);
};

void GradeIop::_validate(bool for_real)
{
  bool change_any = black_clamp | white_clamp;
  bool change_zero = false;
  for (int z = 0; z < 4; z++) {
    float A = whitepoint[z] - blackpoint[z];
    A = A ? (white[z] - black[z]) / A : 10000.0f;
    A *= multiply[z];
    float B = add[z] + black[z] - blackpoint[z] * A;
    if (A != 1 || B || gamma[z] != 1.0f) {
      change_any = true;
      if (B)
        change_zero = true;
    }
  }
  set_out_channels(change_any ? Mask_All : Mask_None);
  PixelIop::_validate(for_real);
  if (change_zero)
    info_.black_outside(false);
}

void GradeIop::pixel_engine(const Row& in, int y, int x, int r,
                            ChannelMask channels, Row& out)
{
  foreach (n, channels) {
    unsigned z = colourIndex(n);
    if (z > 3) {
      out.copy(in, n, x, r);
      continue;
    }
    float A = whitepoint[z] - blackpoint[z];
    A = A ? (white[z] - black[z]) / A : 10000.0f;
    A *= multiply[z];
    float B = add[z] + black[z] - blackpoint[z] * A;
    if (!B && in.is_zero(n)) {
      out.erase(n);
      continue;
    }
    float G = gamma[z];
    // patch for linux alphas because the pow function behaves badly
    // for very large or very small exponent values.
#ifdef __alpha
    if (G < 0.008f)
      G = 0.0f;
    if (G > 125.0f)
      G = 125.0f;
#endif
    const float* inptr = in[n] + x;
    float* OUTBUF = out.writable(n) + x;
    float* END = OUTBUF + (r - x);
    if (!reverse) {
      // do the linear interpolation:
      if (A != 1 || B) {
        for (float* outptr = OUTBUF; outptr < END;)
          *outptr++ = *inptr++ *A + B;
        inptr = OUTBUF;
      }
      // clamp
      if (white_clamp || black_clamp) {
        for (float* outptr = OUTBUF; outptr < END;) {
          float a = *inptr++;
          if (a < 0.0f && black_clamp)
            a = 0.0f;
          else if (a > 1.0f && white_clamp)
            a = 1.0f;
          *outptr++ = a;
        }
        inptr = OUTBUF;
      }
      // do the gamma:
      if (G <= 0) {
        for (float* outptr = OUTBUF; outptr < END;) {
          float V = *inptr++;
          if (V < 1.0f)
            V = 0.0f;
          else if (V > 1.0f)
            V = INFINITY;
          *outptr++ = V;
        }
      }
      else if (G != 1.0f) {
        G = 1.0f / G;
        for (float* outptr = OUTBUF; outptr < END;) {
          float V = *inptr++;
          if (V <= 0.0f)
            ;              //V = 0.0f;
#ifdef __alpha
          else if (V <= 1e-6f && G > 1.0f)
            V = 0.0f;
#endif
          else if (V < 1)
            V = powf(V, G);
          else
            V = 1.0f + (V - 1.0f) * G;
          *outptr++ = V;
        }
      }
      else if (inptr != OUTBUF) {
        memcpy(OUTBUF, inptr, (END - OUTBUF) * sizeof(*OUTBUF));
      }
    }
    else {
      // Reverse gamma:
      if (G <= 0) {
        for (float* outptr = OUTBUF; outptr < END;)
          *outptr++ = *inptr++ > 0.0f ? 1.0f : 0.0f;
        inptr = OUTBUF;
      }
      else if (G != 1.0f) {
        for (float* outptr = OUTBUF; outptr < END;) {
          float V = *inptr++;
          if (V <= 0.0f)
            ;              //V = 0.0f;
#ifdef __alpha
          else if (V <= 1e-6f && G > 1.0f)
            V = 0.0f;
#endif
          else if (V < 1.0f)
            V = powf(V, G);
          else
            V = 1.0f + (V - 1.0f) * G;
          *outptr++ = V;
        }
        inptr = OUTBUF;
      }
      // Reverse the linear part:
      if (A != 1.0f || B) {
        if (A)
          A = 1 / A;
        else
          A = 1.0f;
        B = -B * A;
        for (float* outptr = OUTBUF; outptr < END;)
          *outptr++ = *inptr++ *A + B;
        inptr = OUTBUF;
      }
      // clamp
      if (white_clamp || black_clamp) {
        for (float* outptr = OUTBUF; outptr < END;) {
          float a = *inptr++;
          if (a < 0.0f && black_clamp)
            a = 0.0f;
          else if (a > 1.0f && white_clamp)
            a = 1.0f;
          *outptr++ = a;
        }
        inptr = OUTBUF;
      }
      else if (inptr != OUTBUF) {
        memcpy(OUTBUF, inptr, (END - OUTBUF) * sizeof(*OUTBUF));
      }
    }
  }
}

#include "DDImage/Knobs.h"

void GradeIop::knobs(Knob_Callback f)
{
  AColor_knob(f, blackpoint, IRange(-1, 1), "blackpoint");
  Tooltip(f, "This color is turned into black");
  AColor_knob(f, whitepoint, IRange(0, 4), "whitepoint");
  Tooltip(f, "This color is turned into white");
  AColor_knob(f, black, IRange(-1, 1), "black", "lift");
  Tooltip(f, "Black is turned into this color");
  AColor_knob(f, white, IRange(0, 4), "white", "gain");
  Tooltip(f, "White is turned into this color");
  AColor_knob(f, multiply, IRange(0, 4), "multiply");
  Tooltip(f, "Constant to multiply result by");
  AColor_knob(f, add, IRange(-1, 1), "add", "offset");
  Tooltip(f, "Constant to add to result (raises both black & white, unlike lift)");
  AColor_knob(f, gamma, IRange(.2, 5), "gamma");
  Tooltip(f, "Gamma correction applied to final result");
  Newline(f, "  ");
  Bool_knob(f, &reverse, "reverse");
  Tooltip(f, "Invert the math to undo the correction");
  Bool_knob(f, &black_clamp, "black_clamp", "black clamp");
  Tooltip(f, "Output that is less than zero is changed to zero");
  Bool_knob(f, &white_clamp, "white_clamp", "white clamp");
  Tooltip(f, "Output that is greater than 1 is changed to 1");
}

static Iop* build(Node* node)
{
  return (new NukeWrapper(new GradeIop(node)))->channelsRGBoptionalAlpha();
}
const Iop::Description GradeIop::d(CLASS, "Color/Correct/Grade", build);
