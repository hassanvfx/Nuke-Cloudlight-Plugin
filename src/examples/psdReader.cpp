// psdReader.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

/*
** psd reader by Matthias Melcher
*/

#include "DDImage/Iop.h"
#include "DDImage/Reader.h"
#include "DDImage/Row.h"
#include "DDImage/ARRAY.h"
#include "DDImage/Thread.h"
#include "DDImage/DDString.h"
#include "DDImage/Knob.h"
#include <ctype.h>
#include <math.h>

using namespace DD::Image;

static unsigned readShort(FILE* file)
{
  U16 v;
  fread(&v, 2, 1, file);
  DD::Image::Reader::frommsb(&v, 1);
  return v;
}

static long readLong(FILE* file)
{
  U32 v;
  fread(&v, 4, 1, file);
  DD::Image::Reader::frommsb(&v, 1);
  return v;
}

class PSDLayer
{
public:
  char* name;
  long nChan;
  short chanID[32];
  long chanSize[32];
  long channelStart[32];
  Channel channelMap[32];
  int x, y, r, t;
  void print()
  {
#if 0
    printf("----- Layer %s -----\n", name);
    printf("x: %d, y: %d, r: %d, t: %d, w: %d, h:%d\n", x, y, r, t, r - x, t - y);
    for (int i = 0; i < nChan; i++) {
      printf("  psd chan %d, Nuke chan %d, start %d\n", i, channelMap[i], channelStart[i]);
    }
#endif
  }
};

class psdReader : public Reader
{
  FILE* file;
  int depth, width, height;
  int bpc;
  char* layername;
  long image_start;
  unsigned char* array;
  PSDLayer* layer;
  int nLayer;
  ChannelSet mask;
  Lock lock;

  MetaData::Bundle _meta;
  const MetaData::Bundle& fetchMetaData(const char* key)
  {
    return _meta;
  }

  bool getFileHeader()
  {
    fseek(file, 0, SEEK_SET);
    char buf[5] = { 0 };
    fread(buf, 1, 4, file);
    if (strncmp (buf, "8BPS", 4)) {
      iop->error("Not a psd file (needs \"8BPS\" in header)");
      return false;
    }
    short version = readShort(file);
    if (version != 1) {    // the only version that is documented by Adobe
      iop->error("psd version %d is not supported", version);
      return false;
    }
    fseek(file, 6, SEEK_CUR);
    depth  = readShort(file);
    height = readLong(file);
    width  = readLong(file);
    bpc = readShort(file);
    if (bpc != 8 && bpc != 16) {
      iop->error("psd bit depth of %d is not supported", bpc);
      return false;
    }
    short mode = readShort(file);

    _meta.setData(MetaData::DEPTH, MetaData::DEPTH_FIXED(bpc));

    if (mode == 1 && depth >= 1)
      return true;                            // grayscale
    if (mode == 3 && depth >= 3)
      return true;                            // RGB w/ or w/o alpha
    iop->error("psd mode %d with depth %d is not supported", mode, depth);
    // Other modes:
    // 0=bitmap, 2=indexed color, 4=cmyk, 7=multichannel, 8=duotone, 9=lab
    // 6 may also be duotone

    return false;
  }

  // ignore the color mode. No support for LUTs
  // If data was there, it would be a 768 byte lookup table for 256 colors
  bool getColorModeData()
  {
    long size = readLong(file);
    fseek(file, size, SEEK_CUR);
    return true;
  }

  // ignore these blocks for now
  bool getImageResources()
  {
    long size = readLong(file);
    long here = ftell(file);

    // This code is commented out as it had no effect
    // - the code after the block overrode all the seeks anyway
    /*for (long n = size; n > 8;) {
       char buf[4];
       fread(buf, 1, 4, file); // 8BIM
       readShort(file);        // ID - unused

       // Name - "Pascal-format string" (presumably one byte length + unterminated text data),
       // always an even length
       unsigned char nameLen = fgetc(file);
       nameLen = (nameLen+2) & 0xfe;
       fseek(file, nameLen-1, SEEK_CUR);

       long resSize = readLong (file);
       resSize = (resSize+1) & 0xfffffffe;
       long resHere = ftell(file);
       n -= resSize + nameLen + 10;
       //switch (id) {
       //  case 1005:
       //    getResolutionRsrc ();
       //    break;
       //  }
       fseek(file, resHere+resSize, SEEK_SET);
       }*/

    fseek(file, here + size, SEEK_SET);
    return true;
  }

  bool getLayerAndMaskInfo()
  {
    long size = readLong(file);
    long here = ftell(file);

    // Don't start reading layer & mask info if there isn't supposed to be any
    bool ret = true;
    if (size > 0) {
      ret = getLayerInfo();
      if (ret)
        ret = getMaskInfo();
    }

    //fseek(file, here+size, SEEK_SET);
    image_start = here + size;
    return ret;
  }

  bool getLayerInfo();

  bool getMaskInfo ()
  {
    long size = readLong(file);
    fseek(file, size, SEEK_CUR);
    return true;
  }

  void rleDecode(unsigned char* d, long len);
  void copyDecode(unsigned char* d, long len);
  void rleDecode(U16* d, long len);
  void copyDecode(U16* d, long len);
  void getImageData();

public:
  psdReader (Read*, int fd);
  ~psdReader ();
  void engine (int y, int x, int r, ChannelMask, Row &);
  void open ();
  static const Description d;
};

static Reader* build (Read* iop, int fd, const unsigned char* b, int n)
{
  return new psdReader (iop, fd);
}

static bool test (int fd, const unsigned char* block, int length)
{
  return strncmp((char*)block, "8BPS", 4) == 0;
}

const Reader::Description psdReader::d ("psd\0", build, test);

psdReader::psdReader (Read* r, int fd) : Reader (r), array(0), nLayer(0)
{

  file = fdopen(fd, "rb");
  depth = width = height = 0;
  layername = 0;
  layer = 0;

  if (!getFileHeader())
    return;
  if (!getColorModeData())
    return;
  if (!getImageResources())
    return;
  if (!getLayerAndMaskInfo())
    return;

  // add the base channels
  switch (depth) {
    case 1: mask = Mask_Red;
      break;
    case 2: mask = Mask_Red | Mask_Alpha;
      break;
    case 3: mask = Mask_RGB;
      break;
    default: mask = Mask_RGBA;
      break;
  }

  // now add channels for all extra layer in our image
  for (int i = 0; i < nLayer; i++) {
    PSDLayer& l = layer[i];
    if (l.nChan < 1)
      continue;
    // skip empty layers that seem to be common:
    if (l.r <= l.x || l.t <= l.y)
      continue;
    // Make the name nuke-friendly:
    char name[280];
    // copy leaving enough room at end for channel name:
    if (isdigit(l.name[0])) {
      name[0] = '_';
      strlcpy(name + 1, l.name, 279 - 10);
    }
    else {
      strlcpy(name, l.name, 280 - 10);
    }
    char* p = name;
    int junk = 0;
    for (; *p; ++p) {
      if (!isalnum(*p)) {
        *p = '_';
        junk++;
      }
    }
    if (p - name > 30 || junk > (p - name) / 3)
      p = name + sprintf(name, "layer%d", i);

    for (int j = 0; j < l.nChan; j++) {
      switch (l.chanID[j]) {
        case 0: strcpy(p, ".red");
          break;
        case 1: strcpy(p, ".green");
          break;
        case 2: strcpy(p, ".blue");
          break;
        case - 1: strcpy(p, ".alpha");
          break;
        case - 2: strcpy(p, ".mask");
          break;
        default:
          if (l.chanID[j] < 0)
            sprintf(p, ".c%d_idn%d", j, -l.chanID[j]);
          else
            sprintf(p, ".c%d_id%d", j, l.chanID[j]);
          break;
      }
      Channel ch = channel(name);
      l.channelMap[j] = ch;
      mask += (ch);
    }
    l.print();
  }

  set_info(width, height, 3);
  info_.channels(mask);
  info_.ydirection(-1);
}

psdReader::~psdReader ()
{
  fclose(file);
  delete[] array;
}

void psdReader::open()
{
}

bool psdReader::getLayerInfo ()
{
  long size = readLong(file);
  long here = ftell(file);

  // get the number of layers and allocate space for reading
  short nLayer = readShort(file);
  if (nLayer < 0)
    nLayer = -nLayer;
  this->nLayer = nLayer;
  layer = new PSDLayer[nLayer];
  memset(layer, 0, sizeof(PSDLayer) * nLayer);

  // now read the data for each layer (name, size, compositing scheme)
  int ln;
  for (ln = 0; ln < nLayer; ln++) {
    PSDLayer& l = layer[ln];
    l.y = readLong(file);
    l.x = readLong(file);
    l.t = readLong(file);
    l.r = readLong(file);
    layer[ln].nChan = readShort(file);
    short nc   = short(layer[ln].nChan);
    for (int c = 0; c < nc; c++) {
      layer[ln].chanID[c]   = readShort(file);
      layer[ln].chanSize[c] = readLong(file);
    }
    char buf[4];
    fread(buf, 1, 4, file); // should read '8BIM'
    fread(buf, 1, 4, file); // blend mode key
    fgetc(file); // opacity
    fgetc(file); // clipping
    fgetc(file); // flags
    fgetc(file); // filler

    long extraSize = readLong(file);    // remember the extra data size
    long extraHere = ftell(file);

    long layerMaskSize = readLong(file);    // skip the layer mask
    fseek(file, layerMaskSize, SEEK_CUR);
    long layerBlendingSize = readLong(file);    // skip the layer blending
    fseek(file, layerBlendingSize, SEEK_CUR);

    unsigned char nameSize = fgetc(file);
    char name[257];
    fread(name, 1, nameSize, file);
    name[nameSize] = 0;
    if (name[0] == 0)
      strcpy(name, "background");
    layer[ln].name = strdup(name);

    fseek(file, extraHere + extraSize, SEEK_SET); // skip unknown data
  }

  // find the file offset for the pixel data of each layer and channel
  long img_data = ftell(file);
  for (ln = 0; ln < nLayer; ln++) {
    PSDLayer& l = layer[ln];
    for (int c = 0; c < l.nChan; c++) {
      l.channelStart[c] = img_data;
      img_data += l.chanSize[c];
    }
  }

  fseek(file, here + size, SEEK_SET);
  return true;
}

// This is a guess! Not tested!
void psdReader::rleDecode(U16* d, long len)
{
  for (; len > 0;) {
    int k = readShort(file);
    if (k >= 0) {
      int n = k + 1;
      if (n > len)
        n = len;
      fread(d, 2, n, file);
      frommsb(d, n);
      d += n;
      len -= n;
    }
    else {
      int n = -k + 1;
      if (n > len)
        n = len;
      int c = readShort(file); // get high byte
      for (int i = 0; i < n; i++)
        *d++ = c;
      len -= n;
      fgetc(file); // ignore low byte
    }
  }
}

void psdReader::rleDecode(unsigned char* d, long len)
{
  for (; len > 0;) {
    signed char k = fgetc(file);
    if (k >= 0) {
      int n = k + 1;
      if (n > len)
        n = len;
      fread(d, 1, n, file);
      d += n;
      len -= n;
    }
    else {
      int n = -k + 1;
      if (n > len)
        n = len;
      memset(d, fgetc(file), n);
      d += n;
      len -= n;
    }
  }
}

void psdReader::copyDecode(U16* d, long len)
{
  fread(d, 2, len, file);
  frommsb(d, len);
}

void psdReader::copyDecode(unsigned char* d, long len)
{
  fread(d, 1, len, file);
}

/* The entire non-layer image is stored as a single compressed block.
   It appears the array of sizes before this is useless, as the compression
   goes right across the boundaries between the lines. So instead this
   allocates the entire buffer and reads it all in at once.
 */
void psdReader::getImageData()
{
  Guard guard(lock);
  if (array)
    return;          // another thread did it
  fseek(file, image_start, SEEK_SET);
  short cp = readShort(file); // compression type
  int srcDepth = depth;
  if (depth > 4)
    srcDepth = 4;
  if (bpc > 8) {
    U16* dst = new U16[width * height * srcDepth];
    if (cp == 0) {        // uncompressed
      copyDecode(dst, width * height * srcDepth);
    }
    else if (cp == 1) {          // run length encoding
      fseek(file, height * srcDepth * 2, SEEK_CUR); // skip compressed data size array
      rleDecode(dst, width * height * srcDepth);
    }
    else {
      iop->error("psd compression type %d is not supported", cp);
    }
    array = (unsigned char*)dst;
  }
  else {
    unsigned char* dst = new unsigned char[width * height * srcDepth];
    if (cp == 0) {        // uncompressed
      copyDecode(dst, width * height * srcDepth);
    }
    else if (cp == 1) {          // run length encoding
      fseek(file, height * srcDepth * 2, SEEK_CUR); // skip compressed data size array
      rleDecode(dst, width * height * srcDepth);
    }
    else {
      iop->error("psd compression type %d is not supported", cp);
    }
    array = dst;
  }
}

void psdReader::engine (int y, int x, int r, ChannelMask c1, Row& row)
{
  ChannelSet channels(c1);
  int py = height - y - 1;
  for (Channel z = Chan_Red; z <= Chan_Alpha; incr(z))
    if (intersect(channels, z)) {
      if (!array)
        getImageData();
      int i = z - 1;
      if (i >= depth)
        i = depth - 1;
      if (bpc > 8)
        from_short(z, row.writable(z) + x, (U16*)array + (i * height + py) * width + x, 0, r - x, 16);
      else
        from_byte(z, row.writable(z) + x, array + (i * height + py) * width + x, 0, r - x, 1);
    }

  channels -= (Mask_RGBA);
  if (!channels)
    return;

  for (int lnum = 0; lnum < nLayer; lnum++) {
    PSDLayer& l = layer[lnum];
    for (int cnum = 0; cnum < l.nChan; cnum++) {
      Channel z = l.channelMap[cnum];
      if (intersect(channels, z)) {
        if (py < l.y || py >= l.t) {
          row.erase(z);
          continue;
        }
        long cstart = l.channelStart[cnum];
        if (!cstart) {
          row.erase(z);
          continue;
        }
        ARRAY(U16, src, l.r - l.x);
        unsigned char* const charsrc = (unsigned char*)(&(src[0]));
        {
          Guard guard(lock);
          fseek(file, cstart, SEEK_SET);
          short cp = readShort(file); // compression type?!
          if (cp == 1) {
            long off = 0;
            for (int yy = py - l.y; yy > 0; yy--)
              off += readShort(file);
            fseek(file, cstart + off + 2 + 2 * (l.t - l.y), SEEK_SET);
            if (bpc > 8)
              rleDecode(src, l.r - l.x);
            else
              rleDecode(charsrc, l.r - l.x);
          }
          else if (cp == 0) {
            if (bpc > 8) {
              fseek(file, cstart + 2 + 2 * (l.r - l.x) * (py - l.y), SEEK_SET);
              copyDecode(src, l.r - l.x);
            }
            else {
              fseek(file, cstart + 2 + (l.r - l.x) * (py - l.y), SEEK_SET);
              copyDecode(charsrc, l.r - l.x);
            }
          }
          else {
            iop->error("psd layer compression type %d is not supported", cp);
            row.erase(z);
            continue;
          }
        }
        float* dst = row.writable(z);
        int px = x;
        int pr = r;
        while (px < l.x && px < pr)
          dst[px++] = 0;
        while (pr > l.r && pr > px)
          dst[--pr] = 0;
        // We do alpha (linear) conversion for all negative channel id's,
        // and rgb conversion for positive ones.
        if (bpc > 8)
          from_short(l.chanID[cnum] >= 0 ? Chan_Red : Chan_Alpha,
                     dst + px, src + px - l.x, 0, pr - px, 16);
        else
          from_byte(l.chanID[cnum] >= 0 ? Chan_Red : Chan_Alpha,
                    dst + px, charsrc + px - l.x, 0, pr - px, 1);
      }
    }
  }
}

// end of psdReader.C
