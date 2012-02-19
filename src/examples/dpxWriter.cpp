// dpxWriter.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/FileWriter.h"
#include "DDImage/Row.h"
#include "DDImage/ARRAY.h"
#include "DDImage/DDMath.h"
#include "DDImage/Knobs.h"
#include "DDImage/DDString.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "DPXimage.h"

using namespace DD::Image;

static const char* const dnames[] = {
  "8 bit", "10 bit", "12 bit", "16 bit", 0
};
static int bits[] = {
  8, 10, 12, 16
};

class DPXWriter : public FileWriter
{
  static const Description d;
  int datatype;
  bool fill;
  bool YCbCr;
  bool bigEndian;

  int num_channels;
  int components;
  int bytes;

public:
  const char* _timecode, * _edgecode;

  DPXWriter(Write* iop) : FileWriter(iop)
  {
    datatype = 1; // 10 bits
    fill = false;
    YCbCr = false;
    bigEndian = true; // true for back-compatibility
    _timecode = _edgecode = 0;
  }

  LUT* defaultLUT() const
  {
    if (datatype == 1 && !YCbCr) {
      return LUT::getLut(LUT::LOG);
    }
    else if (datatype) {
      return LUT::getLut(LUT::INT16);
    }
    else {
      return LUT::getLut(LUT::INT8);
    }
  }

  void assignProp(R32& field,  const MetaData::Bundle& metadata, const char* propname);
  void assignProp(U32& field,  const MetaData::Bundle& metadata, const char* propname);
  void assignProp(char* field, size_t fieldlen, const MetaData::Bundle& metadata, const char* propname);

  void knobs(Knob_Callback f)
  {

    Enumeration_knob(f, &datatype, dnames, "datatype");
    Bool_knob(f, &fill, "fill");
    Tooltip(f, "Compress 10/12 bit data by removing unused bits.");
    //     Bool_knob(f, &YCbCr, "YCbCr", "4:2:2");
    //     Tooltip(f, "Write as YCbCr 4:2:2 data, which is 2/3 the size");
    Bool_knob(f, &bigEndian, "bigEndian", "big endian");
    Tooltip(f, "Force file to be big-endian, rather than native-endian. This is slower, but some programs will only read big-endian files");
    String_knob(f, &_timecode, "timecode", "time code");
    Tooltip(f, "A timecode here in HHMMSSFF format "
               "will be written to the file. A typical value is to copy the "
               "timecode from a file reader: [knob Read1.timecode]");
    String_knob(f, &_edgecode, "edge_code", "edge code");
    Tooltip(f, "An edge code here in Nuke edge code format "
               "will be written to the file. A typical use is to copy the "
               "edge code from a file reader, e.g.: [value Read1.edge_code]");
  }

  void execute();

  const char* help() { return "Kodak/SMPTE Digital Picture format"; }
};

static Writer* build(Write* iop)
{
  return new DPXWriter(iop);
}

const Writer::Description DPXWriter::d("dpx\0", "DPX", build);

void DPXWriter::assignProp(R32& field, const MetaData::Bundle& metadata, const char* propname)
{
  if (metadata.find(propname) == metadata.end())
    return;

  double value = metadata.getDouble(propname);
  field = value;
  if (bigEndian)
    tomsb((U32*)&field, 1);
}

void DPXWriter::assignProp(U32& field, const MetaData::Bundle& metadata, const char* propname)
{
  if (metadata.find(propname) == metadata.end())
    return;

  unsigned value = (unsigned)metadata.getDouble(propname);
  field = value;
  if (bigEndian)
    tomsb((U32*)&field, 1);
}

void DPXWriter::assignProp(char* field, size_t fieldlen, const MetaData::Bundle& metadata, const char* propname)
{
  if (metadata.find(propname) == metadata.end())
    return;

  std::string value = metadata.getString(propname);
  strncpy(field, value.c_str(), fieldlen);
  field[fieldlen - 1] = 0;
  if (bigEndian)
    tomsb((U32*)&field, 1);
}

void DPXWriter::execute()
{
  if (!open())
    return;

  //Write header
  DPXHeader header;
  memset(&header, 0, sizeof(header));

  //FileInfo
  header.file.magicNumber = DPX_MAGIC;
  header.file.offsetToImageData = sizeof(header);
  //file size = header + (rows*columns*bytes/pixel)
  header.file.totalFileSize = sizeof(header) + (width() * height() * 4);
  header.file.dittoKey = 1; //0=same, 1=new
  header.file.genericHeaderSize = sizeof(header.file) + sizeof(header.image) + sizeof(header.orientation); //generic header length in bytes
  header.file.specificHeaderSize = sizeof(header.film) + sizeof(header.video); //industry header length in bytes
  header.file.userDataSize = 0; //user-defined data length in bytes
  if (bigEndian)
    tomsb(&header.file.magicNumber, 9);
  strcpy(header.file.version, "V1.0");
  strlcpy(header.file.imageFileName, filename(), 100);
  time_t fileClock = time(0);
  struct tm* fileTime = localtime(&fileClock);
  strftime(header.file.creationTime, 24, "%Y:%m:%d:%H:%M:%S:%Z", fileTime);
  //Should add version in here
  sprintf(header.file.creator, "Nuke");
  header.file.key = UNDEF_U32;

  MetaData::Bundle metaData = iop->fetchMetaData(NULL);

  assignProp(header.film.frameRate,     metaData, MetaData::FRAME_RATE);

  header.film.framePosition = UNDEF_R32;
  assignProp(header.film.framePosition, metaData, MetaData::DPX::FRAMEPOS);

  header.film.sequenceLen = UNDEF_U32;
  assignProp(header.film.sequenceLen,   metaData, MetaData::DPX::SEQUENCE_LENGTH);

  header.film.heldCount = UNDEF_U32;
  assignProp(header.film.heldCount,     metaData, MetaData::DPX::HELD_COUNT);
  assignProp(header.film.frameId,       sizeof header.film.frameId,   metaData, MetaData::DPX::FRAME_ID);
  assignProp(header.file.project,       sizeof header.file.project,   metaData, MetaData::PROJECT);
  assignProp(header.file.copyright,     sizeof header.file.copyright, metaData, MetaData::COPYRIGHT);

  //Image data
  header.image.orientation = 0; //image orientation -- (left to right, top to bottom)
  header.image.numberElements = 1; //number of image elements
  if (bigEndian)
    tomsb(&header.image.orientation, 2);
  header.image.pixelsPerLine = width(); //x value
  header.image.linesPerImage = height(); //y value
  if (bigEndian)
    tomsb(&header.image.pixelsPerLine, 2);

  //Channel data
  for (int i = 0; i < 1; i++) {
    header.image.element[i].dataSign = 0; // data sign (0 = unsigned, 1 = signed )
    header.image.element[i].lowData = UNDEF_U32; //reference low data code value
    header.image.element[i].highData = UNDEF_U32; // reference high data code value
    header.image.element[i].lowQuantity = UNDEF_R32;
    header.image.element[i].highQuantity = UNDEF_R32;
    if (bigEndian)
      tomsb(&header.image.element[i].dataSign, 5);
    num_channels = Writer::num_channels();

    if (num_channels > 3) { // we are trying to write alpha
      num_channels = 4;
      if (YCbCr) {
        header.image.element[i].descriptor = DESCRIPTOR_CbYACrYA;
        components = 3;
      }
      else {
        header.image.element[i].descriptor = DESCRIPTOR_RGBA;
        components = 4;
      }
    }
    else if (num_channels > 1) {
      num_channels = 3;
      if (YCbCr) {
        header.image.element[i].descriptor = DESCRIPTOR_CbYCrY;
        components = 2;
      }
      else {
        header.image.element[i].descriptor = DESCRIPTOR_RGB;
        components = 3;
      }
    }
    else {
      num_channels = components = 1;
      header.image.element[i].descriptor = DESCRIPTOR_Y;
    }

    switch (datatype) {
      case 0: // 8 bits
        bytes = (width() * components + 3) & - 4;
        break;
      case 1: // 10 bits
        if (fill)
          bytes = (width() * components * 10 + 31) / 32 * 4;
        else
          bytes = (width() * components + 2) / 3 * 4;
        break;
      case 2: // 12 bits
        if (fill)
          bytes = (width() * components * 12 + 31) / 32 * 4;
        else
          bytes = (width() * components) * 2;
        break;
      case 3: // 16 bits
        bytes = (width() * components) * 2;
        break;
    }

    if (lut()->linear() || lut() == LUT::getLut(LUT::LOG))
      header.image.element[i].transfer = TRANSFER_USER;
    else
      header.image.element[i].transfer = TRANSFER_LINEAR;
    header.image.element[i].colorimetric = header.image.element[i].transfer;

    header.image.element[i].bits = bits[datatype];
    header.image.element[i].packing = fill ? 0 : 1;
    header.image.element[i].encoding = 0; // encoding for element (no run length encoding applied)
    if (bigEndian)
      tomsb(&header.image.element[i].packing, 2);

    header.image.element[i].dataOffset = sizeof(header);
    header.image.element[i].eolPadding = 0; // end of line padding used in element (no padding)
    header.image.element[i].eoImagePadding = 0; // end of image padding used in element (no padding)
    if (bigEndian)
      tomsb(&header.image.element[i].dataOffset, 3);
  }

  //Image Orientation
  header.orientation.xOffset = 0;
  header.orientation.yOffset = 0;
  header.orientation.xCenter = R32( width() ) / 2.0f;
  header.orientation.yCenter = R32( height() ) / 2.0f;
  header.orientation.xOrigSize = width();
  header.orientation.yOrigSize = height();
  if (bigEndian)
    tomsb(&header.orientation.xOffset, 6);

  header.orientation.border[0] = 0;
  header.orientation.border[1] = 0;
  header.orientation.border[2] = 0;
  header.orientation.border[3] = 0;
  if (bigEndian)
    tomsb(&header.orientation.border[0], 4);

  header.orientation.pixelAspect[1] = 1200;
  header.orientation.pixelAspect[0] =
    (U32)(iop->format().pixel_aspect() * header.orientation.pixelAspect[1] + .5);
  if (bigEndian)
    tomsb(&header.orientation.pixelAspect[0], 2);

  std::string timecode = _timecode ? _timecode : "";

  if (timecode.length() == 0 && metaData.getData(MetaData::TIMECODE)) {
    timecode = metaData.getString(MetaData::TIMECODE);
  }

  if (timecode.length()) {
    // Skip everything except digits, encode as BCD:
    unsigned tc_bcd = 0;

    const char* timecodec = timecode.c_str();

    for (const char* q = timecodec; *q; q++) {
      if (*q >= '0' && *q <= '9') {
        tc_bcd = (tc_bcd << 4) + (*q - '0');
      }
    }
    header.video.timeCode = tc_bcd;
    if (bigEndian)
      tomsb(&header.video.timeCode, 1);
  }

  std::string edgecode = _edgecode ? _edgecode : "";

  if (edgecode.length() == 0 && metaData.getData(MetaData::EDGECODE)) {
    edgecode = metaData.getString(MetaData::EDGECODE);
  }

  if (edgecode.length()) {
    char edgeCodeNoSpace[16];
    int i = 0;

    const char* edgecodec = edgecode.c_str();

    for (const char* src = edgecodec; *src && i < 16; src++)
      if (!isspace(*src))
        edgeCodeNoSpace[i++] = *src;

    while (i < 16)
      edgeCodeNoSpace[i++] = '0';
    DPXFilmHeader* s = &(header.film);
    memcpy( s->filmManufacturingIdCode, edgeCodeNoSpace, 2 );
    memcpy( s->filmType, edgeCodeNoSpace + 2, 2 );
    memcpy( s->prefix, edgeCodeNoSpace + 4, 6 );
    memcpy( s->count, edgeCodeNoSpace + 10, 4 );
    memcpy( s->perfsOffset, edgeCodeNoSpace + 14, 2 );
  }

  //Now we write out actual image data
  write(&header, sizeof(header));

  ChannelSet mask = channel_mask(num_channels);
  input0().request(0, 0, width(), height(), mask, 1);
  if (aborted())
    return;

  Row row(0, width());
  unsigned off = sizeof(header);
  int n = num_channels * width();
  ARRAY(U16, src, n + 4);
  src[n] = src[n + 1] = src[n + 2] = src[n + 3] = 0;
  ARRAY(U32, buf, bytes / 4);

  for (int y = height(); y--;) {
    if (aborted())
      return;
    iop->status(float(1.0f - float(y) / height()));
    get(y, 0, width(), mask, row);
    void* p = NULL;
    if (!datatype) {
      // 8-bit data
      uchar* buf = (uchar*)&(src[0]);
      for (int z = 0; z < num_channels; z++)
        to_byte(z, buf + z, row[channel(z)], row[Chan_Alpha], width(), num_channels);
      p = buf;
    }
    else {
      // 10,12,16 bit data
      for (int z = 0; z < num_channels; z++)
        to_short(z, src + z, row[channel(z)], row[Chan_Alpha], width(), bits[datatype], num_channels);
      switch (datatype) {
        case 1: // 10 bits
          if (fill) {
            for (int x = 0; x < n; x++) {
              unsigned a = (x * 10) / 32;
              unsigned b = (x * 10) % 32;
              if (b > 22) {
                buf[a + 1] = src[x] >> (32 - b);
                buf[a] |= src[x] << b;
              }
              else if (b) {
                buf[a] |= src[x] << b;
              }
              else {
                buf[a] = src[x];
              }
            }
            if (bigEndian)
              tomsb(buf, bytes / 4);
            p = buf;
          }
          else {
            for (int x = 0; x < n; x += 3)
              buf[x / 3] = (src[x] << 22) + (src[x + 1] << 12) + (src[x + 2] << 2);
            if (bigEndian)
              tomsb(buf, bytes / 4);
            p = buf;
          }
          break;
        case 2: // 12 bits
          if (fill) {
            for (int x = 0; x < n; x++) {
              unsigned a = (x * 12) / 32;
              unsigned b = (x * 12) % 32;
              if (b > 20) {
                buf[a + 1] = src[x] >> (32 - b);
                buf[a] |= src[x] << b;
              }
              else if (b) {
                buf[a] |= src[x] << b;
              }
              else {
                buf[a] = src[x];
              }
            }
            if (bigEndian)
              tomsb(buf, bytes / 4);
            p = buf;
          }
          else {
            for (int x = 0; x < n; x++)
              src[x] <<= 4;
            if (bigEndian)
              tomsb(src, n);
            p = src;
          }
          break;
        case 3: // 16 bits
          if (bigEndian)
            tomsb(src, n);
          p = src;
      }
    }
    write(off, p, bytes);
    off += bytes;
  }

  close();
}
