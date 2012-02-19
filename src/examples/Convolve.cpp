// Convolve.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char CLASS[] = "Convolve";
static const char HELP[] =
  "This node takes two inputs. Input B is the image you wish to "
  "perform the convolution on, Input A is the convolution matrix. "
  "@i;It is very much recommended that you Crop input A to a small "
  "area! @n;The cropped area is what is used, the center of the "
  "filter is the center of the crop.";

#include <stdio.h>
#include "DDImage/Iop.h"
#include "DDImage/DDString.h"
#include "DDImage/Thread.h"
#include "DDImage/Row.h"
#include "DDImage/Tile.h"
#include "DDImage/Knobs.h"
#include "DDImage/DDMath.h"
#include "DDImage/NukeWrapper.h"
#ifndef FN_PROCESSOR_PPC
  #include <xmmintrin.h>
#endif

using namespace DD::Image;

class Convolve : public Iop
{

  bool K_normalize;
  int filterWidth;
  int filterHeight;
  Channel channel;
  ChannelSet _sumChannels;
  float _sum[Chan_Last + 1];
  void generateSum(const Tile& tile, ChannelMask channels);
  Lock _sumLock;
public:

  Convolve(Node*);
  void _validate(bool);
  void _request(int, int, int, int, ChannelMask, int);
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Description d;

  void engine(int y, int x, int r, ChannelMask channels, Row& row);
};

static Iop* Convolve_c(Node* node) { return new NukeWrapper(new Convolve(node)); }
const Iop::Description Convolve::d(CLASS, "Filter/Convolve", Convolve_c);

Convolve::Convolve(Node* node)
  : Iop(node),
  K_normalize( true ),
  filterWidth( 0 ),
  filterHeight( 0 ),
  channel(Chan_Black)
{
  inputs(2);
}

void Convolve::knobs(Knob_Callback f)
{
  Input_Channel_knob(f, &channel, 1, 0, "channel");
  Tooltip(f, "Use this channel from A input as the convolution matrix. "
             "If this is turned off, each output channel uses the corresponding "
             "channel from the A input.");
  Bool_knob(f, &K_normalize, "normalize", "Normalize");
  Tooltip(f, "Divide the result by the sum of all the numbers in the "
             "convolution matrix from A.");
}

void Convolve::_validate(bool for_real)
{
  input0().validate(for_real);
  info_ = input0().info();
  input1().validate(for_real);
  filterWidth = input1().w();
  filterHeight = input1().h();
  info_.clipmove(-filterWidth / 2, -filterHeight / 2, (filterWidth - 1) / 2, (filterHeight - 1) / 2);
  _sumChannels.clear();
}

void Convolve::generateSum(const Tile& tile, ChannelMask channels)
{
  Guard guard(_sumLock);

  DD::Image::ChannelSet newChannels = channels;
  newChannels -= _sumChannels;

  if (newChannels.empty()) {
    return;
  }

  memset(_sum, 0, sizeof(float) * (Chan_Last + 1));
  const int xStart = tile.x();
  const int xEnd = tile.r();
  ChannelSet toProcess = channel ? channel : channels;
  foreach (z, toProcess) {
    if (!(tile.channels() & z)) {
      _sum[z] = 1.0f;
      continue;
    }
    for (int y = tile.y(); y < tile.t(); ++y) {
      const float* filterptr = tile[z][y];
      for (int i = xStart; i < xEnd; ++i) {
        _sum[z] += filterptr[i];
      }
    }
  }
  if (channel) {
    foreach (z, channels) {
      if (z == channel)
        continue;
      _sum[z] = _sum[channel];
    }
  }
  _sumChannels = channels;
}

void Convolve::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{
  // always get the entire filter:
  input(1)->request(input(1)->x(), input(1)->y(), input(1)->r(), input(1)->t(),
                    channel ? channel : channels, count);

  x -= (filterWidth - 1) / 2;
  r += (filterWidth) / 2;
  y -= (filterHeight - 1) / 2;
  t += (filterHeight) / 2;

  input(0)->request(x, y, r, t, channels, count);

  _sumChannels.clear();
}

static size_t GetFloatAlignOffset(const float* buffer)
{
  size_t alignedStart = (size_t)buffer;
  size_t offset = alignedStart & 15;
  if ( 0 == offset ) {
    return 0;
  }
  
  size_t startBit = 16 - offset;
  size_t startFloat = startBit / 4;
  return startFloat;
}

static void FnConvolve(float* outptr, const float* inptr, float filterValue, int start, int end)
{
#if 0
  for (int xx = start; xx < end; ++xx) {
    outptr[xx] += inptr[xx] * filterValue;
  }
  return;
#endif

  int i = start;
#ifndef FN_PROCESSOR_PPC
  size_t startFloat = GetFloatAlignOffset(&outptr[start]) + start;

  for (; i < int(startFloat); ++i) {
    outptr[i] += inptr[i] * filterValue;
  }

  int lastValToCpy = ((end - startFloat) / 4) * 4 + startFloat;
  __m128 f = _mm_load_ps1(&filterValue);
  for (; i < lastValToCpy; i += 4) {
    __m128 input = _mm_loadu_ps(&inptr[i]);
    __m128 output = _mm_load_ps(&outptr[i]);
    output = _mm_add_ps(output, _mm_mul_ps(input, f));
    _mm_store_ps(&outptr[i], output);
  }
#endif
  for (; i < end; ++i) {
    outptr[i] += inptr[i] * filterValue;
  }
}


void Convolve::engine(int y, int x, int r, ChannelMask channels, Row& row)
{

  // Get the entire convolution matrix:
  Tile tile(input1(), channel ? channel : channels);
  // If aborted is true, the tile is no good, so quit without looking at it:
  if (aborted())
    return;

  // Account for filter width and height when processing pixel at the edges.
  // This is consistent with the logic in DD::Image::Convolve and works correctly
  // whether a filter dimension is odd or even.
  const int leftOffset = (filterWidth - 1) / 2;
  const int rightOffset = (filterWidth) / 2;
  const int bottomOffset = (filterHeight - 1) / 2;

  Row inrow( x - leftOffset, r + rightOffset );

  float* outptrs[Chan_Last + 1];
  foreach (z, channels) {
    outptrs[z] = row.writable(z);
    memset(outptrs[z] + x, 0, (r - x) * sizeof(float));
  }

  const int fx0 = tile.x();
  const int fxr = tile.r();
  for (int Y = 0; Y < tile.h(); Y++) {
    const int fy = tile.t() - Y - 1;
    input0().get( y - bottomOffset + Y, x - leftOffset, r + rightOffset, channels, inrow );
    foreach (z, channels) {
      Channel z1 = channel ? channel : z;
      if (!(tile.channels() & z1)) {
        row.erase(z);
        continue;
      }
      const float* filterptr = tile[z1][fy];
      if (!inrow.is_zero(z)) {
        float* outptr = outptrs[z];
        const float* inptr = inrow[z] - leftOffset;
        for (int counter = fxr - 1; counter >= fx0; --counter) {
          float f = filterptr[counter];
          if (f) {
            //Attempt to hand SSE this inner loop.
            FnConvolve(outptr, inptr, f, x, r);
            //Old version is in here.
            //for (int xx=x; xx<r; ++xx)
            //{
            //  outptr[xx] += inptr[xx] * f;
            //}
          }
          inptr++;
        }
      }
    }
    if (aborted())
      return;
  }
  if (K_normalize) {
    generateSum(tile, channels);
    foreach (z, channels) {
      float f = 1 / _sum[z];
      if (f != 1) {
        float* outptr = outptrs[z];
        for (int xx = x; xx < r; xx++)
          outptr[xx] *= f;
      }
    }
  }
}
