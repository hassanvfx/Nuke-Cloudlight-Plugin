// yuvReader.C

// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.
// Permission is granted to reuse portions or all of this code for the
// purpose of implementing Nuke plugins, or to demonstrate or document
// the methods needed to implemente Nuke plugins.

// Reads those video files known as yuv, abekas, accom, quatel, d1,
// 4:2:2, etc.  Also reads interlaced versions of these files,
// recognized by the name "sdl"

// This is also an example of a file-reading plugin.

#include "DDImage/FileReader.h"
#include "DDImage/Row.h"
#include <sys/stat.h> // needed to measure file size

using namespace DD::Image;

class yuvReader : public FileReader
{
  bool interlaced;

  MetaData::Bundle _meta;
  const MetaData::Bundle& fetchMetaData(const char* key)
  {
    return _meta;
  }

public:
  yuvReader(Read*, bool interlaced,
            int fd, const unsigned char*, int, int bits = 16, bool bug = false);
  ~yuvReader();
  void engine(int y, int x, int r, ChannelMask, Row &);
  void open();
  static const Reader::Description d;
  static const Reader::Description sdld;
};

// You must write a test function that tells Nuke if a file really is of
// the type that you can read. Usually this will work by examining data
// near the start of the file, which is passed by the block pointer and
// with the integer length. You can also read data from the file descriptor,
// but be sure to seek it back to length.
//
// If the user types "yuv:name" it will only try the yuv test and will fail
// if the file does not pass.
//
// In the more common case where the user types "name.yuv",
// although the filename will be used to select which test to try first,
// Nuke will go through all known file types and call the test to try to
// figure out what format a file is. This allows a file to be read even
// if it is misnamed.
//
// Yuv is unusual in that there is no data, but the files can be identified
// by a very specific length, which I determine from the fd:
static unsigned cachesize; // remembered
static bool test(int fd, const unsigned char* block, int length)
{
  struct stat buf;
  fstat(fd, &buf);
  cachesize = buf.st_size;
  if (cachesize % 1440)
    return false;                     // must be an even multiple of 720 pixels
  if (cachesize < 100 * 1440)
    return false;                         // also make sure it has some height
  return true;
}

// This is the function called to from the description to create the reader:
static Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
  return new yuvReader(iop, false, fd, b, n);
}

// The description has a null-separated list of possilble names to call
// this from, and pointers to the test and build procedures:
const Reader::Description yuvReader::d("yuv\0", build, test);

// I also provide "sdl" files, which are the same format except the lines
// are in the file in interlaced order. In order for Nuke to see this, a
// plugin named "sdlReader" must be created that loads this one, usually
// by just putting "load yuvReader" into a .tcl plugin:
static Reader* buildsdl(Read* iop, int fd, const unsigned char* b, int n)
{
  return new yuvReader(iop, true, fd, b, n);
}
const Reader::Description yuvReader::sdld("sdl\0", buildsdl, test);

// The constructor should read any interesting information out of the
// file and set the info_ structure by calling set_info() and then
// perhaps modifying it further.
// In this case I use the size measured by the test() function to set
// the number of rows.
yuvReader::yuvReader(Read* iop, bool interlaced,
                     int fd, const unsigned char* block, int len, int b, bool bug)
  : FileReader(iop, fd, block, len)
{
  this->interlaced = interlaced;
  set_info(720, cachesize / 1440, 3);
  // set which direction is most efficient to read. 1 for up, -1 for down:
  info_.ydirection(-1);

  _meta.setData(MetaData::DEPTH, MetaData::DEPTH_8);
}

// frame() is called before the first call to engine(). This can be
// used to read in information that is not needed to fill in the info_
// in the constructor. For some formats this will read in all the
// pixels.  Because Nuke sets a mutex around the calls to this code
// you don't have to worry about multithreading issues in here.
// If this is a movie format, you should implement the animated(double,double)
// function and return true, and this will be called when the frame
// number changes. You can call frame() to get the current frame number.
// For YUV files nothing needs to be done.
void yuvReader::open() {}

// If you allocate tables in the constructor or in frame() you will need
// to make the destructor get rid of them, so memory does not leak:
yuvReader::~yuvReader()
{
  // nothing
}

// The engine reads individual rows out of the input.
void yuvReader::engine(int y, int x, int xr, ChannelMask channels, Row& row)
{

  // Usually it is easier to deal with the whole width of the file. This
  // modifies the line to the full width if a smaller amount was requested:
  row.range(0, 720);

  // Figure out the line number of the input file we want:
  int inputy = height() - y - 1;
  if (interlaced) { // this is the normal state
    if (inputy % 2)
      inputy = (height() + inputy) / 2;
    else
      inputy = (inputy / 2);
  }

  // set up pointers to the output:
  float* R = row.writable(Chan_Red);
  float* G = row.writable(Chan_Green);
  float* B = row.writable(Chan_Blue);

  // Read in the part of the file we need. This block of the file will
  // remain in memory until we call unlock(). This locking mechanism
  // allows multiple threads to read the file at once.
  // The other 2 arguments are the minimum and maximum length that
  // must be locked, it will try to lock the maximum but won't produce
  // an error unless less than the minimum is available.
  FILE_OFFSET offset = inputy * 1440;
  FILE_OFFSET end = offset + 1440;
  lock(offset, 1440, 1440);

  // The YUV matrix multiplication is done in file space, not linear space.
  // So first we convert the read bytes to floating point values in the
  // 0-255 range (actually values outside that range work as well)
  while (offset < end) {
    int y, u, v;

    u = byte(offset++) - 128;
    y = byte(offset++) - 16;
    //    if (y<0) y = 0;
    v = byte(offset++) - 128;

    float Y = (1.1644f / 255.0f) * y;
    *R++ = Y + (1.5966f / 255.0f) * v;
    *G++ = Y - (0.391998f * u + .813202f * v) / 255.0f;
    *B++ = Y + (2.0184f / 255.0f) * u;

    y = byte(offset++) - 16;
    //    if (y<0) y = 0;

    Y = (1.1644f / 255.0f) * y;
    *R++ = Y + (1.5966f / 255.0f) * v;
    *G++ = Y - (0.391998f * u + .813202f * v) / 255.0f;
    *B++ = Y + (2.0184f / 255.0f) * u;
  }

  // do this as soon as possible so other threads that are blocked
  // can start reading data from the file:
  unlock();

  // Now convert the floating-point values into linear 0-1 values:
  for (Channel z = Chan_Red; z <= Chan_Blue; incr(z)) {
    float* P = row.writable(z);
    from_float(z, P, P, 0, 720);
  }
}
