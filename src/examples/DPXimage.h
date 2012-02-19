// DPXimage.h
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

// SMPTE DPX files header structures

// Modified from original that is Copyright 1991 Kodak (Australasia) Pty Ltd
// Also information from http://www.fileformat.info/format/dpx

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef int S32;
typedef float R32;

#define UNDEF_U8  0xFF
#define UNDEF_U32 0xFFFFFFFF
#define UNDEF_S32 0x80000000
#define UNDEF_R32 0x7F800000
#define UNDEF_ASC 0

#define DPX_MAGIC 0x53445058 // (SDPX)
#define DPX_MAGIC_FLIPPED 0x58504453 // (XPDS)

struct DPXFileInfoHeader
{
  U32 magicNumber;        // magic number 0x53445058 (SDPX) or 0x58504453 (XPDS)
  U32 offsetToImageData;    // offset to image data in bytes
  char version[8];        // which header format version is being used (v1.0)
  U32 totalFileSize;        // file size in bytes
  U32 dittoKey;            // read time short cut - 0 = same, 1 = new
  U32 genericHeaderSize;    // generic header length in bytes
  U32 specificHeaderSize;     // industry header length in bytes
  U32 userDataSize;        // user-defined data length in bytes
  char imageFileName[100];    // image file name
  char creationTime[24];    // file creation date "yyyy:mm:dd:hh:mm:ss:LTZ"
  char creator[100];        // file creator's name
  char project[200];        // project name
  char copyright[200];        // right to use or copyright info
  U32 key;            // encryption ( FFFFFFFF = unencrypted )
  char reserved[104];        // padding
};

struct DPXImageInfoHeader
{
  U16 orientation;        // or: 1=right-to-left, 2=bottom-to-top, 4=transpose
  U16 numberElements;        // number of elements
  U32 pixelsPerLine;        // width of image
  U32 linesPerImage;        // height of image
  struct DPXImageElement
  {
    U32 dataSign;        // 0 = unsigned, 1 = signed
    U32 lowData;        // minimum expected value
    R32 lowQuantity;        // minimum possible value
    U32 highData;        // maximum expected value
    R32 highQuantity;        // maximum possible value
    U8  descriptor;        // descriptor for image element
    U8  transfer;        // transfer characteristics for element
    U8  colorimetric;        // colormetric specification for element
    U8  bits;             // size of each component: 1,8,10,12,16,32, or 64
    U16 packing;        // 0=pack the bits, 1=skip low bits, 2=skip high bits
    U16 encoding;        // 0=raw, 1=run-length encoding
    U32 dataOffset;        // offset to data of element
    U32 eolPadding;        // number of bytes added to end of each line
    U32 eoImagePadding;        // number of bytes added to end of image
    char description[32];    // description of element
  }
  element[8];
  U8 reserved[52];        // padding
};

enum { // values for descriptor
  DESCRIPTOR_USER_1    = 0, // user-defined 1(?) component element
  DESCRIPTOR_R        = 1,
  DESCRIPTOR_G        = 2,
  DESCRIPTOR_B        = 3,
  DESCRIPTOR_A        = 4,
  DESCRIPTOR_Y        = 6,
  DESCRIPTOR_CbCr    = 7,
  DESCRIPTOR_Z        = 8,
  DESCRIPTOR_COMPOSITE_VIDEO = 9, // the signal?
  DESCRIPTOR_RGB    = 50,
  DESCRIPTOR_RGBA    = 51,
  DESCRIPTOR_ABGR    = 52,
  DESCRIPTOR_CbYCrY    = 100,
  DESCRIPTOR_CbYACrYA    = 101,
  DESCRIPTOR_CbYCr    = 102,
  DESCRIPTOR_CbYCrA    = 103,
  DESCRIPTOR_USER_2    = 150, // user-defined 2-component element
  DESCRIPTOR_USER_3    = 151, // user-defined 3-component element
  DESCRIPTOR_USER_4    = 152, // user-defined 4-component element
  DESCRIPTOR_USER_5    = 153, // user-defined 5-component element
  DESCRIPTOR_USER_6    = 154, // user-defined 6-component element
  DESCRIPTOR_USER_7    = 155, // user-defined 7-component element
  DESCRIPTOR_USER_8    = 156  // user-defined 8-component element
};

enum { // values for transfer & colorimetric (if not marked T-only)
  TRANSFER_USER        = 0, // unfortunatly seems to be used to mean log by some software
  TRANSFER_DENSITY    = 1, // printing density
  TRANSFER_LINEAR    = 2, // T-only, appears to mean sRGB
  TRANSFER_LOG        = 3, // T-only
  TRANSFER_VIDEO    = 4, // unspecified video
  TRANSFER_SMPTE_240M    = 5,
  TRANSFER_CCIR_709_1    = 6,
  TRANSFER_CCIR_601_2_BG = 7,
  TRANSFER_CCIR_601_2_M    = 8,
  TRANSFER_NTSC        = 9,
  TRANSFER_PAL        = 10,
  TRANSFER_Z_LINEAR    = 11, // T-only
  TRANSFER_Z_HOMOGENOUS    = 12  // T-only
};

struct DPXOrientationHeader
{
  U32 xOffset, yOffset;        // top-left corner of data
  R32 xCenter, yCenter;        // coordinates of center of image
  U32 xOrigSize, yOrigSize;    // size of the original image
  char fileName[100];        // source image file name
  char creationTime[24];    // source image creation time YYYY:MM:DD:HH:MM:SS:LTZ
  char inputName[32];         // input device name
  char inputSN[32];        // input device serial number
  U16 border[4];        // thickness of eroded borders (left, right, top, bottom)
  U32 pixelAspect[2];        // shape of a pixel (horizontal, vertical)
  U8  reserved[28];        // padding
};

struct DPXFilmHeader
{
  unsigned char filmManufacturingIdCode[2];
  unsigned char filmType[2];
  unsigned char perfsOffset[2];
  unsigned char prefix[6];
  unsigned char count[4];
  unsigned char format[32];        // name of film format
  U32 framePosition;         // frame number in the sequence
  U32 sequenceLen;        // total number of frames in sequence
  U32 heldCount;         // number of times to repeat this frame
  R32 frameRate;         // frame rate of original in frames/sec
  R32 shutterAngle;        // shutter angle of camera in degrees
  char frameId[32];        // frame identification (i.e. keyframe)
  char slateInfo[100];        // slate information
  U8  reserved[56];        // padding
};

struct DPXTelevisionHeader
{
  U32 timeCode;            // SMPTE time code
  U32 userBits;            // SMPTE user bits
  U8  interlace;        // interlace ( 0 = noninterlaced, 1 = 2:1 interlace)
  U8  fieldNumber;        // field number
  U8  videoSignal;        // video signal standard (table 4)
  U8  unused;             // used for byte alignment only
  R32 horzSampleRate;        // horizontal sampling rate in Hz
  R32 vertSampleRate;        // vertical sampling rate in Hz
  R32 frameRate;         // temporal sampling rate or frame rate in Hz
  R32 timeOffset;        // time offset from sync to first pixel
  R32 gamma;            // gamma correction, default is 2.2 for NTSC
  R32 blackLevel;        // value representing reference black
  R32 blackGain;         // linear gain applied to signals below the breakpoint
  R32 breakpoint;        // threshold above which the gamma law is applied
  R32 whiteLevel;        // value representing reference white
  R32 integrationTimes;        // temporal sampline aperture of the camera
  U8  reserved[76];        // padding
};

enum { // values for videoSignal
  VIDEO_UNDEFINED    = 0,
  VIDEO_NTSC        = 1,
  VIDEO_PAL        = 2,
  VIDEO_PAL_M        = 3,
  VIDEO_SECAM        = 4,
  VIDEO_525i_4x3    = 50,
  VIDEO_625i_4x3    = 51,
  VIDEO_525i        = 100, // all the rest are 16x9
  VIDEO_625i        = 101,
  VIDEO_1050i        = 150,
  VIDEO_1125i        = 151,
  VIDEO_1250i        = 152,
  VIDEO_525p        = 200,
  VIDEO_625p        = 201,
  VIDEO_787p5p        = 202
};

struct DPXUsedDefined
{
  char userId[32];        // identification string
  U8 data[1];            // first byte of up to 1mb of data
};

struct DPXHeader
{
  DPXFileInfoHeader    file;
  DPXImageInfoHeader    image;
  DPXOrientationHeader     orientation;
  DPXFilmHeader        film;
  DPXTelevisionHeader     video;
  //DPXUserDefined    user;
};

/* SMPTE DPX format is (almost)exactly the same as Cineon except
   for the size of the header components:
    Data Type:    Cineon        SMPTE DPX
    -------------------------------------
    Image Header:    2048        2048
    PostageStamp:    24576        24608
    Padding:    5632        5600
    Total Header:    32256        32256
    -------------------------------------
    Image Data:    Variable    Variable
 */
