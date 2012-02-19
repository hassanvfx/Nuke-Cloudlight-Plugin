// This file contains code which is common to movReader and movWriter.

// Platforms to add to QuickTime's list
enum {
  kPlatformAny,
};

// Architectures we might be running on
enum Architecture {
  kArchitectureAny,
  kArchitectureIntel,
  kArchitecturePPC
};

// Flags indicating special cases for problem codecs
enum {
  kNo64ARGBPixelFormat = (1 << 0), // Don't use 64bit ARGB - it's broken
  kNo422YpCbCr8PixelFormat = (1 << 1), // Don't use 2vuy - it's broken
  kNo4444YpCbCrA8RPixelFormat = (1 << 2), // Don't use r408 - it's broken
  k64ARGBPixelFormatNativeByteOrder = (1 << 4), // Codec uses native byte order for 64bit ARGB (should be big-endian)
  kNo4444YpCbCrAFPixelFormat = (1 << 8), // Don't use r4fl - it's broken
  kDontTagGamma = (1 << 5), // Don't tag the movie with the gamma level
};

typedef struct
{
  OSType componentType;
  OSType componentSubType;
  OSType componentManufacturer;
  int platform;
  Architecture architecture;
  int flags;
} ComponentFlags;

// The list of dodgy codecs. We might want to make this a text file that users can edit when they acquire a new codec down the market.
static ComponentFlags gComponentFlags[] = {
  { decompressorComponentType, 'r210', '2GMB', kPlatformAny, kArchitectureAny, kNo64ARGBPixelFormat },
  { decompressorComponentType, 0,      'BMAG', kPlatformMacintosh, kArchitectureIntel, k64ARGBPixelFormatNativeByteOrder },
  { decompressorComponentType, 0,      'Ajav', kPlatformMacintosh, kArchitectureIntel, k64ARGBPixelFormatNativeByteOrder },
  { decompressorComponentType, 'mx3n',  'appl', kPlatformAny, kArchitectureAny, kNo4444YpCbCrA8RPixelFormat },
  { decompressorComponentType, 'mx4n',  'appl', kPlatformAny, kArchitectureAny, kNo4444YpCbCrA8RPixelFormat },
  { decompressorComponentType, 'mx5n',  'appl', kPlatformAny, kArchitectureAny, kNo4444YpCbCrA8RPixelFormat },

  { decompressorComponentType, '2vuy',  'Ajav', kPlatformAny, kArchitectureAny, kNo422YpCbCr8PixelFormat | kNo64ARGBPixelFormat },
  { decompressorComponentType, '2Vuy',  'Ajav', kPlatformAny, kArchitectureAny, kNo422YpCbCr8PixelFormat | kNo64ARGBPixelFormat },

  { compressorComponentType, 'CFHD',  'cine', kPlatformAny, kArchitectureAny, kNo4444YpCbCrAFPixelFormat },
  { decompressorComponentType, 'CFHD',  'cine', kPlatformAny, kArchitectureAny, kNo4444YpCbCrAFPixelFormat },
  { 0 }
};

enum {
  // Apple doesn't provide an enum for the r4fl pixel format, so we make one up....
  k4444YpCbCrAFPixelFormat = 'r4fl'
};

// Given a compressor or decompressor, return our preferred pixel format and some flags indicating codec capabilities (or more likely lack thereof).
static void getCodecInfo( Component codec, OSType* pixelFormat, int* flags )
{
  *flags = 0;
  ComponentDescription cd;
  if ( GetComponentInfo( codec, &cd, NULL, NULL, NULL ) == noErr ) {
    for ( int i = 0; gComponentFlags[i].componentType; i++ ) {
      ComponentFlags* f = &gComponentFlags[i];
      if (
        (f->componentType == 0 || f->componentType == cd.componentType) &&
        (f->componentSubType == 0 || f->componentSubType == cd.componentSubType) &&
        (f->componentManufacturer == 0 || f->componentManufacturer == cd.componentManufacturer )
#if FN_OS_MAC
        && (f->platform == kPlatformAny || f->platform == kPlatformMacintosh)
#endif
#ifdef FN_OS_WINDOWS
        && (f->platform == kPlatformAny || f->platform == kPlatformWindows)
#endif
#ifdef FN_PROCESSOR_PPC
        && (f->architecture == kArchitectureAny || f->architecture == kArchitecturePPC)
#endif
#ifdef FN_PROCESSOR_INTEL
        && (f->architecture == kArchitectureAny || f->architecture == kArchitectureIntel)
#endif
        ) {
        *flags = f->flags;
        break;
      }
    }
  }

  *pixelFormat = 0;
  OSTypePtr* hResource = NULL;
  OSErr err = GetComponentPublicResource( codec, 'cpix', 1, (Handle*)&hResource );
  if ( err == noErr && hResource ) {
    int count = GetHandleSize( (Handle)hResource ) / 4;

    static OSType preferredFormats[] = {
      k4444YpCbCrAFPixelFormat,
      k4444YpCbCrA8RPixelFormat,
      k422YpCbCr8PixelFormat,
      k64ARGBPixelFormat,
      k32ARGBPixelFormat
    };

    unsigned int bestFormat = INT_MAX;
    for ( int i = 0; i < count; i++ ) {
      OSType format = (*hResource)[i];

      for ( unsigned int j = 0; j < sizeof(preferredFormats) / sizeof(OSType); j++ ) {
        // Some codecs claim to support particular formats, but get it wrong.
        if ( format == k64ARGBPixelFormat && (*flags & kNo64ARGBPixelFormat) )
          continue;
        if ( format == k64ARGBPixelFormat && (*flags & kNo64ARGBPixelFormat) )
          continue;
        if ( format == k4444YpCbCrA8RPixelFormat && (*flags & kNo4444YpCbCrA8RPixelFormat) )
          continue;
        if ( format == k422YpCbCr8PixelFormat && (*flags & kNo422YpCbCr8PixelFormat) )
          continue;
        if ( format == k4444YpCbCrAFPixelFormat && (*flags & kNo4444YpCbCrAFPixelFormat) )
          continue;
        if ( format == preferredFormats[j] && j < bestFormat ) {
          *pixelFormat = format;
          bestFormat = j;
        }
      }
    }

    DisposeHandle( (Handle)hResource );
  }
  if ( *pixelFormat == 0 )
    *pixelFormat = k32ARGBPixelFormat;

  if ( *pixelFormat == k4444YpCbCrAFPixelFormat ) {
    // Now a magical special case. It turns out, after long investigation, that although the r4fl pixel format is
    // registered with Core Video, it's not registered with ICM. If we find this to be the case, register it ourselves.
    // If we don't register it, attempts to use it with codecs result in cDepthErr.
    ICMPixelFormatInfo pixelFormatInfo;
    pixelFormatInfo.size = sizeof(ICMPixelFormatInfo);
    OSErr err = ICMGetPixelFormatInfo( *pixelFormat, &pixelFormatInfo );
    if ( err != noErr ) {
      memset( &pixelFormatInfo, 0, sizeof(pixelFormatInfo) );
      pixelFormatInfo.size  = sizeof(ICMPixelFormatInfo);
      pixelFormatInfo.formatFlags = 0;
      pixelFormatInfo.bitsPerPixel[0] = 128;

      err = ICMSetPixelFormatInfo( *pixelFormat, &pixelFormatInfo );
    }
  }
}
