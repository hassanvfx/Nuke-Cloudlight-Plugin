// dpxReader.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/FileReader.h"
#include "DDImage/Row.h"
#include "DDImage/DDMath.h"
#include "DDImage/Knob.h"
#include "DDImage/ARRAY.h"
#include "DDImage/Matrix3.h"
#include "DPXimage.h"

#include <stdio.h>
#include <sstream>
#include <iomanip>
#include <limits>
#include <sys/stat.h>

using namespace DD::Image;

const float Kb = .0722f; //.114f;
const float Kr = .2126f; //.299f;

///! Sanitizes the string by replacing any non-printable characters with
///! C-style escape sequences.
inline std::string SanitizeString(const std::string& s)
{
  std::ostringstream oss;
  for (size_t i = 0; i < s.length(); i++) {
    if (isprint(s[i]))
      oss << s[i];
    else
      // double-cast here is intentional to (a) keep character in range 0-255 rather than -128-127 and
      // (b) then to print it to the stream as a number rather than as a char
      oss << "\\x" << std::setw(2) << std::hex << std::setfill('0') << (unsigned int)((unsigned char)s[i]);
  }
  return oss.str();
}

class dpxReader : public FileReader
{

  // this is the parts of the header we keep:
  bool flipped;
  bool ycbcr_hack;
  unsigned orientation;
  unsigned width;
  unsigned height;
  struct Element
  {
    U8 descriptor;
    U8 bits;
    U16 packing;
    U32 dataOffset;
    U32 bytes; // bytes per line
    int components; // descriptor decoded into # of components
    ChannelSet channels; // which Nuke channels it supplies
  }
  element[8];

  static const Description description;

  MetaData::Bundle meta;

public:

  const MetaData::Bundle& fetchMetaData(const char* key)
  {
    return meta;
  }

  dpxReader(Read* iop, int fd, const unsigned char* block, int len)
    : FileReader(iop, fd, block, len)
  {
    DPXHeader header;

    // Copy the header chunk raw:
    read(&header, 0, int(sizeof(header)));

    // Put the header into native-endianess:
    if (header.file.magicNumber != DPX_MAGIC) {
      flipped = true;
      flip(&header.file.magicNumber, 2); // magicNumber & offsetToImageData
      flip(&header.file.totalFileSize, 5); // totalFileSize thru userDataSize
      flip(&header.image.orientation, 2); // orientation & numberElements
      flip(&header.image.pixelsPerLine, 2); // pixelsPerLine & linesPerImage
      for (int i = 0; i < header.image.numberElements; i++) {
        flip(&header.image.element[i].dataSign, 5); // dataSign, low/high stuff
        flip(&header.image.element[i].packing, 2); // packing & encoding
        flip(&header.image.element[i].dataOffset, 3); // dataOffset, eol/imagePadding
      }
      flip((U32*)(&header.film.frameRate), 1);
      flip((U32*)(&header.film.framePosition), 1);
      flip((U32*)(&header.film.sequenceLen), 1);
      flip((U32*)(&header.film.heldCount), 1);
      flip((U32*)(&header.video.frameRate), 1);
      flip((U32*)(&header.film.shutterAngle), 1);
      flip((U32*)(&header.film.frameId), 1);
      flip((U32*)(&header.video.gamma), 1);
      flip(&header.video.timeCode, 1);
      flip((U32*)(&header.orientation.pixelAspect), 2);
    }
    else {
      flipped = false;
    }

    width = header.image.pixelsPerLine;
    height = header.image.linesPerImage;

    // Figure out the pixel aspect ratio. We recognize two possible
    // 'invalid' values -- all 0s or all 1s.
    // Equality is also invalid because Shake writes that for all images
    // Another bug version writes the image size as the pixel aspect,
    // ignore that.
    double pixel_aspect = 0;
    if ( header.orientation.pixelAspect[0] != 0 &&
         header.orientation.pixelAspect[1] != 0 &&
         header.orientation.pixelAspect[0] != 0xffffffff &&
         header.orientation.pixelAspect[1] != 0xffffffff &&
         header.orientation.pixelAspect[0] != header.orientation.pixelAspect[1] &&
         (header.orientation.pixelAspect[0] != width ||
          header.orientation.pixelAspect[1] != height)) {
      pixel_aspect = (double)header.orientation.pixelAspect[0] /
                     (double)header.orientation.pixelAspect[1];
    }

    // Set the image size. We will figure out the channels later from the elements, use rgb here:
    set_info(width, height, 3, pixel_aspect);

#define DUMP_HEADER 0
#if DUMP_HEADER
    printf("%s:", header.file.imageFileName);
    if (flipped)
      printf(" flipped");
    printf("\n");
    // printf(" size=%dx%dx, ", header.image.pixelsPerLine,
    //                         header.image.linesPerImage);
    // printf(" orientation=%x\n", header.image.orientation);
#endif

    int bitdepth = 0;
    for (int i = 0; i < header.image.numberElements; i++) {
      if (header.image.element[i].bits > bitdepth)
        bitdepth = header.image.element[i].bits;
    }

    ycbcr_hack = false;
    info_.channels(Mask_None);
    for (int i = 0; i < header.image.numberElements; i++) {
#if DUMP_HEADER
      printf(" %d: ", i);
      //    printf(" lowData=%d\n", header.image.element[i].lowData);
      //    printf(" lowQuantity=%f\n", header.image.element[i].lowQuantity);
      //    printf(" highData=%d\n", header.image.element[i].highData);
      //    printf(" highQuantity=%f\n", header.image.element[i].highQuantity);
      printf("d %d, ", header.image.element[i].descriptor);
      printf("t %d, ", header.image.element[i].transfer);
      printf("c %d, ", header.image.element[i].colorimetric);
      printf("%d bits, ", header.image.element[i].bits);
      if (header.image.element[i].dataSign)
        printf("signed, ");
      if (header.image.element[i].packing)
        printf("filled=%d, ", header.image.element[i].packing);
      if (header.image.element[i].encoding)
        printf("rle=%d, ", header.image.element[i].encoding);
      //    printf(" dataOffset=%d\n", header.image.element[i].dataOffset);
      if (header.image.element[i].eolPadding)
        printf("eolPadding=%d, ", header.image.element[i].eolPadding);
      //    printf(" eoImagePadding=%d\n", header.image.element[i].eoImagePadding);
      printf("\"%s\"\n", header.image.element[i].description);
#endif
      element[i].descriptor = header.image.element[i].descriptor;
      switch (element[i].descriptor) {
        case DESCRIPTOR_R:
          element[i].channels = Mask_Red;
          element[i].components = 1;
          break;
        case DESCRIPTOR_G:
          element[i].channels = Mask_Green;
          element[i].components = 1;
          break;
        case DESCRIPTOR_B:
          element[i].channels = Mask_Blue;
          element[i].components = 1;
          break;
        case DESCRIPTOR_A:
          element[i].channels = Mask_Alpha;
          element[i].components = 1;
          break;
        default:
          printf("Unknown DPX element descriptor %d\n", element[i].descriptor);
        case DESCRIPTOR_Y:
          element[i].channels = Mask_RGB;
          element[i].components = 1;
          break;
        case DESCRIPTOR_CbCr:
          element[i].channels = ChannelSetInit(6); // blue+green
          element[i].components = 1;
          if (i && element[0].descriptor == DESCRIPTOR_Y) {
            element[0].channels = Mask_Red;
            ycbcr_hack = true;
          }
          break;
        case DESCRIPTOR_Z:
          element[i].channels = Mask_Z;
          element[i].components = 1;
          break;
        case DESCRIPTOR_RGB:
          element[i].channels = Mask_RGB;
          element[i].components = 3;
          break;
        case DESCRIPTOR_RGBA:
          element[i].channels = Mask_RGBA;
          element[i].components = 4;
          break;
        case DESCRIPTOR_ABGR:
          element[i].channels = Mask_RGBA;
          element[i].components = 4;
          break;
        case DESCRIPTOR_CbYCrY:
          element[i].channels = Mask_RGB;
          element[i].components = 2;
          break;
        case DESCRIPTOR_CbYACrYA:
          element[i].channels = Mask_RGBA;
          element[i].components = 3;
          break;
        case DESCRIPTOR_CbYCr:
          element[i].channels = Mask_RGB;
          element[i].components = 3;
          break;
        case DESCRIPTOR_CbYCrA:
          element[i].channels = Mask_RGBA;
          element[i].components = 4;
          break;
        case DESCRIPTOR_USER_2:
          element[i].channels = ChannelSetInit(3); // red+green
          element[i].components = 2;
          break;
        case DESCRIPTOR_USER_3:
          element[i].channels = Mask_RGB;
          element[i].components = 3;
          break;
        case DESCRIPTOR_USER_4:
          element[i].channels = Mask_RGBA;
          element[i].components = 4;
          break;
        case DESCRIPTOR_USER_5:
          element[i].channels = Mask_RGBA;
          element[i].components = 5;
          break;
        case DESCRIPTOR_USER_6:
          element[i].channels = Mask_RGBA;
          element[i].components = 6;
          break;
        case DESCRIPTOR_USER_7:
          element[i].channels = Mask_RGBA;
          element[i].components = 7;
          break;
        case DESCRIPTOR_USER_8:
          element[i].channels = Mask_RGBA;
          element[i].components = 8;
          break;
      }

      element[i].bits = header.image.element[i].bits;
      element[i].packing = header.image.element[i].packing;
      //element[i].encoding = header.image.element[i].encoding;
      element[i].dataOffset = header.image.element[i].dataOffset;

      switch (element[i].bits) {
        case 1:
          element[i].bytes = (width * element[i].components + 31) / 32 * 4;
          break;
        case 8:
          element[i].bytes = (width * element[i].components + 3) & - 4;
          break;
        case 10:
          if (element[i].packing) {
            element[i].bytes = (width * element[i].components + 2) / 3 * 4;
            // detect stupid writers that did the math wrong
            struct stat s;
            fstat(fd, &s);
            if (element[i].dataOffset + element[i].bytes * height > size_t(s.st_size))
              element[i].bytes = (width * element[i].components) / 3 * 4;
          }
          else
            element[i].bytes = (width * element[i].components * 10 + 31) / 32 * 4;
          break;
        case 12:
          if (element[i].packing)
            element[i].bytes = (width * element[i].components) * 2;
          else
            element[i].bytes = (width * element[i].components * 12 + 31) / 32 * 4;
          break;
        case 16:
          element[i].bytes = (width * element[i].components) * 2;
          break;
        case 32: // no sample files available for this
        case 64: // no sample files available for this
        default:
          printf("Unhandled DPX number of bits %d\n", element[i].bits);
          element[i].channels = Mask_None;
          break;
      }
      if (header.image.element[i].eolPadding != 0xffffffff)
        element[i].bytes += header.image.element[i].eolPadding;

      info_.turn_on(element[i].channels);
    }

    unsigned v = header.video.timeCode;
    char buffer[12];
    sprintf(buffer, "%02x:%02x:%02x:%02x", v >> 24, (v >> 16) & 255,
            (v >> 8) & 255, v & 255);
    iop->setTimeCode(buffer);
    std::string timecode = buffer;

    std::string edgecode;

    DPXFilmHeader* s = &(header.film);
    if ( s->filmManufacturingIdCode[0] &&
         s->filmType[0] &&
         s->perfsOffset[0] &&
         s->prefix[0] &&
         s->count[0] ) {
      char buffer[22];
      sprintf( buffer, "%c%c %c%c %c%c%c%c%c%c %c%c%c%c %c%c",
               s->filmManufacturingIdCode[0],
               s->filmManufacturingIdCode[1],
               s->filmType[0], s->filmType[1],
               s->prefix[0], s->prefix[1],
               s->prefix[2], s->prefix[3], s->prefix[4], s->prefix[5],
               s->count[0], s->count[1], s->count[2], s->count[3],
               s->perfsOffset[0], s->perfsOffset[1] );
      edgecode = buffer;

      sprintf( buffer, "%c%c %c%c %c%c %c%c%c%c %c%c%c%c %c%c",
               s->filmManufacturingIdCode[0],
               s->filmManufacturingIdCode[1],
               s->filmType[0], s->filmType[1],
               s->prefix[0], s->prefix[1],
               s->prefix[2], s->prefix[3], s->prefix[4], s->prefix[5],
               s->count[0], s->count[1], s->count[2], s->count[3],
               s->perfsOffset[0], s->perfsOffset[1] );
      iop->setEdgeCode(SanitizeString(buffer));
    }

    orientation = header.image.orientation;
    info_.ydirection((orientation & 2) ? 1 : -1);

    switch (header.image.element[0].transfer) {
      case TRANSFER_USER: // seems to be used by some log files
      case TRANSFER_DENSITY:
      case TRANSFER_LOG:
        lut_ = LUT::getLut(LUT::LOG);
        break;
      case TRANSFER_CCIR_709_1:
        lut_ = LUT::builtin("rec709");
        break;
      case TRANSFER_LINEAR: // unfortunatly too much software writes this for sRGB...
      default:
        lut_ = LUT::getLut(header.image.element[0].bits <= 8 ? LUT::INT8 : LUT::INT16);
        break;
    }

    // XXXX

    meta.setData(MetaData::TIMECODE, timecode);

    if (pixel_aspect != 0) {
      meta.setData(MetaData::PIXEL_ASPECT, pixel_aspect);
    }

    meta.setData(MetaData::DEPTH, MetaData::DEPTH_FIXED(bitdepth));

    if ((header.video.frameRate != 0) && (isfinite(header.video.frameRate))) {
      meta.setData(MetaData::FRAME_RATE, header.video.frameRate);
    }

    if ((header.film.frameRate != 0) && (isfinite(header.film.frameRate))) {
      meta.setData(MetaData::FRAME_RATE, header.film.frameRate);
    }

    //    if  (header.film.framePosition != UNDEF_R32) {
    meta.setData(MetaData::DPX::FRAMEPOS,        header.film.framePosition);
    //    }

    if  (header.film.sequenceLen != UNDEF_U32) {
      meta.setData(MetaData::DPX::SEQUENCE_LENGTH, header.film.sequenceLen);
    }

    if  (header.film.heldCount != UNDEF_U32) {
      meta.setData(MetaData::DPX::HELD_COUNT, header.film.heldCount);
    }

    meta.setDataIfNotEmpty(MetaData::DPX::FRAME_ID, header.film.frameId);
    meta.setDataIfNotEmpty(MetaData::SLATE_INFO, header.film.slateInfo);

    if (edgecode.length() && edgecode != "00 00 000000 0000 00") {
      meta.setData(MetaData::EDGECODE, edgecode);
    }

    meta.setTimeStamp(MetaData::FILE_CREATION_TIME, header.file.creationTime);
    meta.setDataIfNotEmpty(MetaData::CREATOR, header.file.creator);
    meta.setDataIfNotEmpty(MetaData::PROJECT, header.file.project);
    meta.setDataIfNotEmpty(MetaData::COPYRIGHT, header.file.copyright);
  }

  ~dpxReader() {}

  void CConvert(float* dest, const uchar* src, int x, int r, int delta)
  {
    const float m = 1.0f / 255;
    const float off = .5f - 0x80 * m;
    for (; x < r; x++)
      dest[x] = src[x * delta] * m + off;
  }

  void CConvert(float* dest, const U16* src, int x, int r, int delta, int bits)
  {
    const float m = 1.0f / ((1 << bits) - 1);
    const float off = .5f - (1 << (bits - 1)) * m;
    for (; x < r; x++)
      dest[x] = src[x * delta] * m + off;
  }

  void CbConvert(float* dest, const uchar* src, int x, int r, int delta)
  {
    const float m = 1.0f / 255;
    const float m2 = m / 2;
    const float off = .5f - 0x80 * m;
    if (!(r & 1) && r >= int(width)) {
      dest[r - 1] = src[(r - 2) * delta] * m + off;
      r--;
    }
    for (; x < r; x++) {
      if (x & 1)
        dest[x] = (src[(x - 1) * delta] + src[(x + 1) * delta]) * m2 + off;
      else
        dest[x] = src[x * delta] * m + off;
    }
  }

  void CbConvert(float* dest, const U16* src, int x, int r, int delta, int bits)
  {
    const float m = 1.0f / ((1 << bits) - 1);
    const float m2 = m / 2;
    const float off = .5f - (1 << (bits - 1)) * m;
    if (!(r & 1) && r >= int(width)) {
      dest[r - 1] = src[(r - 2) * delta] * m + off;
      r--;
    }
    for (; x < r; x++) {
      if (x & 1)
        dest[x] = (src[(x - 1) * delta] + src[(x + 1) * delta]) * m2 + off;
      else
        dest[x] = src[x * delta] * m + off;
    }
  }

  void CrConvert(float* dest, const uchar* src, int x, int r, int delta)
  {
    const float m = 1.0f / 255;
    const float m2 = m / 2;
    const float off = .5f - 0x80 * m;
    if (!x) {
      dest[x] = src[(x + 1) * delta] * m + off;
      x++;
    }
    for (; x < r; x++) {
      if (x & 1)
        dest[x] = src[x * delta] * m + off;
      else
        dest[x] = (src[(x - 1) * delta] + src[(x + 1) * delta]) * m2 + off;
    }
  }

  void CrConvert(float* dest, const U16* src, int x, int r, int delta, int bits)
  {
    const float m = 1.0f / ((1 << bits) - 1);
    const float m2 = m / 2;
    const float off = .5f - (1 << (bits - 1)) * m;
    if (!x) {
      dest[x] = src[(x + 1) * delta] * m + off;
      x++;
    }
    for (; x < r; x++) {
      if (x & 1)
        dest[x] = src[x * delta] * m + off;
      else
        dest[x] = (src[(x - 1) * delta] + src[(x + 1) * delta]) * m2 + off;
    }
  }

  void YConvert(float* dest, const uchar* src, int x, int r, int delta)
  {
    Linear::from_byte(dest + x, src + x * delta, r - x, delta);
  }

  void YConvert(float* dest, const U16* src, int x, int r, int delta, int bits)
  {
    Linear::from_short(dest + x, src + x * delta, r - x, bits, delta);
  }

  void AConvert(float* dest, const uchar* src, int x, int r, int delta)
  {
    Linear::from_byte(dest + x, src + x * delta, r - x, delta);
  }

  void AConvert(float* dest, const U16* src, int x, int r, int delta, int bits)
  {
    Linear::from_short(dest + x, src + x * delta, r - x, bits, delta);
  }

  void fixYCbCr(int x, int r, bool alpha, Row& row)
  {
    if (iop->raw())
      return;
    float* R = row.writable(Chan_Red);
    float* G = row.writable(Chan_Green);
    float* B = row.writable(Chan_Blue);
    for (int X = x; X < r; X++) {
      float y = (R[X] - float(16 / 255.0f)) * float(255.0f / 219);
      float u = (G[X] - .5f) * float(255.0f / 224);
      float v = (B[X] - .5f) * float(255.0f / 224);
      R[X] = v * (2 - 2 * Kr) + y;
      G[X] = y - v * ((2 - 2 * Kr) * Kr / (1 - Kr - Kb)) - u * ((2 - 2 * Kb) * Kb / (1 - Kr - Kb));
      B[X] = u * (2 - 2 * Kb) + y;
    }
    from_float(Chan_Red, R + x, R + x, alpha ? row[Chan_Alpha] + x : 0, r - x);
    from_float(Chan_Green, G + x, G + x, alpha ? row[Chan_Alpha] + x : 0, r - x);
    from_float(Chan_Blue, B + x, B + x, alpha ? row[Chan_Alpha] + x : 0, r - x);
  }

  void read_element8(const Element& e, int y, int x, int r, Row& row)
  {
    // uncompress into an array of bytes:
    ARRAY(uchar, buf, width * e.components);
    if (e.bits == 1) {
      unsigned n = (e.bytes + 3) / 4;
      ARRAY(U32, src, n);
      read(src, e.dataOffset + y * e.bytes, e.bytes);
      if (flipped)
        flip(src, n);
      for (unsigned x = 0; x < width * e.components; x++)
        buf[x] = (src[x / 32] & (1 << (x & 31))) ? 255 : 0;
    }
    else {
      read(buf, e.dataOffset + y * e.bytes, width * e.components);
    }
    // now convert to rgb
    switch (e.descriptor) {

      case DESCRIPTOR_CbCr:
        // to actually get rgb we need the Y from the other element. NYI
        CbConvert(row.writable(Chan_Green), buf, x, r, 1);
        CrConvert(row.writable(Chan_Blue), buf, x, r, 1);
        if (ycbcr_hack)
          fixYCbCr(x, r, false, row);
        break;
      case DESCRIPTOR_RGBA: {
        const uchar* alpha = buf + x * 4 + 3;
        foreach(z, e.channels)
        from_byte(z, row.writable(z) + x, buf + x * 4 + (z - 1), alpha, r - x, 4);
        break;
      }
      case DESCRIPTOR_ABGR: {
        const uchar* alpha = buf + x * 4;
        foreach(z, e.channels)
        from_byte(z, row.writable(z) + x, buf + x * 4 + (4 - z), alpha, r - x, 4);
        break;
      }
      case DESCRIPTOR_CbYCrY:
        YConvert(row.writable(Chan_Red), buf + 1, x, r, 2);
        CbConvert(row.writable(Chan_Green), buf, x, r, 2);
        CrConvert(row.writable(Chan_Blue), buf, x, r, 2);
        fixYCbCr(x, r, false, row);
        break;
      case DESCRIPTOR_CbYACrYA:
        CbConvert(row.writable(Chan_Green), buf, x, r, 3);
        CrConvert(row.writable(Chan_Blue), buf, x, r, 3);
        YConvert(row.writable(Chan_Red), buf + 1, x, r, 3);
        AConvert(row.writable(Chan_Alpha), buf + 2, x, r, 3);
        fixYCbCr(x, r, true, row);
        break;
      case DESCRIPTOR_CbYCr:
        CConvert(row.writable(Chan_Green), buf, x, r, 3);
        YConvert(row.writable(Chan_Red), buf + 1, x, r, 3);
        CConvert(row.writable(Chan_Blue), buf + 2, x, r, 3);
        fixYCbCr(x, r, false, row);
        break;
      case DESCRIPTOR_CbYCrA:
        CConvert(row.writable(Chan_Green), buf, x, r, 4);
        YConvert(row.writable(Chan_Red), buf + 1, x, r, 4);
        CConvert(row.writable(Chan_Blue), buf + 2, x, r, 4);
        AConvert(row.writable(Chan_Alpha), buf + 3, x, r, 4);
        fixYCbCr(x, r, true, row);
        break;
      case DESCRIPTOR_Y:
        if (ycbcr_hack) {
          YConvert(row.writable(Chan_Red), buf, x, r, 1);
          break;
        } // else fall through:
      default: {
        int Z = 0;
        foreach(z, e.channels) {
          from_byte(z, row.writable(z) + x, buf + Z + x * e.components, 0 /*alpha*/, r - x, e.components);
          if (Z + 1 < e.components)
            Z++;
        }
        break;
      }
    }
  }

  void read_element16(const Element& e, int y, int x, int r, Row& row)
  {
    ARRAY(U16, buf, width * e.components + 2);
    switch (e.bits) {
      case 10: {
        unsigned n = (e.bytes + 3) / 4;
        unsigned x;
        ARRAY(U32, src, n);
        read(src, e.dataOffset + y * e.bytes, e.bytes);
        if (flipped)
          flip(src, n);
        switch (e.packing) {
          case 0:
            for (x = 0; x < width * e.components; x++) {
              unsigned a = (x * 10) / 32;
              unsigned b = (x * 10) % 32;
              if (b > 22)
                buf[x] = ((src[a + 1] << (32 - b)) + (src[a] >> b)) & 0x3ff;
              else
                buf[x] = (src[a] >> b) & 0x3ff;
            }
            break;
          case 1:
            for (x = 0; x < n; x++) {
              buf[3 * x + 0] = (src[x] >> 22) & 0x3ff;
              buf[3 * x + 1] = (src[x] >> 12) & 0x3ff;
              buf[3 * x + 2] = (src[x] >> 02) & 0x3ff;
            }
            break;
          case 2:
            for (x = 0; x < n; x++) {
              buf[3 * x + 0] = (src[x] >> 20) & 0x3ff;
              buf[3 * x + 1] = (src[x] >> 10) & 0x3ff;
              buf[3 * x + 2] = (src[x] >> 00) & 0x3ff;
            }
            break;
        }
        break;
      }
      case 12:
        switch (e.packing) {
          case 0: {
            unsigned n = (e.bytes + 3) / 4;
            ARRAY(U32, src, n);
            read(src, e.dataOffset + y * e.bytes, e.bytes);
            if (flipped)
              flip(src, n);
            for (unsigned x = 0; x < width * e.components; x++) {
              unsigned a = (x * 12) / 32;
              unsigned b = (x * 12) % 32;
              if (b > 20)
                buf[x] = ((src[a + 1] << (32 - b)) + (src[a] >> b)) & 0xfff;
              else
                buf[x] = (src[a] >> b) & 0xfff;
            }
            break;
          }
          case 1: {
            unsigned n = width * e.components;
            read(buf, e.dataOffset + y * e.bytes, n * 2);
            if (flipped)
              flip(buf, n);
            for (unsigned x = 0; x < n; x++)
              buf[x] >>= 4;
            break;
          }
          case 2: {
            unsigned n = width * e.components;
            read(buf, e.dataOffset + y * e.bytes, n * 2);
            if (flipped)
              flip(buf, n);
            for (unsigned x = 0; x < n; x++)
              buf[x] &= 0xfff;
            break;
          }
        }
        break;
      case 16:
        read(buf, e.dataOffset + y * e.bytes, width * e.components * 2);
        if (flipped)
          flip(buf, width * e.components);
        break;
    }
    // now convert to rgb
    switch (e.descriptor) {

      case DESCRIPTOR_CbCr:
        // to actually get rgb we need the Y from the other element. NYI
        CbConvert(row.writable(Chan_Green), buf, x, r, 1, e.bits);
        CrConvert(row.writable(Chan_Blue), buf, x, r, 1, e.bits);
        if (ycbcr_hack)
          fixYCbCr(x, r, false, row);
        break;
      case DESCRIPTOR_RGBA: {
        const U16* alpha = buf + x * 4 + 3;
        foreach(z, e.channels)
        from_short(z, row.writable(z) + x, buf + x * 4 + (z - 1), alpha, r - x, e.bits, 4);
        break;
      }
      case DESCRIPTOR_ABGR: {
        const U16* alpha = buf + x * 4;
        foreach(z, e.channels)
        from_short(z, row.writable(z) + x, buf + x * 4 + (4 - z), alpha, r - x, e.bits, 4);
        break;
      }
      case DESCRIPTOR_CbYCrY:
        YConvert(row.writable(Chan_Red), buf + 1, x, r, 2, e.bits);
        CbConvert(row.writable(Chan_Green), buf, x, r, 2, e.bits);
        CrConvert(row.writable(Chan_Blue), buf, x, r, 2, e.bits);
        fixYCbCr(x, r, false, row);
        break;
      case DESCRIPTOR_CbYACrYA:
        CbConvert(row.writable(Chan_Green), buf, x, r, 3, e.bits);
        CrConvert(row.writable(Chan_Blue), buf, x, r, 3, e.bits);
        YConvert(row.writable(Chan_Red), buf + 1, x, r, 3, e.bits);
        AConvert(row.writable(Chan_Alpha), buf + 2, x, r, 3, e.bits);
        fixYCbCr(x, r, true, row);
        break;
      case DESCRIPTOR_CbYCr:
        YConvert(row.writable(Chan_Red), buf + 1, x, r, 3, e.bits);
        if (e.bits == 10) {
          // The "flowers" 10-bit sample image has the Cr/Cb reversed. This may be
          // a mistake, and other small test images have them the other way.
          // However I am leaving this reversed as that is how previous versions
          // of Nuke read this file.
          CConvert(row.writable(Chan_Blue), buf, x, r, 3, e.bits);
          CConvert(row.writable(Chan_Green), buf + 2, x, r, 3, e.bits);
        }
        else {
          CConvert(row.writable(Chan_Green), buf, x, r, 3, e.bits);
          CConvert(row.writable(Chan_Blue), buf + 2, x, r, 3, e.bits);
        }
        fixYCbCr(x, r, false, row);
        break;
      case DESCRIPTOR_CbYCrA:
        CConvert(row.writable(Chan_Green), buf, x, r, 4, e.bits);
        YConvert(row.writable(Chan_Red), buf + 1, x, r, 4, e.bits);
        CConvert(row.writable(Chan_Blue), buf + 2, x, r, 4, e.bits);
        AConvert(row.writable(Chan_Alpha), buf + 3, x, r, 4, e.bits);
        fixYCbCr(x, r, true, row);
        break;
      case DESCRIPTOR_Y:
        if (ycbcr_hack) {
          YConvert(row.writable(Chan_Red), buf, x, r, 1, e.bits);
          break;
        } // else fall through:
      default: {
        int Z = 0;
        foreach(z, e.channels) {
          from_short(z, row.writable(z) + x, buf + Z + x * e.components, 0 /*alpha*/, r - x, e.bits, e.components);
          if (Z + 1 < e.components)
            Z++;
        }
        break;
      }
    }
  }

  void read_element(const Element& e, int y, int x, int r, Row& row)
  {
    if (e.bits <= 8)
      read_element8(e, y, x, r, row);
    else
      read_element16(e, y, x, r, row);
  }

  void engine(int y, int x, int r, ChannelMask channels, Row& row)
  {
    if (!(orientation & 2))
      y = height - y - 1;
    ChannelSet remaining(channels);
    if (ycbcr_hack && (channels & Mask_RGB))
      remaining += Mask_RGB;
    for (unsigned i = 0; i < 8; i++) {
      if (element[i].channels & remaining) {
        read_element(element[i], y, x, r, row);
        remaining -= element[i].channels;
        if (!remaining)
          break;
      }
    }
  }

};

static bool test(int fd, const unsigned char* block, int length)
{
  DPXHeader* header = (DPXHeader*)block;
  U32 m = header->file.magicNumber;
  if (m == DPX_MAGIC || m == DPX_MAGIC_FLIPPED)
    return true;
  return false;
}

static Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
  return new dpxReader(iop, fd, b, n);
}

const Reader::Description dpxReader::description("dpx\0", build, test);

// end dpxReader.C
