// tiffReader.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

/* Reads TIFF files using the public domain libtiff.

   libtiff understands a lot of the data formats, but only outputs
   srgb 8-bit converted colors. We use this interface if we cannot figure
   out anything better, but this recognizes all the 16 bit and higher
   forms of data where we have test images and directly converts the
   uncompressed tiff data to Nuke floating point.

   This is an example of a file reader that is not a subclass of
   FileReader. Instead this uses the library's reader functions and
   a single lock so that multiple threads do not crash the library.

   This file also contains the TiffWriter, because libtiff has a
   single error message callback pointer and thus these both need
   to be able to see and use that.
 */

#include "DDImage/DDWindows.h"
#include "tiffio.h"
#include "tiff.h"
#include <assert.h>
#ifdef _WIN32
  #include <io.h>
#endif
#include "DDImage/Reader.h"
#include "DDImage/Row.h"
#include "DDImage/Thread.h"
#include "DDImage/DDString.h"
#include "DDImage/Memory.h"
#include "DDImage/Knobs.h"
#include "DDImage/Read.h"
#include "DDImage/MetaData.h"
#include "DDImage/Reader.h"

using namespace DD::Image;

#define DEFAULT_RESOLUTION 72
#define DEFAULT_RESOLUTION_UNIT 1

#define STRIP_SIZE (1 << 17)

#undef TIFF_UPSIDEDOWN_OPTION

class TiffReaderFormat : public ReaderFormat
{

  friend class TiffReader;

#ifdef TIFF_UPSIDEDOWN_OPTION
  bool upsideDown;
#endif

public:
  TiffReaderFormat()
  {
#ifdef TIFF_UPSIDEDOWN_OPTION
    upsideDown = false;
#endif
  }

  void knobs(Knob_Callback c)
  {
#ifdef TIFF_UPSIDEDOWN_OPTION
    Bool_knob(c, &upsideDown, "upside-down");
#endif
  }

  void append(Hash& hash)
  {
#ifdef TIFF_UPSIDEDOWN_OPTION
    hash.append(upsideDown);
#endif
  }
};

class TiffReader : public Reader
{

  TIFF* tif;
  Lock lock;
  uint16 bitspersample;
  uint16 samplesperpixel;
  uint16 planarconfig;
  uint16 orientation;
  uint32 rowsperstrip;
  uint32* buffer;
  size_t buffersize;
  int strip_number; // which strip is currently read
  unsigned stripsize; // size of one channel of pixels

#ifdef TIFF_UPSIDEDOWN_OPTION
  bool upsideDown;
#endif

  MetaData::Bundle _meta;
  const MetaData::Bundle& fetchMetaData(const char* key)
  {
    return _meta;
  }

  void failure(const char* fmt, unsigned value);
  void liberror();
  // DDImage/Memory api:

  int weight() const { return 100; }

  static int weightCallback(void* user_data)
  {
    return ((TiffReader*)user_data)->weight();
  }

  bool free(size_t amount_to_free)
  {
    // trylock must be used as this may be called from inside
    // the Memory::allocate() inside the locked part of engine():
    if (!lock.trylock())
      return false;
    if (!buffer) { lock.unlock();
                   return false; }
    Memory::deallocate(buffer, buffersize);
    buffer = 0;
    lock.unlock();
    return true;
  }

  static bool freeCallback(void* user_data, size_t amount_to_free)
  {
    return ((TiffReader*)user_data)->free(amount_to_free);
  }

  void info(std::ostream& o, const void* restrict_to)
  {
    if (!buffer)
      return;
    if (restrict_to && iop->node() != (const Node*)restrict_to)
      return;
    iop->print_name(o);
    o << ": ";
    Memory::print_bytes(o, buffersize * 4);
    if (planarconfig) {
      o << " tiff reader strip "
        << samplesperpixel * (bitspersample == 32 ? 4 : bitspersample > 8 ? 2 : 1) << 'x'
        << width() << 'x' << rowsperstrip;
    }
    else {
      o << " tiff reader buffer 4x" << width() << 'x' << height();
    }
    o << " w " << weight() << std::endl;
  }

  static void infoCallback(void* user_data, std::ostream& o, const void* restrict_to)
  {
    ((TiffReader*)user_data)->info(o, restrict_to);
  }

public:

  TiffReader(Read*, int fd);
  ~TiffReader();
  void engine(int y, int x, int r, ChannelMask, Row &);
  void open();
  static const Reader::Description d;

};

// Test to see if the block is the header of a tiff file:
static bool test(int fd, const unsigned char* block, int length)
{
  // test for big-endian:
  if (block[0] == 'M' && block[1] == 'M' && block[2] == 0 && block[3] == 42)
    return true;
  // test for little-endian:
  if (block[0] == 'I' && block[1] == 'I' && block[2] == 42 && block[3] == 0)
    return true;
  return false;
}

static Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
  return new TiffReader(iop, fd);
}

static ReaderFormat* buildformat(Read* iop)
{
  return new TiffReaderFormat();
}

// If you uncomment the pritnf you can make sure the plugin is loaded:
static inline const char* foo()
{
  //  printf("TiffReader constructed\n");
  return "tiff\0tif\0tiff16\0tif16\0ftif\0ftiff\0";
}

const Reader::Description TiffReader::d(foo(), build, test, buildformat);

static char* errorbuffer;
static void errorhandler(const char* module, const char* fmt, va_list ap)
{
  if (!errorbuffer)
    errorbuffer = new char[1024];
  vsnprintf(errorbuffer, 1024, fmt, ap);
}

void TiffReader::failure(const char* fmt, unsigned value)
{
  if (bitspersample <= 8)
    return;                       // it is ok
  fprintf(stderr, "%s : can't read %d BitsPerSample because ",
          filename(), bitspersample);
  fprintf(stderr, fmt, value);
  fprintf(stderr, ", only top 8 bits will be read\n");
  fflush(stderr);
}

#define USE_OUR_DECODER 1

template<class T, class V>
static bool getMetaData(TIFF* metatif, const TIFFFieldInfo* fi, MetaData::Bundle& metadata, const std::string& key)
{
  if (fi->field_readcount == TIFF_VARIABLE2 || fi->field_readcount == TIFF_VARIABLE || fi->field_readcount > 1) {

    size_t count = 0;
    T* data;

    if (fi->field_readcount == TIFF_VARIABLE) {
      uint16 gotcount = 0;
      TIFFGetField(metatif, fi->field_tag, &gotcount, &data);
      count = gotcount;
    }
    else if (fi->field_readcount == TIFF_VARIABLE2) {
      uint32 gotcount = 0;
      TIFFGetField(metatif, fi->field_tag, &gotcount, &data);
      count = gotcount;
    }
    else {
      TIFFGetField(metatif, fi->field_tag, &data);
      count = fi->field_readcount;
    }

    std::vector<V> values;
    values.resize(count);
    for (unsigned i = 0; i < count; i++) {
      values[i] = data[i];
    }

    metadata.setData(key, values);
    return true;
  }
  else if (fi->field_readcount == 1) {
    T data;
    TIFFGetField(metatif, fi->field_tag, &data);
    metadata.setData(key, V(data));
    return true;
  }
  return false;
}

static bool getMetaDataString(TIFF* metatif, const TIFFFieldInfo* fi, MetaData::Bundle& metadata, const std::string& key)
{
  if (fi->field_readcount > 1) {
    char* data;
    TIFFGetField(metatif, fi->field_tag, &data);
    metadata.setData(key, data);
    return true;
  }
  return false;
}

static void fetchTiffMetaData(const char* filename, MetaData::Bundle& metadata)
{
  TIFF* metatif = TIFFOpen(filename, "r");
  if (metatif) {
    uint32 exif_offset;
    if (TIFFGetField(metatif, TIFFTAG_EXIFIFD, &exif_offset)) {
      TIFFReadEXIFDirectory(metatif, exif_offset);
      int cnt = TIFFGetTagListCount(metatif);
      for (int i = 0; i < cnt; i++) {
        ttag_t tag = TIFFGetTagListEntry(metatif, i);
        const TIFFFieldInfo* fi = TIFFFieldWithTag(metatif, tag);
        std::string exifName = MetaData::EXIF::EXIF_PREFIX + std::string("2/") + std::string(fi->field_name);

        bool usedMetaData = false;

        switch (fi->field_type) {
          case TIFF_BYTE: {
            usedMetaData = getMetaData<uint8, int>(metatif, fi, metadata, exifName);
            break;
          }
          case TIFF_UNDEFINED: {
            usedMetaData = getMetaData<uint8, int>(metatif, fi, metadata, exifName);
            break;
          }

          case TIFF_ASCII: {
            usedMetaData = getMetaDataString(metatif, fi, metadata, exifName);
            break;
          }
          case TIFF_SHORT: {
            usedMetaData = getMetaData<uint16, int>(metatif, fi, metadata, exifName);
            break;
          }
          case TIFF_LONG: {
            usedMetaData = getMetaData<uint32, double>(metatif, fi, metadata, exifName);
            break;
          }
          case TIFF_SBYTE: {
            usedMetaData = getMetaData<int8, int>(metatif, fi, metadata, exifName);
            break;
          }
          case TIFF_SSHORT: {
            usedMetaData = getMetaData<int16, int>(metatif, fi, metadata, exifName);
            break;
          }
          case TIFF_SLONG: {
            usedMetaData = getMetaData<int32, int>(metatif, fi, metadata, exifName);
            break;
          }
          case TIFF_SRATIONAL:
          case TIFF_RATIONAL:
          case TIFF_FLOAT: {
            usedMetaData = getMetaData<float, double>(metatif, fi, metadata, exifName);
            break;
          }
          case TIFF_DOUBLE: {
            usedMetaData = getMetaData<double, double>(metatif, fi, metadata, exifName);
            break;
          }
          default:
            ;

        }
      }
    }
    TIFFClose(metatif);
  }
}


TiffReader::TiffReader(Read* r, int fd) : Reader(r)
{
#ifdef TIFF_UPSIDEDOWN_OPTION
  TiffReaderFormat* trf = dynamic_cast<TiffReaderFormat*>(r->handler());
  if (trf) {
    upsideDown = trf->upsideDown;
  }
  else {
    upsideDown = false;
  }
#endif

  buffer = 0;
  Memory::register_user(this, weightCallback, freeCallback, infoCallback);

  TIFFSetErrorHandler(errorhandler);
  TIFFSetWarningHandler(0);
  lseek(fd, 0L, SEEK_SET);
  if (errorbuffer)
    errorbuffer[0] = 0;
  tif = TIFFFdOpen(fd, filename(), "r");
  if (!tif) {
    liberror();
    return;
  }
  unsigned w;
  TIFFGetFieldDefaulted(tif, TIFFTAG_IMAGEWIDTH, &w);
  unsigned h;
  TIFFGetFieldDefaulted(tif, TIFFTAG_IMAGELENGTH, &h);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesperpixel);
  set_info(w, h, samplesperpixel);
  TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitspersample);
  TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rowsperstrip);
  if (rowsperstrip > h)
    rowsperstrip = h;
  uint16 photometric;
  if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric))
    photometric = PHOTOMETRIC_RGB;
  TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &orientation);
  TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planarconfig);
  uint16 compression;
  TIFFGetFieldDefaulted(tif, TIFFTAG_COMPRESSION, &compression);
  uint16 sampleformat;
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleformat);

  if (sampleformat == SAMPLEFORMAT_IEEEFP && bitspersample == 32) {
    _meta.setData(MetaData::DEPTH, MetaData::DEPTH_FLOAT);
  }
  else if (sampleformat == SAMPLEFORMAT_IEEEFP && bitspersample == 16) {
    _meta.setData(MetaData::DEPTH, MetaData::DEPTH_HALF);
  }
  else {
    _meta.setData(MetaData::DEPTH, MetaData::DEPTH_FIXED(bitspersample));
  }

  float xres = 0, yres = 0;

  TIFFGetField(tif, TIFFTAG_XRESOLUTION, &xres);
  TIFFGetField(tif, TIFFTAG_YRESOLUTION, &yres);

  if (xres != 0 && yres != 0) {
    _meta.setData(MetaData::TIFF::TIFF_XRESOLUTION, xres);
    _meta.setData(MetaData::TIFF::TIFF_YRESOLUTION, yres);

    uint16 resunit = 0;
    TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
    _meta.setData(MetaData::TIFF::TIFF_RESOLUTIONUNIT, resunit);
  }

  fetchTiffMetaData(filename(), _meta);

  //    char* c = 0; TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &c);
  //    info_.comment = c;
  // set the default colourspace:
  lut_ = LUT::getLut(bitspersample > 16 ? LUT::FLOAT : (bitspersample > 8 ? LUT::INT16 : LUT::INT8));
#if USE_OUR_DECODER
  if (TIFFIsTiled(tif)) {
    failure("TIFF is tiled", 0);
  }
  else if (photometric != PHOTOMETRIC_RGB && photometric != PHOTOMETRIC_MINISBLACK) {
    failure("Photometric is %d", photometric);
  #if 0
  }
  else if (compression == COMPRESSION_PACKBITS && rowsperstrip > 1) {
    // TIFFReadEncodedStrip() fails for some compressions, possibly due
    // to bugs in it? The following compreesions have been encountered
    // and are known to work:
    // COMPRESSION_NONE
    // COMPRESSION_LZW
    // Actually PACKBITS appears to work, maybe I had a bad file.
    failure("Compression is %d", compression);
  #endif
  }
  else if (sampleformat == SAMPLEFORMAT_IEEEFP && bitspersample != 32
           && bitspersample != 16 /* for bad Terragen files! */) {
    failure("it has %d-bit floating point data", bitspersample);
  }
  else if (sampleformat != SAMPLEFORMAT_IEEEFP &&
           (bitspersample > 16 || bitspersample < 8)) {
    failure("it has %d-bit unsigned data", bitspersample);
  }
  else {
    // we can use our decoder:
    info_.ydirection(((orientation - 1) & 2) ? -1 : 1);
    strip_number = -1;
    return;
  }
#endif
  planarconfig = 0; // indicates that library should be used
  info_.ydirection(0);
  //info_.slowness = 0; // turn off any buffering
}

void TiffReader::liberror()
{
  iop->error(errorbuffer && *errorbuffer ? errorbuffer : "libtiff error");
}

// delay anything unneeded for info_ until this is called:
// Unfortunately libtiff really wants to read everything at once...
void TiffReader::open() { }

TiffReader::~TiffReader()
{
  free(0);
  Memory::unregister_user(this);
  if (tif) {
#if DEBUG
    assert(lock.trylock());
    TIFFClose(tif);
    lock.unlock();
#else
    TIFFClose(tif);
#endif
  }
}

void TiffReader::engine(int y, int x, int r, ChannelMask channels, Row& row)
{

#ifdef TIFF_UPSIDEDOWN_OPTION
  if (upsideDown == 1) {
    y = height() - 1 - y;
  }
#endif

  Guard guard(lock);

  if (!planarconfig) {
    if (!buffer) {
      buffersize = width() * height();
      buffer = Memory::allocate<uint32>( buffersize );
      if (!TIFFReadRGBAImage(tif, width(), height(), buffer))
        liberror();
    }
    const uchar* ROW = (uchar*)(buffer + y * width() + x);
    foreach (z, channels) {
#if __BIG_ENDIAN__
      const uchar* ALPHA = ROW;
      const uchar* FROM = ROW + 4 - z;
#else
      const uchar* ALPHA = ROW + 3;
      const uchar* FROM = ROW + z - 1;
#endif
      from_byte(z, row.writable(z) + x, FROM, ALPHA, r - x, 4);
    }
    return;
  }

  // Use our converter.
  // First find and load the strip containing this row:

  if (!buffer) {
    stripsize = rowsperstrip * width();
    if (bitspersample == 32)
      stripsize *= 4;
    else if (bitspersample > 8)
      stripsize *= 2;
    buffersize = (samplesperpixel * stripsize + 3) / 4;
    buffer = Memory::allocate<uint32>( buffersize );
    strip_number = -1;
  }
  uchar* strip = (uchar*)buffer;

  // flip it if TOPLEFT or other top orientation:
  if (!((orientation - 1) & 2))
    y = height() - y - 1;

  // We must do each row in a strip in sequence:
  int sn = int(y / rowsperstrip);
  if (sn != strip_number) {
    strip_number = sn;
    if (planarconfig == 2) {
      for (int z = 0; z < samplesperpixel; z++)
        if (TIFFReadEncodedStrip(tif, sn * samplesperpixel + z,
                                 (void*)(strip + stripsize * z),
                                 (long)stripsize) < 0) {
          liberror();
          break;
        }

    }
    else {
      if (TIFFReadEncodedStrip(tif, sn, (void*)strip,
                               (long)(stripsize * samplesperpixel)) < 0)
        liberror();
    }
  }

  y %= rowsperstrip;

  if (bitspersample <= 8) {
    if (planarconfig == 2) {
      const uchar* ALPHA =
        (samplesperpixel > 3) ? strip + (3 * rowsperstrip + y) * width() + x : 0;
      foreach (z, channels) {
        const uchar* FROM = strip + ((z - 1) * rowsperstrip + y) * width() + x;
        from_byte(z, row.writable(z) + x, FROM, ALPHA, r - x);
      }
    }
    else {
      const uchar* ALPHA =
        (samplesperpixel > 3) ? strip + (y * width() + x) * samplesperpixel + 3 : 0;
      foreach (z, channels) {
        const uchar* FROM = strip + (y * width() + x) * samplesperpixel + z - 1;
        from_byte(z, row.writable(z) + x, FROM, ALPHA, r - x, samplesperpixel);
      }
    }
  }
  else if (bitspersample <= 16) {
    // 16-bits per sample
    if (planarconfig == 2) {
      const U16* ALPHA =
        (samplesperpixel > 3) ? (U16*)strip + (3 * rowsperstrip + y) * width() + x : 0;
      foreach (z, channels) {
        const U16* FROM = (U16*)strip + ((z - 1) * rowsperstrip + y) * width() + x;
        from_short(z, row.writable(z) + x, FROM, ALPHA, r - x, bitspersample);
      }
    }
    else {
      const U16* ALPHA =
        (samplesperpixel > 3) ? (U16*)strip + (y * width() + x) * samplesperpixel + 3 : 0;
      foreach (z, channels) {
        const U16* FROM = (U16*)strip + (y * width() + x) * samplesperpixel + z - 1;
        from_short(z, row.writable(z) + x, FROM, ALPHA, r - x, bitspersample, samplesperpixel);
      }
    }
  }
  else {
    // Floating-point samples
    if (planarconfig == 2) {
      const float* ALPHA =
        (samplesperpixel > 3) ? (float*)strip + (3 * rowsperstrip + y) * width() + x : 0;
      foreach (z, channels) {
        const float* FROM = (float*)strip + ((z - 1) * rowsperstrip + y) * width() + x;
        from_float(z, row.writable(z) + x, FROM, ALPHA, r - x);
      }
    }
    else {
      const float* ALPHA =
        (samplesperpixel > 3) ? (float*)strip + (y * width() + x) * samplesperpixel + 3 : 0;
      foreach (z, channels) {
        const float* FROM = (float*)strip + (y * width() + x) * samplesperpixel + z - 1;
        from_float(z, row.writable(z) + x, FROM, ALPHA, r - x, samplesperpixel);
      }
    }
  }
}

////////////////////////////////////////////////////////////////
// TiffWriter.C:

// Write tiff files. This is an example of a file writer that uses
// a separate library. In this case the sepearte library requires it's
// own fd, and unfortunately it closes it (it shouldn't because it did
// not open it). I use the dup() call to get around this so that errors
// are not reported by the base class upon close(). Another way around
// this would be to not use the base class, but that would require
// duplicating the useful .tmp behavior and Windoze drive letters here.

// Also this is a demonstration of how you can make different extensions
// produce different behavior by using a subclass.

// Note on compression:
//
// Unfortunatly many libtiff's are compiled without LZW support
// due to Unisys's patent, and rather than ignore this setting they
// produce an error message and fail. So we cannot turn it on.
// You could turn this on by setting compression = COMPRESSION_LZW.
//
// "Deflate" compression is much better than LZW and tests indicate
// the the majority of programs can read it. So we turn this on by
// default.
//
// Only other setting known to work is COMPRESSION_NONE.

#include "DDImage/FileWriter.h"
#include "DDImage/ARRAY.h"

static const char* const cnames[] = {
  "none", "PackBits", "LZW", "Deflate", 0
};
uint16 ctable[] = {
  COMPRESSION_NONE,
  COMPRESSION_PACKBITS,
  COMPRESSION_LZW,
  COMPRESSION_DEFLATE
};

class TiffWriter : public FileWriter
{
  void liberror();
public:
  int datatype;
  int compress; // the enumeration, not the tiff constant!

  TiffWriter(Write* iop) : FileWriter(iop)
  {
    datatype = 0;
    compress = 3; // deflate
  }
  ~TiffWriter() {}
  void execute();
  void knobs(Knob_Callback f)
  {
    static const char* const dnames[] = {
      "8 bit", "16 bit", "32 bit float", NULL
    };
    Enumeration_knob(f, &datatype, dnames, "datatype", "data type");
    Enumeration_knob(f, &compress, cnames, "compression");
  }
  LUT* defaultLUT() const
  {
    return LUT::getLut(datatype == 2 ? LUT::FLOAT : (datatype ? LUT::INT16 : LUT::INT8));
  }
  static const Writer::Description d;

  const char* help() { return "tiff"; }

};

static Writer* build(Write* iop) { return new TiffWriter(iop); }
const Writer::Description TiffWriter::d("tiff\0tif\0", build);

void TiffWriter::liberror()
{
  iop->error(errorbuffer ? errorbuffer : "libtiff error");
}

void TiffWriter::execute()
{

  TIFFSetErrorHandler(errorhandler);
  TIFFSetWarningHandler(0);

  if (!open())
    return;

  int samplesperpixel = num_channels();
  int imagewidth = this->width();
  int imagelength = this->height();

  TIFF* tif = TIFFFdOpen(dup(fileno((FILE*)file)), filename(), "w"); // "wl" ?
  if (!tif) {
    liberror();
    return;
  }

  ChannelSet channels = channel_mask(samplesperpixel);
  input0().request(0, 0, width(), height(), channels, 1);

  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32)imagewidth);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32)imagelength);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, (uint16)samplesperpixel);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, datatype == 2 ? 32 : datatype ? 16 : 8);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, (uint16)1);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,
               samplesperpixel > 1 ? (uint16)PHOTOMETRIC_RGB :
               (uint16)PHOTOMETRIC_MINISBLACK);

  MetaData::Bundle meta = input0().fetchMetaData(NULL);
  if (meta.getData(MetaData::TIFF::TIFF_XRESOLUTION) != NULL &&
      meta.getData(MetaData::TIFF::TIFF_YRESOLUTION) != NULL &&
      meta.getData(MetaData::TIFF::TIFF_RESOLUTIONUNIT) != NULL) {
    TIFFSetField(tif, TIFFTAG_XRESOLUTION, (double)meta.getDouble(MetaData::TIFF::TIFF_XRESOLUTION));
    TIFFSetField(tif, TIFFTAG_YRESOLUTION, (double)meta.getDouble(MetaData::TIFF::TIFF_YRESOLUTION));
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, (uint16)meta.getDouble(MetaData::TIFF::TIFF_RESOLUTIONUNIT));
  }
  else {
    TIFFSetField(tif, TIFFTAG_XRESOLUTION, (double)DEFAULT_RESOLUTION);
    TIFFSetField(tif, TIFFTAG_YRESOLUTION, (double)DEFAULT_RESOLUTION);
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, (uint16)DEFAULT_RESOLUTION_UNIT);
  }

  int bytespersample = 0;
  if (datatype == 2) {
    bytespersample = sizeof(float);
  }
  else if (datatype == 1) {
    bytespersample = sizeof(unsigned short);
  }
  else if (datatype == 0) {
    bytespersample = sizeof(unsigned char);
  }

  int bytesperline = bytespersample * width() * samplesperpixel;
  uint32 rowsperstrip = (uint32)((STRIP_SIZE) / bytesperline);
  if (rowsperstrip < 1)
    rowsperstrip = 1;

  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, rowsperstrip);

  if (compress)
    TIFFSetField(tif, TIFFTAG_COMPRESSION, ctable[compress]);

  if (datatype == 2) { // float
    ARRAY(float, buffer, samplesperpixel * imagewidth);

    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);

    Row row(0, width());
    for (unsigned y = 0; y < (unsigned)height(); y++) {
      iop->status(double(y) / height());
      get(height() - y - 1, 0, width(), channels, row);
      const float* alpha = samplesperpixel > 3 ? row[channel(3)] : 0;
      if (aborted())
        break;

      for (int i = 0; i < samplesperpixel; i++) {
        to_float(i, buffer + i, row[channel(i)], alpha, width(), samplesperpixel);
      }

      if (TIFFWriteScanline(tif, (void*)buffer, y, 0) < 0) {
        liberror();
        break;
      }
    }

  }
  else if (datatype) {   // 16 bit
    ARRAY(U16, buffer, samplesperpixel * imagewidth);
    ARRAY(U16, tempbuffer, imagewidth);
    Row row(0, width());
    for (unsigned y = 0; y < (unsigned)height(); y++) {
      iop->status(double(y) / height());
      get(height() - y - 1, 0, width(), channels, row);
      const float* alpha = samplesperpixel > 3 ? row[channel(3)] : 0;
      if (aborted())
        break;
      for (int i = 0; i < samplesperpixel; i++) {
        // unfortunately Writer lacks a "delta" version of to_short
        // so I have to interlace it here...
        to_short(i, tempbuffer, row[channel(i)], alpha, width(), 16);
        U16* TO = buffer + i;
        U16* FROM = tempbuffer;
        for (int x = width(); x--;) {
          *TO = *FROM++;
          TO += samplesperpixel;
        }
      }
      if (TIFFWriteScanline(tif, (void*)buffer, y, 0) < 0) {
        liberror();
        break;
      }
    }
  }
  else {   // 8 bit
    ARRAY(uchar, buffer, samplesperpixel * imagewidth);
    Row row(0, width());
    for (unsigned y = 0; y < (unsigned)height(); y++) {
      iop->status(double(y) / height());
      get(height() - y - 1, 0, width(), channels, row);
      const float* alpha = samplesperpixel > 3 ? row[channel(3)] : 0;
      if (aborted())
        break;
      for (int i = 0; i < samplesperpixel; i++)
        to_byte(i, buffer + i, row[channel(i)], alpha, width(), samplesperpixel);
      if (TIFFWriteScanline(tif, (void*)buffer, y, 0) < 0) {
        liberror();
        break;
      }
    }
  }

  TIFFClose(tif);
  close();
}

class tiff16Writer : public TiffWriter
{
public:
  tiff16Writer(Write* iop) : TiffWriter(iop) { datatype = 1; }
  static const Writer::Description d;
};

static Writer* build16(Write* iop) { return new tiff16Writer(iop); }
const Writer::Description tiff16Writer::d("tiff16\0tif16\0", build16);

// for floating point tiffs
class fTiffWriter : public TiffWriter
{
public:
  fTiffWriter(Write* iop) : TiffWriter(iop) { datatype = 2; }
  static const Writer::Description d;
};

static Writer* buildftiff(Write* iop) { return new fTiffWriter(iop); }
const Writer::Description fTiffWriter::d("ftiff\0ftif\0", buildftiff);

// end of tiffReader.C
