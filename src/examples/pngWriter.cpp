// pngWriter.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "png.h"
#include "DDImage/DDWindows.h"
#undef FAR  // suppress warning in zconf.h caused by windows.h
#include "DDImage/FileWriter.h"
#include "DDImage/Row.h"
#include "DDImage/ARRAY.h"
#include "DDImage/Knobs.h"

using namespace DD::Image;


class pngWriter : public FileWriter
{

protected:
public:
  int datatype;
  pngWriter(Write* iop) : FileWriter(iop), datatype(0) {}
  ~pngWriter() {}
  void execute();
  static const Writer::Description d;

  void knobs(Knob_Callback f)
  {
    static const char* const dtypes[] = { "8 bit", "16 bit", 0 };
    Enumeration_knob(f, &datatype, dtypes, "datatype", "data type");
  }

  const char* help() { return "Portable Network Graphics format"; }
};

static Writer* build(Write* iop) { return new pngWriter(iop); }
const Writer::Description pngWriter::d("png\0", build);

int color_type_lookup[] = {
  PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA,
  PNG_COLOR_TYPE_RGB, PNG_COLOR_TYPE_RGB_ALPHA
};


void pngWriter::execute()
{
  if (!open())
    return;
  int wdt = width(), hgt = height(), depth = 0;
  Channel ch[4]; // channel lookup

  // find the channels that we want to write (4 max, mapping to RGBA)
  depth = iop->depth();
  if (depth > 4)
    depth = 4;
  for (int i = 0; i < 4; i++)
    ch[i] = iop->channel_written_to(i);
  ChannelSet channels = channel_mask(depth);

  png_struct* png_ptr =
    png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_info* info_ptr =
    png_ptr ? png_create_info_struct (png_ptr) : 0;
  if (!png_ptr || !info_ptr) {
    iop->error("Failed to allocate png structures");
    png_destroy_write_struct (&png_ptr, &info_ptr);
    return;
  }

  // allocate everything else before the setjmp!
  input0().request(0, 0, width(), height(), channels, 1);
  Row row(0, wdt);
  ARRAY(png_byte, png_pixels, (datatype ? 2 : 1) * wdt * depth);

  if (!setjmp(png_jmpbuf(png_ptr))) {

    png_init_io (png_ptr, (FILE*)file);

    png_set_IHDR (png_ptr, info_ptr, wdt, hgt, datatype ? 16 : 8,
                  color_type_lookup[depth - 1],
                  PNG_INTERLACE_NONE,
                  PNG_COMPRESSION_TYPE_BASE,
                  PNG_FILTER_TYPE_BASE);

    // write the file header information
    png_write_info (png_ptr, info_ptr);

    // Read each row and tell png to write it out:
    for (int y = 0; y < hgt; y++) {
      iop->status(double(y) / height());
      get(hgt - y - 1, 0, wdt, channels, row);
      const float* alpha = depth > 3 ? row[ch[3]] : 0;
      if (aborted())
        break;
      if (datatype) {
        U16* buffer16 = (U16*)(&png_pixels[0]);
        for (int i = 0; i < depth; i++)
          to_short(i, buffer16 + i, row[ch[i]], alpha, wdt, 16, depth);
        tomsb(buffer16, wdt * depth);
      }
      else {
        unsigned char* buffer8 = png_pixels;
        for (int i = 0; i < depth; i++)
          to_byte(i, buffer8 + i, row[ch[i]], alpha, wdt, depth);
      }
      png_write_row(png_ptr, png_pixels);
    }

    /* write the additional chuncks to the PNG file (not really needed) */
    if (!aborted())
      png_write_end (png_ptr, info_ptr);

  }
  else {   // longjmp to here from png library:

    iop->error("Error from libpng");

  }

  /* clean up after the write, and free any memory allocated */
  png_destroy_write_struct (&png_ptr, &info_ptr);
  close();
}

class png16Writer : public pngWriter
{
public:
  png16Writer(Write* iop) : pngWriter(iop) { datatype = 1; }
  static const Writer::Description d;
};

static Writer* build16(Write* iop) { return new png16Writer(iop); }
const Writer::Description png16Writer::d("png16\0", build16);
