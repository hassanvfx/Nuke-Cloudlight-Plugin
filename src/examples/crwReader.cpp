// crwReader.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

/* Reads crw files via popen of dcraw conversion tool.  (Really is
   reading 16bit P6 format PPM files)

   04/14/03     Initial Release                Charles Henrich (henrich@d2.com)
   09/14/06     Indentation, removed unused variables and unnecessary knobs
 */

#ifdef _WIN32
  #define _WINSOCKAPI_
#endif

#include "DDImage/Reader.h"
#include "DDImage/Row.h"
#include "DDImage/Thread.h"
#include "DDImage/DDString.h"
#include "DDImage/MetaData.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#ifdef _WIN32
  #define ushort unsigned short
  #define popen _popen
  #define pclose _pclose
#endif

using namespace DD::Image;

class crwReader : public Reader
{

  int C_ppmwidth, C_ppmheight, C_ppmmaxval;
  ushort* C_image_cache;
  void barf(const char* command)
  {
    iop->error("\nError running %s\n"
               "If you have the \"dcraw\" software installed, make sure that it's in your path.\n"
               "If you don't have it, the latest version is available as source from:\n"
               "    http://www.cybercom.net/~dcoffin/dcraw/\n"
               "where you can also find links to precompiled versions for Windows and OSX."
               , command);
  }

public:

  MetaData::Bundle _meta;
  const MetaData::Bundle& fetchMetaData(const char* key)
  {
    return _meta;
  }

  crwReader(Read*, int fd);
  ~crwReader();
  void engine(int y, int x, int r, ChannelMask, Row &);
  static const Description d;

};

static bool test(int fd, const unsigned char* block, int length)
{
  /* Figure out test later XXX */
  return true;
}

static Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
  return new crwReader(iop, fd);
}

const Reader::Description crwReader::d("crw\0cr2\0", build, test);

crwReader::crwReader(Read* r, int fd) : Reader(r)
{

  C_image_cache = NULL;

  info_.ydirection(-1);

  char command[BUFSIZ];
  snprintf(command, BUFSIZ, "dcraw -4 -c \"%s\"", filename());
#ifdef _WIN32
  FILE* pipe = popen(command, "rb");
#else
  FILE* pipe = popen(command, "r");
#endif

  if (!pipe) {
    barf(command);
    return;
  }
  printf("crwReader: %s\n", command);

  char magic[4];
  magic[0] = '\0';
  // put some dummy values in so if reading fails it does not crash:
  C_ppmwidth = 640;
  C_ppmheight = 480;

  fscanf(pipe, "%3s %d %d %d", magic, &C_ppmwidth, &C_ppmheight, &C_ppmmaxval);

  if (strcmp(magic, "P6") != 0) {
    pclose(pipe);
    barf(command);
    return;
  }
  printf("crwReader: reading pixels\n");

  int numpixels = C_ppmwidth * C_ppmheight * 3;
  C_image_cache = new ushort[numpixels];

  fgetc(pipe); /* Skip whitespace char after header */

  int numread = fread(C_image_cache, 2, numpixels, pipe);
  if (numread < numpixels) {
    if (numread < 1)
      barf(command);
    else
      iop->error("dcraw only returned %d of the %d samples needed",
                 numread, numpixels);
  }

  frommsb(C_image_cache, numpixels);

  pclose(pipe);
  printf("crwReader: done\n");

  set_info(C_ppmwidth, C_ppmheight, 3);

  _meta.setData(MetaData::DEPTH, MetaData::DEPTH_16);
}

crwReader::~crwReader()
{
  delete[] C_image_cache;
}

void crwReader::engine(int y, int x, int r, ChannelMask channels, Row& row)
{
  int xcount;
  int cacheoffset = 0;
  float* dstpixrow;

  if (!C_image_cache) {
    row.erase(channels);
    return;
  }

  y = height() - 1 - y;

  foreach(z, channels) {
    dstpixrow = row.writable(z);
    cacheoffset = (y * C_ppmwidth + x) * 3 + (z - 1);
    for (xcount = x; xcount < r; xcount++) {
      dstpixrow[xcount] = float(C_image_cache[cacheoffset] * 1.0 / C_ppmmaxval);
      cacheoffset += 3;
    }
  }
}
