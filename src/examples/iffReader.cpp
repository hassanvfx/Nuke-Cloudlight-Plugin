// iffReader.C

////////////////////////////////////////////////////////////////
/*
 * Maya IFF image reading and writing code
 * Copyright (C) 1997-1999 Mike Taylor
 * (email: mtaylor@aw.sgi.com, WWW: http://reality.sgi.com/mtaylor)
 * Copyright (C) 2003 Luke Tokheim
 * Copyright (C) 2003-2009 The Foundry Visionmongers Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Note: this license applies only to this plugin, and not to the rest
 * of DDImage, Nuke, etc.
 */
////////////////////////////////////////////////////////////////

#include "DDImage/Reader.h"
//#define __IFF_DEBUG_

typedef struct _iff_image
{
  unsigned width;   //!< Image width.
  unsigned height;  //!< Image height.
  unsigned depth;   //!< Color format, bytes per color pixel.
  unsigned datatype; //!< 0=byte, 1=short, 3=float?
  unsigned char* rgba;    //!< The color data of size width*height*depth.
  unsigned short* srgba;    //!< data if datatype==1
  float* frgba;    //!< data if datatype==3
  float znear;  //!< The near clipping plane for the z buffer data.
  float zfar;   //!< The far clipping plane for the z buffer data.
  /*!
     Access pixel x,y as zbuffer[width*y + x]. (Starting from the top left.)

     The stored values now are -1/z components in light eye space. When
     reading a z depth value back, one thus need to compute (-1.0 / z)
     to get the real z component in eye space. This format is the same as
     the camera depth format. This format is always used, whatever the light
     type is. In order to be able to compute real 3D distances of the shadow
     samples there's an IFF tag: 'ESXY' 'Eye Space X Y' values are two float
     values, the first one is the width size, the second one is the height size
     matching the map width and map height. When reading the shadow map buffer,
     one can thus convert from the pixel coordinates to the light eye space
     xy coords. If the pixel coordinates are considered to be normalized in
     the [-1.0, 1.0] range, the eye space xy are given by:

      X_light_eye_space = 3D normalized_pixel_x * (width / 2.0)

      Y_light_eye_space = 3D normalized_pixel_y * (heigth / 2.0)

     Once one get the (X,Y,Z) light eye space coordinates, the true 3D
     distance from the light source is:

      true_distance = 3D sqrt(X=B2 + Y=B2 + Z=B2)

     (This was copied from the Alias|Wavefront API Knowledge Base.)
   */
  float* zbuffer;  //!< The z buffer data of size width*height.
  /*!
     Eye x-ratio. This is a depth map specific field used to
     compute the xy eye coordinates from the normalized pixel.
   */
  float zesx;
  /*!
     Eye y-ratio. This is a depth map specific field used to
     compute the xy eye coordinates from the normalized pixel.
   */
  float zesy;
  /*!
     This does not work right now! I do not know how to interpret these vectors.
     If you can figure it out, please let me know.
   */
  float* blurvec;  //!< The packed xy motion blur vectors of size 2*width*height.
} iff_image;


iff_image* iff_load( const char* filename );
void iff_free( iff_image* image );
unsigned iff_get_error();

/*////////////////////////////////////////////////////////////// */

#include "DDImage/DDWindows.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char uByte;
typedef unsigned int uInt32;
typedef unsigned short uInt16;
typedef float Float32;

#define RGB_FLAG     (1)
#define ALPHA_FLAG   (2)
#define ZBUFFER_FLAG (4)

#define CHUNK_STACK_SIZE (32)

/* Error code definitions */
#define IFF_NO_ERROR     (0)
#define IFF_OPEN_FAILS   (1)
#define IFF_READ_FAILS   (2)
#define IFF_BAD_TAG      (3)
#define IFF_BAD_COMPRESS (4)
#define IFF_BAD_STACK    (5)
#define IFF_BAD_CHUNK    (6)

/* Define the IFF tags we are looking for in the file. */
const uInt32 IFF_TAG_CIMG = ('C' << 24) | ('I' << 16) | ('M' << 8) | ('G');
const uInt32 IFF_TAG_FOR4 = ('F' << 24) | ('O' << 16) | ('R' << 8) | ('4');
const uInt32 IFF_TAG_TBHD = ('T' << 24) | ('B' << 16) | ('H' << 8) | ('D');
const uInt32 IFF_TAG_TBMP = ('T' << 24) | ('B' << 16) | ('M' << 8) | ('P');
const uInt32 IFF_TAG_RGBA = ('R' << 24) | ('G' << 16) | ('B' << 8) | ('A');
const uInt32 IFF_TAG_CLPZ = ('C' << 24) | ('L' << 16) | ('P' << 8) | ('Z');
const uInt32 IFF_TAG_ESXY = ('E' << 24) | ('S' << 16) | ('X' << 8) | ('Y');
const uInt32 IFF_TAG_ZBUF = ('Z' << 24) | ('B' << 16) | ('U' << 8) | ('F');
const uInt32 IFF_TAG_BLUR = ('B' << 24) | ('L' << 16) | ('U' << 8) | ('R');
const uInt32 IFF_TAG_BLRT = ('B' << 24) | ('L' << 16) | ('R' << 8) | ('T');
//const uInt32 IFF_TAG_HIST = ('H' << 24) | ('I' << 16) | ('S' << 8) | ('T');


/* For the stack of chunks */
typedef struct _iff_chunk
{
  uInt32 tag;
  uInt32 start;
  uInt32 size;
  uInt32 chunkType;
} iff_chunk;

iff_chunk chunkStack[CHUNK_STACK_SIZE];
int chunkDepth = -1;

/* The current error state. */
unsigned iff_error = IFF_NO_ERROR;


// -- Function prototypes, local to this file.
iff_chunk iff_begin_read_chunk( FILE* file );
void iff_end_read_chunk( FILE* file );
uByte* iff_decompress_rle( FILE* file, uInt32 numBytes, uByte* compressedData,
                           uInt32 compressedDataSize, uInt32* compressedIndex );
uByte* iff_decompress_tile_rle( FILE* file, uInt16 width, uInt16 height, uInt16 depth,
                                uByte* compressedData, uInt32 compressedDataSize );
uByte* iff_read_uncompressed_tile( FILE* file,
                                   uInt16 width, uInt16 height, uInt16 depth );

/*
 * -- Basic input functions.
 *
 */

uInt16 iff_get_short( FILE* file )
{
  uByte buf[2];
  size_t result = 0;

  result = fread( buf, 2, 1, file);

  if ( result != 1 ) {
    if ( iff_error == IFF_NO_ERROR ) {
      assert(false);
      iff_error = IFF_READ_FAILS;
    }
    return 0 ;
  }

  return ( buf[0] << 8 ) + ( buf[1] ) ;
}

uInt32 iff_get_long( FILE* file )
{
  uByte buffer[4];

  size_t result = fread( buffer, 4, 1, file );
  if ( result != 1 ) {
    if ( iff_error == IFF_NO_ERROR ) {
      assert(false);
      iff_error = IFF_READ_FAILS;
    }
    return 0 ;
  }

  return ( buffer[0] << 24 ) + ( buffer[1] << 16 )
         + ( buffer[2] << 8 ) + ( buffer[3] << 0 ) ;
}

Float32 iff_get_float( FILE* file )
{
  uByte buffer[4];
  uInt32 value;

  size_t result = fread( buffer, 4, 1, file );
  if ( result != 1 ) {
    if ( iff_error == IFF_NO_ERROR ) {
      assert(false);
      iff_error = IFF_READ_FAILS;
    }
    return 0.0 ;
  }

  value = ( buffer[3] << 24 ) + ( buffer[2] << 16 )
          + ( buffer[1] << 8 ) + ( buffer[0] << 0 );

  return *((Float32*)&value) ;
}

/*
 * IFF Chunking Routines.
 *
 */

iff_chunk iff_begin_read_chunk( FILE* file )
{
  chunkDepth++;
  if ( (chunkDepth >= CHUNK_STACK_SIZE) || (chunkDepth < 0) ) {
    if ( iff_error == IFF_NO_ERROR ) {
      iff_error = IFF_BAD_STACK;
    }
    return chunkStack[0] ;
  }

  chunkStack[chunkDepth].start = ftell( file );
  chunkStack[chunkDepth].tag = iff_get_long( file );
  chunkStack[chunkDepth].size = iff_get_long( file );

  if ( chunkStack[chunkDepth].tag == IFF_TAG_FOR4 ) {
    // -- We have a form, so read the form type tag as well.
    chunkStack[chunkDepth].chunkType = iff_get_long( file );
  }
  else {
    chunkStack[chunkDepth].chunkType = 0;
  }

#if 0 //def __IFF_DEBUG_
  printf( "Beginning Chunk: %c%c%c%c",
          (((&chunkStack[chunkDepth].tag)[0] >> 24) & 0xFF),
          (((&chunkStack[chunkDepth].tag)[0] >> 16) & 0xFF),
          (((&chunkStack[chunkDepth].tag)[0] >> 8) & 0xFF),
          (((&chunkStack[chunkDepth].tag)[0] >> 0) & 0xFF));

  printf("  start: %u", chunkStack[chunkDepth].start );
  printf("  size: %u", chunkStack[chunkDepth].size );
  if ( chunkStack[chunkDepth].chunkType != 0 ) {
    printf("  type: %c%c%c%c",
           (((&chunkStack[chunkDepth].chunkType)[0] >> 24) & 0xFF),
           (((&chunkStack[chunkDepth].chunkType)[0] >> 16) & 0xFF),
           (((&chunkStack[chunkDepth].chunkType)[0] >> 8) & 0xFF),
           (((&chunkStack[chunkDepth].chunkType)[0] >> 0) & 0xFF));
  }
  printf( "  depth: %d\n", chunkDepth );
#endif

  return chunkStack[chunkDepth] ;
}

void iff_end_read_chunk( FILE* file )
{
  uInt32 end = chunkStack[chunkDepth].start + chunkStack[chunkDepth].size + 8;
  int part;

  if ( chunkStack[chunkDepth].chunkType != 0 ) {
    end += 4;
  }
  // Add padding
  part = end % 4;
  if ( part != 0 ) {
    end += 4 - part;
  }

  fseek( file, end, SEEK_SET );

#if 0 //def __IFF_DEBUG_
  printf( "Closing Chunk: %c%c%c%c\n\n",
          (((&chunkStack[chunkDepth].tag)[0] >> 24) & 0xFF),
          (((&chunkStack[chunkDepth].tag)[0] >> 16) & 0xFF),
          (((&chunkStack[chunkDepth].tag)[0] >> 8) & 0xFF),
          (((&chunkStack[chunkDepth].tag)[0] >> 0) & 0xFF) );
#endif

  chunkDepth--;
}




/*
 * Compression Routines
 *
 */

void decompress_rle(uByte* data, uInt32 delta, uInt32 numBytes,
                    uByte* compressedData,
                    uInt32 compressedDataSize,
                    uInt32* compressedIndex )
{
#if 0 //def __IFF_DEBUG_
  printf( "Decompressing data %d\n", numBytes );
#endif

  uInt32 FROM = *compressedIndex;
  uInt32 TO = 0;

  while (TO < numBytes) {

    if (FROM >= compressedDataSize) {
      printf("NOT ENOUGH COMPRESSED DATA\n");
      while (TO < numBytes)
        data[delta * TO++] = 0;
      break;
    }

    uByte nextChar = compressedData[FROM++];
    unsigned count = (nextChar & 0x7f) + 1;

    if ( ( TO + count ) > numBytes ) {
      printf("COUNT BAD, %d+%d > %d\n", TO, count, numBytes);
      count = numBytes - TO;
      //       printf("Start at %d of %d: ", *compressedIndex, compressedDataSize);
      //       for (uInt32 i = *compressedIndex; i <= FROM;) {
      //     uByte n = compressedData[i++];
      //     printf("%02x ", n);
      //     if (n & 0x80) i++;
      //     else {i += (n&0x7f) + 1;}
      //       }
      //       printf("\n");
    }

    if ( nextChar & 0x80 ) {

      // We have a duplication run

      nextChar = compressedData[FROM++];
      for (uInt32 i = 0; i < count; ++i )
        data[delta * TO++] = nextChar;

    }
    else {

      // We have a verbatim run
      for (uInt32 i = 0; i < count; ++i )
        data[delta * TO++] = compressedData[FROM++];
    }
  }

  *compressedIndex = FROM;
}

uByte* read_tile(FILE* file, int size, int depth, int datasize, int* offsets)
{
  uByte* result = (uByte*)malloc( size * depth );
  if (datasize >= size * depth) {
    fread(result, 1, size * depth, file);
  }
  else {
    // compressed tile
    uByte* data = (uByte*)malloc(datasize);
    datasize = int(fread(data, 1, datasize, file));
    uInt32 index = 0;
    for (int i = 0; i < depth; i++)
      decompress_rle(result + offsets[i], depth, size,
                     data, datasize, &index);
    free(data);
  }
  return result;
}

iff_image* iff_load( const char* filename )
{
  iff_chunk chunkInfo;
  iff_image* image;

  // -- Header info.
  uInt32 width, height, depth, npixels;
  uInt32 flags, compress;
  uInt16 tiles;
  uInt16 tbmpfound;
  uInt16 datatype;

  uInt16 x1, x2, y1, y2, tile_width, tile_height;
  uInt32 tile;
  uInt32 ztile;

  uInt32 i;
  uInt32 remainingDataSize;

  uInt32 oldSpot;
  uInt32 fileLength;


  FILE* file;


  // -- Initialize the top of the chunk stack.
  chunkDepth = -1;
  image = 0;

  // -- Open the file.
  file = fopen( filename, "rb" );
  if ( !file ) {
    if ( iff_error == IFF_NO_ERROR ) {
      iff_error = IFF_OPEN_FAILS;
    }
    return 0 ;
  }

  // -- File should begin with a FOR4 chunk of type CIMG
  chunkInfo = iff_begin_read_chunk( file );
  if ( chunkInfo.chunkType != IFF_TAG_CIMG ) {
    if ( iff_error == IFF_NO_ERROR ) {
      // -- This is not a CIMG, it is not an IFF Image.
      iff_error = IFF_BAD_TAG;
    }
    return 0 ;
  }


  /*
   * Read the image header
   * OK, we have a FOR4 of type CIMG, look for the following tags
   *        FVER
   *        TBHD    bitmap header, definition of size, etc.
   *        AUTH
   *        DATE
   */
  while ( 1 ) {

    chunkInfo = iff_begin_read_chunk( file );

    // -- Right now, the only info we need about the image is in TBHD
    // -- so search this level until we find it.
    if ( chunkInfo.tag == IFF_TAG_TBHD ) {

      // -- Header chunk found
      width = iff_get_long( file );
      height = iff_get_long( file );
      iff_get_short( file ); // -- Don't support
      iff_get_short( file ); // -- Don't support
      flags = iff_get_long( file );
      datatype = iff_get_short( file );
      tiles = iff_get_short( file );
      compress    = iff_get_long( file );

#ifdef __IFF_DEBUG_
      printf( "****************************************\n" );
      printf( "Width: %u\n", width);
      printf( "Height: %u\n", height);
      printf( "flags: 0x%X\n", flags);
      printf( "datatype: %d\n", datatype);
      printf( "tiles: %hu\n", tiles);
      printf( "compress: %u\n", compress);
      printf( "****************************************\n" );
#endif

      iff_end_read_chunk( file );

      if ( compress > 1 ) {
        if ( iff_error == IFF_NO_ERROR ) {
          iff_error = IFF_BAD_COMPRESS;
        }
        return 0 ;
      }

      break;
    }
    else {

#ifdef __IFF_DEBUG_
      // Skipping unused data at FOR4 <size> CIMG depth
      printf("Skipping Chunk: %c%c%c%c\n",
             (((&chunkStack[chunkDepth].tag)[0] >> 24) & 0xFF),
             (((&chunkStack[chunkDepth].tag)[0] >> 16) & 0xFF),
             (((&chunkStack[chunkDepth].tag)[0] >> 8) & 0xFF),
             (((&chunkStack[chunkDepth].tag)[0] >> 0) & 0xFF));
#endif

      iff_end_read_chunk( file );
    }
  } /* END find TBHD while loop */


  // -- Number of channels.
  depth = 0;

  if ( flags & RGB_FLAG ) {
    depth += 3;
  }

  if ( flags & ALPHA_FLAG ) {
    depth += 1;
  }


  npixels = width * height;


  // -- Allocate the image struct.
  image = (iff_image*)malloc( sizeof( iff_image ) );
  image->width = width;
  image->height = height;
  image->depth = depth;
  image->datatype = datatype;
  image->znear = 0.0;
  image->zfar =  0.0;
  image->zesx = 0.0;
  image->zesy = 0.0;
  image->rgba = 0;
  image->srgba = 0;
  image->frgba = 0;
  image->zbuffer = 0;
  image->blurvec = 0;

  if (datatype == 3) {
    image->frgba = (float*)malloc( npixels * depth * 4 );
    memset( image->frgba, 0, npixels * depth * 4 );
  }
  else if (datatype == 1) {
    image->srgba = (uInt16*)malloc( npixels * depth * 2 );
    memset( image->srgba, 0, npixels * depth * 2 );
  }
  else {
    image->rgba = (uByte*)malloc( npixels * depth);
    memset( image->rgba, 0, npixels * depth);
  }
  if ( flags & ZBUFFER_FLAG ) {
    image->zbuffer = (Float32*)malloc( npixels * sizeof( Float32 ) );
    memset( image->zbuffer, 0, npixels * sizeof( Float32 ) );
  }

  // -- Assume the next FOR4 of type TBMP
  tbmpfound = 0;


  // Read the tiled image data
  while ( !tbmpfound ) {

    chunkInfo = iff_begin_read_chunk( file );

    /*
     * OK, we have a FOR4 of type TBMP, (embedded FOR4)
     * look for the following tags
     *        RGBA    color data,    RLE compressed tiles of 32 bbp data
     *        ZBUF    z-buffer data, 32 bit float values
     *        CLPZ    depth map specific, clipping planes, 2 float values
     *        ESXY    depth map specific, eye x-y ratios, 2 float values
     *        HIST
     *        VERS
     *        FOR4 <size>    BLUR (twice embedded FOR4)
     */
    if ( chunkInfo.chunkType == IFF_TAG_TBMP ) {
      tbmpfound = 1;

      // Image data found
      tile = 0;
      ztile = 0;


#if 0 //def __IFF_DEBUG_
      printf( "Reading image tiles\n" );
#endif


      if ( !(flags & ZBUFFER_FLAG) ) {
        ztile = tiles;
      }

      if ( depth == 0 ) {
        tile = tiles;
      }


      // -- Read tiles
      while ( ( tile < tiles ) || ( ztile < tiles ) ) {

        chunkInfo = iff_begin_read_chunk( file );

        if ( !(chunkInfo.tag == IFF_TAG_RGBA) && !(chunkInfo.tag == IFF_TAG_ZBUF) ) {
          if ( iff_error == IFF_NO_ERROR ) {
            iff_error = IFF_BAD_CHUNK;
          }
          iff_free( image );
          return 0 ;
        }

        // Get tile size and location info
        x1 = iff_get_short( file );
        y1 = iff_get_short( file );
        x2 = iff_get_short( file );
        y2 = iff_get_short( file );

        remainingDataSize = chunkInfo.size - 8;

        tile_width = x2 - x1 + 1;
        tile_height = y2 - y1 + 1;

#if 0 //def __IFF_DEBUG_
        printf( "Tile x1: %hu  ", x1 );
        printf( "y1: %hu  ", y1 );
        printf( "x2: %hu  ", x2 );
        printf( "y2: %hu\n", y2 );
#endif

        // -- OK, we found an RGBA chunk, eat it.
        if ( chunkInfo.tag == IFF_TAG_RGBA ) {

          if ( depth == 0 ) {
            iff_end_read_chunk( file );
            continue;
          }

          if (datatype == 3) {
            static int offsets[4][16] = {
              { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
              { 0, 4, 1, 5, 2, 6, 3, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
              { 0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11, 12, 13, 14, 15 },
              { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 }
            };
            float* tileData = (float*)read_tile(file,
                                                tile_width * tile_height,
                                                4 * depth,
                                                remainingDataSize,
                                                offsets[depth - 1]);
            if ( tileData ) {
              DD::Image::Reader::frommsb((unsigned*)tileData, tile_width * tile_height * depth);
              float* from = tileData;
              for (unsigned i = 0; i < tile_height; i++) {
                float* to = image->frgba + depth * (width * (y1 + i) + x1);
                for (unsigned j = 0; j < tile_width; j++) {
                  for (unsigned k = 0; k < depth; k++)
                    to[k] = from[depth - k - 1];
                  to += depth;
                  from += depth;
                }
              }
              free( tileData );
            }


          }
          else if (datatype == 1) {
            static int offsets[4][8] = {
              { 0, 1, 2, 3, 4, 5, 6, 7 },
              { 0, 2, 1, 3, 4, 5, 6, 7 },
              { 0, 2, 4, 1, 3, 5, 6, 7 },
              { 0, 2, 4, 6, 1, 3, 5, 7 }
            };
            uInt16* tileData = (uInt16*)read_tile(file,
                                                  tile_width * tile_height,
                                                  2 * depth,
                                                  remainingDataSize,
                                                  offsets[depth - 1]);
            if ( tileData ) {
              DD::Image::Reader::frommsb(tileData, tile_width * tile_height * depth);
              uInt16* from = tileData;
              for (unsigned i = 0; i < tile_height; i++) {
                uInt16* to = image->srgba + depth * (width * (y1 + i) + x1);
                for (unsigned j = 0; j < tile_width; j++) {
                  for (unsigned k = 0; k < depth; k++)
                    to[k] = from[depth - k - 1];
                  to += depth;
                  from += depth;
                }
              }
              free( tileData );
            }

          }
          else {

            static int offsets[] = {
              0, 1, 2, 3
            };
            uByte* tileData = read_tile(file, tile_width * tile_height, depth,
                                        remainingDataSize, offsets);
            if ( tileData ) {
              uByte* from = tileData;
              for (unsigned i = 0; i < tile_height; i++) {
                uByte* to = image->rgba + depth * (width * (y1 + i) + x1);
                for (unsigned j = 0; j < tile_width; j++) {
                  for (unsigned k = 0; k < depth; k++)
                    to[k] = from[depth - k - 1];
                  to += depth;
                  from += depth;
                }
              }
              free( tileData );
            }
          }

          iff_end_read_chunk( file );
          tile++;

        } /* END RGBA chunk */

        // -- OK, we found a ZBUF chunk, eat it....hmmm, tasty
        else if ( chunkInfo.tag == IFF_TAG_ZBUF ) {
          static int offsets[] = {
            0, 1, 2, 3
          };
          float* tileData = (float*)read_tile(file, tile_width * tile_height, 4,
                                              remainingDataSize, offsets);

          if ( tileData ) {
            DD::Image::Reader::frommsb((unsigned*)tileData, tile_width * tile_height);
            const int base = y1 * width + x1;
            for (int i = 0; i < tile_height; i++) {
              for (int j = 0; j < tile_width; j++) {
                image->zbuffer[base + i * width + j] = tileData[i * tile_width + j];
              } /* End DEPTH dump */
            }
            free( tileData );
          }

          iff_end_read_chunk( file );
          ztile++;

        } /* END ZBUF chunk */

      } /* END while TBMP tiles */

    } /* END if TBMP */

    else {

#ifdef __IFF_DEBUG_
      // Skipping unused data IN THE BEGINNING OF THE FILE
      printf( "Skipping Chunk in search of TBMP: %c%c%c%c\n",
              (((&chunkStack[chunkDepth].tag)[0] >> 24) & 0xFF),
              (((&chunkStack[chunkDepth].tag)[0] >> 16) & 0xFF),
              (((&chunkStack[chunkDepth].tag)[0] >> 8) & 0xFF),
              (((&chunkStack[chunkDepth].tag)[0] >> 0) & 0xFF));
#endif

      iff_end_read_chunk( file );
    }
  }

  oldSpot = ftell( file ) ;
  fileLength = 0 ;
  fseek( file, 0, SEEK_END ) ;
  fileLength = ftell( file ) ;
  fseek( file, oldSpot, SEEK_SET );


  while ( 1 ) {

    if ( (width * height + ftell( file )) > fileLength ) {

#ifdef __IFF_DEBUG_
      printf ( "End of parsable data, time to quit\n" );
#endif

      break ;
    }

    chunkInfo = iff_begin_read_chunk( file );

    if ( chunkInfo.tag == IFF_TAG_CLPZ ) {

      image->znear = iff_get_float( file );
      image->zfar  = iff_get_float( file );

#ifdef __IFF_DEBUG_
      printf( "Got clipping info: %f %f\n", image->znear, image->zfar );
#endif

      iff_end_read_chunk( file );

    } /* END CLPZ chunk */

    else if ( chunkInfo.tag == IFF_TAG_ESXY ) {
      image->zesx = iff_get_float( file );
      image->zesy = iff_get_float( file );

#ifdef __IFF_DEBUG_
      printf( "Got esxy info: %f %f\n", image->zesx, image->zesy );
#endif

      iff_end_read_chunk( file );
    } /* END ESXY chunk */

    else if ( chunkInfo.tag == IFF_TAG_FOR4 ) {

      if ( chunkInfo.chunkType == IFF_TAG_BLUR ) {

        // -- FIXME: GET THE BLUR INFO HERE
        if ( image->blurvec ) {
          free( image->blurvec );
        }

        while ( 1 ) {

          chunkInfo = iff_begin_read_chunk( file );

          if ( chunkInfo.tag == IFF_TAG_BLRT ) {

            // read in values, uncompressed and in a linear sort
            // of manner, uh huh...
            /*
               printf( "%d\n", iff_get_long( file ) );
               printf( "%d\n", iff_get_long( file ) );
               printf( "%d\n", iff_get_long( file ) );
               printf( "%d\n", iff_get_long( file ) );
             */
            iff_get_long( file ); // -- Don't know what these are
            iff_get_long( file );
            iff_get_long( file );
            iff_get_long( file );



            image->blurvec = (Float32*)malloc( npixels * 2 * sizeof( Float32 ) );

            for ( i = 0; i < npixels; i++ ) {
              image->blurvec[2 * i] =   iff_get_float( file );
              image->blurvec[2 * i + 1] = iff_get_float( file );
            }

            iff_end_read_chunk( file );
            break;
          }

          else {
#ifdef __IFF_DEBUG_
            printf( "Skipping Chunk in search of BLRT: %c%c%c%c\n",
                    (((&chunkStack[chunkDepth].tag)[0] >> 24) & 0xFF),
                    (((&chunkStack[chunkDepth].tag)[0] >> 16) & 0xFF),
                    (((&chunkStack[chunkDepth].tag)[0] >> 8) & 0xFF),
                    (((&chunkStack[chunkDepth].tag)[0] >> 0) & 0xFF) );
#endif

            iff_end_read_chunk( file );
          }
        }
#ifdef __IFF_DEBUG_
        printf("Found FOR4 BLUR\n");
#endif
      }

      iff_end_read_chunk( file );
    }

    else {

#ifdef __IFF_DEBUG_
      // Skipping unused data IN THE BEGINNING OF THE FILE
      printf("Skipping Chunk in search of CLPZ: %c%c%c%c\n",
             (((&chunkStack[chunkDepth].tag)[0] >> 24) & 0xFF),
             (((&chunkStack[chunkDepth].tag)[0] >> 16) & 0xFF),
             (((&chunkStack[chunkDepth].tag)[0] >> 8) & 0xFF),
             (((&chunkStack[chunkDepth].tag)[0] >> 0) & 0xFF));
#endif

      iff_end_read_chunk( file );
    }

  }

  fclose( file );

  return image ;
}

void iff_free( iff_image* image )
{
  if ( image ) {

    if ( image->rgba ) {
      free( image->rgba );
    }
    if ( image->frgba ) {
      free( image->frgba );
    }
    if ( image->srgba ) {
      free( image->srgba );
    }
    if ( image->zbuffer ) {
      free( image->zbuffer );
    }
    if ( image->blurvec ) {
      free( image->blurvec );
    }

    free( image );

    image = 0;
  }
}

unsigned iff_get_error()
{
  unsigned err = iff_error;
  iff_error = IFF_NO_ERROR;
  return err ;
}

const char* iff_error_string( unsigned errorNumber )
{
  switch ( errorNumber ) {
    case ( IFF_NO_ERROR ):
      return "no error" ;
    case ( IFF_OPEN_FAILS ):
      return "cannot open file" ;
    case ( IFF_READ_FAILS ):
      return "cannot read file" ;
    case ( IFF_BAD_TAG ):
      return "unexpected tag" ;
    case ( IFF_BAD_COMPRESS ):
      return "unknown compression format" ;
    case ( IFF_BAD_STACK ):
      return "tag stack corrupt" ;
    case ( IFF_BAD_CHUNK ):
      return "unexpected chunk" ;
  }

  return "" ;
}

////////////////////////////////////////////////////////////////

#include "DDImage/Row.h"
#include <stdio.h>
#ifndef _WIN32
  #include <unistd.h>
#else
  #include <io.h>
#endif
#include <math.h>

using namespace DD::Image;

class iffReader : public Reader
{
  iff_image* image;
public:
  iffReader(Read*, int fd);
  ~iffReader();
  void engine(int y, int x, int r, ChannelMask, Row &);
  void open();
  static const Description d;

  MetaData::Bundle _meta;
  const MetaData::Bundle& fetchMetaData(const char* key)
  {
    return _meta;
  }
};

/** Check for the correct magic numbers at the start of the file
 */
static bool test(int fd, const unsigned char* block, int length)
{
  if (block[0] != 'F'
      || block[1] != 'O' // letter oh
      || block[2] != 'R'
      || block[3] != '4' )
    return false;
  // check if this is a picture file
  if (block[8] != 'C'
      || block[9] != 'I'
      || block[10] != 'M'
      || block[11] != 'G' ) {
    printf("Block is type %c%c%c%c\n", block[8], block[9], block[10], block[11]);
    return false;
  }
  // great, we passed all tests.
  return true;
}

static Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
  return new iffReader(iop, fd);
}

const Reader::Description iffReader::d("iff\0iff16\0", build, test);

iffReader::iffReader(Read* r, int fd) : Reader(r)
{
  close(fd);
  image = iff_load(filename());

  int err = iff_get_error();
  //    printf("err %s, width %d, height %d, depth %d\n",
  //     iff_error_string(err), image->width, image->height, image->depth);
  if (err != IFF_NO_ERROR) {
    iop->error(iff_error_string(err));
    return;
  }

  switch (image->datatype) {
    case 1:
      _meta.setData(MetaData::DEPTH, MetaData::DEPTH_16);
      break;
    case 3:
      _meta.setData(MetaData::DEPTH, MetaData::DEPTH_FLOAT);
      break;
    default:
      _meta.setData(MetaData::DEPTH, MetaData::DEPTH_8);
  }

  set_info(image->width, image->height, image->depth);
  if (image->zbuffer)
    info_.turn_on(Mask_Z);
  if (image->blurvec)
    info_.turn_on(Mask_UV);
  lut_ = LUT::getLut( image->datatype == 3 ? LUT::FLOAT :
                      image->datatype == 1 ? LUT::INT16 :
                      LUT::INT8 );
}

// delay anything unneeded for info_ until this is called:
void iffReader::open()
{
  Reader::open();
}

iffReader::~iffReader()
{
  iff_free(image);
}

void iffReader::engine(int y, int x, int r, ChannelMask channels, Row& row)
{

  const int depth = image->depth;
  int offset = y * image->width + x;
  if (image->datatype == 3) {
    const float* pixel = image->frgba + offset * depth;
    const float* alpha = depth > 3 ? pixel + 3 : 0;
    if (channels & Mask_Red)
      from_float(Chan_Red, row.writable(Chan_Red) + x, pixel + 0, alpha, r - x, depth);
    if (channels & Mask_Green)
      from_float(Chan_Green, row.writable(Chan_Green) + x, pixel + 1, alpha, r - x, depth);
    if (channels & Mask_Blue)
      from_float(Chan_Blue, row.writable(Chan_Blue) + x, pixel + 2, alpha, r - x, depth);
    if (channels & Mask_Alpha)
      from_float(Chan_Alpha, row.writable(Chan_Alpha) + x, pixel + 3, alpha, r - x, depth);

  }
  else if (image->datatype == 1) {
    const U16* pixel = image->srgba + offset * depth;
    const U16* alpha = depth > 3 ? pixel + 3 : 0;
    if (channels & Mask_Red)
      from_short(Chan_Red, row.writable(Chan_Red) + x, pixel + 0, alpha, r - x, 16, depth);
    if (channels & Mask_Green)
      from_short(Chan_Green, row.writable(Chan_Green) + x, pixel + 1, alpha, r - x, 16, depth);
    if (channels & Mask_Blue)
      from_short(Chan_Blue, row.writable(Chan_Blue) + x, pixel + 2, alpha, r - x, 16, depth);
    if (channels & Mask_Alpha)
      from_short(Chan_Alpha, row.writable(Chan_Alpha) + x, pixel + 3, alpha, r - x, 16, depth);

  }
  else {
    const uchar* pixel = image->rgba + offset * depth;
    const uchar* alpha = depth > 3 ? pixel + 3 : 0;
    if (channels & Mask_Red)
      from_byte(Chan_Red, row.writable(Chan_Red) + x, pixel + 0, alpha, r - x, depth);
    if (channels & Mask_Green)
      from_byte(Chan_Green, row.writable(Chan_Green) + x, pixel + 1, alpha, r - x, depth);
    if (channels & Mask_Blue)
      from_byte(Chan_Blue, row.writable(Chan_Blue) + x, pixel + 2, alpha, r - x, depth);
    if (channels & Mask_Alpha)
      from_byte(Chan_Alpha, row.writable(Chan_Alpha) + x, pixel + 3, alpha, r - x, depth);
  }

  if (channels & Mask_Z)
    from_float(Chan_Z, row.writable(Chan_Z) + x, image->zbuffer + offset, 0, r - x, 1);

  if (channels & Mask_U)
    from_float(Chan_Z, row.writable(Chan_U) + x, image->blurvec + 2 * offset, 0, r - x, 2);
  if (channels & Mask_V)
    from_float(Chan_Z, row.writable(Chan_V) + x, image->blurvec + 2 * offset + 1, 0, r - x, 2);
}
