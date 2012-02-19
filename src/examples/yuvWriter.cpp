// yuvWriter.C

// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.
// Permission is granted to reuse portions or all of this code for the
// purpose of implementing Nuke plugins, or to demonstrate or document
// the methods needed to implemente Nuke plugins.

// Write "yuv" or "sdl" files, as used by the Abekas video recorders.
// This converts the Nuke linear data into Rec 601 encoding of Y'CBCR.

// This is also an example of a file-writing plugin.

#include "DDImage/FileWriter.h"
#include "DDImage/Row.h"
#include "DDImage/ARRAY.h"
#include "DDImage/Knobs.h"

using namespace DD::Image;

class yuvWriter : public FileWriter
{
  bool interlace;
public:
  yuvWriter(Write* iop, bool i) : FileWriter(iop), interlace(i) {}
  void execute();
  static const Writer::Description d;
  static const Writer::Description sdld;
  void knobs(Knob_Callback f);

  const char* help() { return "Raw 422 YCbCr files used by Abekas video recorders"; }

};

// This is a function called by the Writer_description to create an instance:
static Writer* build(Write* iop) { return new yuvWriter(iop, false); }

// The description has a null-separated list of possilble names to call
// this from, and a pointer to the build procedure.
//
// If the user types "name.yuv" or "yuv:name" they will cause an instance
// of this to be made to write the file.
const Writer::Description yuvWriter::d("yuv\0", build);

// We also support another file type called "sdl" which is the same data
// in interlaced order.  In order for Nuke to see this, a
// plugin named "sdlWriter" must be created that loads this one, usually
// by just putting "load yuvWriter" into a .tcl plugin:
static Writer* buildsdl(Write* iop) { return new yuvWriter(iop, true); }
const Writer::Description yuvWriter::sdld("sdl\0", buildsdl);

// Define extra knobs to be used when a yuv file is being written.
// In this case we define a help popup to describe the file, and a
// checkmark to turn the interlacing on/off.
void yuvWriter::knobs(Knob_Callback f)
{
  Bool_knob(f, &interlace, "interlaced");
}

// Write the integer portion of v as a byte and return an error diffusion
// Since Rec. 601 reserves codes 0 and 255 for synchronization signals we
// clamp to the 1-254 range:
static float write_error(unsigned char* p, float v)
{
  if (v <= 1) {
    *p = 1;
    return 0;
  }
  else if (v < 254) {
    int c = int(v + .5);
    *p = c;
    return v - c;
  }
  else {   // >= 254 and NaN
    *p = 254;
    return 0;
  }
}

// All the work is done by a single function called "execute". This must
// open and get data from the input image and write it to the file. It
// must periodically call status() and quit on errors so that an
// interactive user gets feedback:

void yuvWriter::execute()
{

  // yuv files have some limits on what they can write:
  if (width() != 720 || (height() != 486 && height() != 576)) {
    iop->error("Image size is %dx%d, must be 720x486 or 720x576",
               width(), height());
    return;
  }

  // This opens the file (the .tmp file, actually):
  if (!open())
    return;

  // channel_mask() will return the correct bitflags to pass to open to
  // account for how the user set the "channels to write" controls. You
  // pass it how many channels to write. Yuv can only write 3. If you
  // can write a varying number of channels use num_channels() to get
  // the number Nuke wants to write:
  ChannelSet channels = channel_mask(3);

  // We must now request the correct area from the input with an Iop::request
  // call. Usually the bounding box is 0,0, and the width and height, however
  // if the file can store the bounding box you might want to call open()
  // on the input and then get the info() area.
  input0().request(0, 0, width(), height(), channels, 1);

  // Temporary floating-point arrays:
  ARRAY(float, Rbuf, 720);
  ARRAY(float, Gbuf, 720);
  ARRAY(float, Bbuf, 720);

  Row row(0, width());

  // Now execute all the rows:
  for (int Y = 0; Y < height(); Y++) {

    iop->status(double(Y) / height()); // update progress indicator

    // Figure out the input line to get (many file formats require it
    // to flip upside down):
    int in_y = height() - Y - 1;
    if (interlace) {
      if (in_y >= height() / 2)
        in_y = (in_y - height() / 2) * 2 + 1;
      else
        in_y = in_y * 2;
    }

    // get the floating-point data:
    get(in_y, 0, width(), channels, row);

    // quit if there was an error:
    if (aborted())
      return;

    // Most file formats will convert() to 8 or 16-bit data.
    // For YUV I turn the data into floating
    // point values so the math for yuv can be done in floating point:
    to_float(0, Rbuf, row[channel(0)], 0, width());
    to_float(1, Gbuf, row[channel(1)], 0, width());
    to_float(2, Bbuf, row[channel(2)], 0, width());

    // these variables are actually error diffusion accumulators:
    float u = 0;
    float v = 0;
    float y = 0;

    const float* R = Rbuf;
    const float* G = Gbuf;
    const float* B = Bbuf;
    const float* END = R + width();
    // reuse the red buffers for the byte output:
#define OUTbuf ((unsigned char*)&(Rbuf[0]))
    unsigned char* out = OUTbuf;

    while (R < END) {

      /* first pixel gives Y and 0.5 of chroma */
      float r = *R++;
      float g = *G++;
      float b = *B++;

      float y1  = 255 * (.25679f * r  +  .504135f * g +  .0979f * b);
      float u1  = 255 * (-.07405f * r + - .145416f * g +  .219467f * b);
      float v1  = 255 * (.219513f * r + - .183807f * g + - .0357f * b);

      /* second pixel just yields a Y and 0.25 U, 0.25 V */
      r = *R++;
      g = *G++;
      b = *B++;

      float y2  = 255 * (.25679f * r  +  .504135f * g +  .0979f * b);
      float u2  = 255 * (-.07405f * r + - .145416f * g +  .219467f * b);
      float v2  = 255 * (.219513f * r + - .183807f * g + - .0357f * b);

      // write four bytes to the output buffer:
      u = write_error(out++, u + u1 + u2 + 128.0f);
      y = write_error(out++, y + y1 + 16.0f);
      v = write_error(out++, v + v1 + v2 + 128.0f);
      y = write_error(out++, y + y2 + 16.0f);
    }

    // write the output buffer to the file:
    write(OUTbuf, 2 * width());
  }

  // This closes the file and renames the .tmp one to the final name:
  close();
}
