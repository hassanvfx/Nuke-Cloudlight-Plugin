// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/DDWindows.h"
#include "DDImage/Reader.h"
#include "DDImage/Row.h"
#include "DDImage/Thread.h"
#include "DDImage/DDString.h"
#include "DDImage/Knobs.h"
#undef newstring


#ifdef _WIN32
  #include <Files.h>
  #include <Movies.h>
  #include <QTML.h>
  #include <GXMath.h>
  #include <MediaHandlers.h>
  #include <io.h>
  #define QUICKTIME_API 1
#elif defined(__APPLE__)
  #include <QuickTime/Movies.h>
  #include <QuickTime/QuickTimeComponents.h>
  #include <QuickTime/ImageCodec.h>
  #define QUICKTIME_API 1
#else
  #define QUICKTIME_API 0
extern "C" {
  // Building on Linux requires libgavl, and gmerlin from http://sourceforge.net/projects/gmerlin/
  #include <gmerlin/avdec.h>
  #include <gavl/gavl.h>
}
#endif
#include <algorithm>
#include <limits>

#if QUICKTIME_API
  #include "movCommon.h"
#endif

using namespace DD::Image;
using namespace std;


class movReader : public Reader
{
  MetaData::Bundle _meta;

  const MetaData::Bundle& fetchMetaData(const char* key)
  {
    return _meta;
  }

#ifdef __linux__
  bgav_t* movie_;
  int track_;
  int stream_;
  const gavl_video_format_t* format_;
  gavl_video_format_s outformat_;
  gavl_video_converter_t* converter_;
  unsigned short* imageBuffer_;
#else
  Movie movie_;
  Track track_;
  OSErr err_;
  unsigned codec_;
  float gamma_;
  OSType manufacturer_;
  int codecFlags_;

  ICMDecompressionSessionRef session_;

  CVPixelBufferRef pixelBuffer_;
#endif

  int framenum_;
  float aspect_;
  int depth_;
  float frame_duration_sec_;

  /*
     For simplicity sake, we're going to create a common set of
     functions for Linux, Windows, and Apple.
   */

  bool open_quicktime();
  bool find_video_track();
  void get_info();

  /*
     Pixel Format conversion functions
   */
  static void ycbcrconvert(unsigned char* src, float* r, float* g, float* b, int bytes);
  static void ycbcrconvert_mpeg(unsigned char* src, float* r, float* g, float* b, int bytes);
  static void yuvsconvert(unsigned char* src, float* r, float* g, float* b, int bytes);
  static void convert4444YpCbCrA8RPixelFormatToARGB( const unsigned char* src, float* a, float* r, float* g, float* b, int length );
  static void convert4444YpCbCrAFPixelFormatToARGB( const float* src, float* a, float* r, float* g, float* b, int length );

  static void ycbcrconvert_raw(unsigned char* src, float* r, float* g, float* b, int bytes);
  static void yuvsconvert_raw(unsigned char* src, float* r, float* g, float* b, int bytes);

#if QUICKTIME_API
  //Helper function for adding ints to a Core Foundation Dictionary
  void addIntToDictionary(CFMutableDictionaryRef, CFStringRef, SInt32);

  float pixelAspect(Media);

  /*
     Callback function for DecodeFrame. This function receives the
     decoded frames and transfers them to RGB before writing to
     imageBuffer.
   */
  static void emitFrame( void* reader, OSStatus result,
                         ICMDecompressionTrackingFlags flags,
                         CVPixelBufferRef pixelBuffer,
                         TimeValue64 displayTime, TimeValue64 displayDuration,
                         ICMValidTimeFlags validFlags,
                         void* reserved, void* frameData);

  /*
     Utility functions. These are used to break-up the work in
     'open' into more manageable pieces.
   */

  void createDecompressionSession(Media);
  std::string nativeFilename() const;
#endif

public:
  movReader(Read*, int fd);
  ~movReader();
  void engine(int y, int x, int r, ChannelMask, Row &);
  void open();
  static const Reader::Description d;

  // get metadata
#ifndef NO_META_DATA
  #if QUICKTIME_API
  void prefetchMetaData();
  #endif
#endif

  virtual bool videosequence() const { return true; }

#if QUICKTIME_API
  /*
     Since emitFrame is a static function that's only passed an instance
     of the reader, we have to supply some functions for allowing it
     to obtain and change class data.
   */
  void formaterror(char* error, char* format)
  {
    iop->error("%s: %c%c%c%c", error,
               format[3], format[2], format[1], format[0]);
  }

  bool raw() { return iop->raw(); }
#endif
};

static bool test(int fd, const unsigned char* block, int length)
{
  return true;
}


static Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
  return new movReader(iop, fd);
}


const Reader::Description movReader::d("mov\0", "QuickTime", build, test);

movReader::movReader(Read* r, int fd) :
  Reader(r),

  movie_(NULL),
#ifdef __linux__
  track_(-1),
  converter_(0),
  imageBuffer_(0),
#else
  track_(NULL),
  err_(noErr),
  session_(NULL),
  pixelBuffer_(0),
#endif
  framenum_(-1),
  aspect_(0.0f),
  depth_(4)
{

#if QUICKTIME_API
  #ifdef _WIN32
  //Initialize Quicktime
  if ( InitializeQTML(0L) != noErr ) {
    r->error("QuickTime not installed");
    return;
  }
  #endif
  if ( EnterMovies() != noErr ) {
    r->error("Couldn't initialize QuickTime");
    return;
  }
  lut_ = LUT::getLut(LUT::GAMMA1_8);
  codecFlags_ = 0;
#endif

  close(fd);  //I don't think we can use this

  /*
     Open the QuickTime movie.
   */
  if ( !open_quicktime() ) {
    r->error("Couldn't open movie for reading");
    return;
  }


  /*
     Find Video Track
   */
  if ( !find_video_track() ) {
    iop->error("No video found");
    return;
  }

  /*
     Height and Width
   */
  get_info();
}

movReader::~movReader()
{
#ifdef __linux__
  if ( imageBuffer_ )
    delete [] imageBuffer_;
#endif

#if QUICKTIME_API
  if ( pixelBuffer_ ) {
    CVPixelBufferUnlockBaseAddress( pixelBuffer_, 0 );
    CVPixelBufferRelease( pixelBuffer_ );
  }
  if ( session_ )
    ICMDecompressionSessionRelease(session_);
  ExitMovies();
  // Don't call TerminateQTML here on Windows.  It seems to cause a crash on subsequent calls to InitializeQTML.
  // In the absence of a static class destructor, we could either call it on static deinit (which would probably
  // fail, since since InitializeQTML fails on static init), or just leave it to be cleaned up when the process
  // exits, so we'll do that.
#else
  if ( converter_ )
    gavl_video_converter_destroy(converter_);
  if ( movie_ )
    bgav_close(movie_);
#endif

}

/*
   Open QuickTime file for reading.

   N.B. On the Windows/Apple side, we should have the new movie
   read into a pixel buffer visual context if Apple ever that option
   to the Windows API.
 */
#if QUICKTIME_API
bool movReader::open_quicktime()
{
  CFStringRef file;
  Boolean active = true;

  QTNewMoviePropertyElement inputProperties[2] = {
    0
  };

  //Location
  file =  CFStringCreateWithBytes(NULL,
                                  (const UInt8*)nativeFilename().c_str(),
                                  strlen(nativeFilename().c_str()),
                                  kCFStringEncodingASCII,
                                  false);

  inputProperties[0].propClass = kQTPropertyClass_DataLocation;
  inputProperties[0].propID = kQTDataLocationPropertyID_CFStringPosixPath;
  inputProperties[0].propValueSize = sizeof(CFStringRef);
  inputProperties[0].propValueAddress = &file;

  inputProperties[1].propClass = kQTPropertyClass_NewMovieProperty;
  inputProperties[1].propID = kQTNewMoviePropertyID_Active;
  inputProperties[1].propValueSize = sizeof(active);
  inputProperties[1].propValueAddress = &active;

  err_ = NewMovieFromProperties(2,
                                inputProperties,
                                0,
                                NULL,
                                &movie_);

  CFRelease(file);

  return err_ == noErr;
}
#else //Linux
void null_cb (void* data, bgav_log_level_t level,
              const char* domain, const char* msg)
{
  //Do nothing.
}

bool movReader::open_quicktime()
{
  movie_ = bgav_create();

  bgav_options_set_log_callback(bgav_get_options(movie_), null_cb, NULL);

  if ( !bgav_open(movie_, filename()) )
    return false;

  return true;
}
#endif

/*
   Find Video Track
 */
#if QUICKTIME_API
bool movReader::find_video_track()
{
  long trackcount = GetMovieTrackCount(movie_);

  for (long i = 1; i <= trackcount; i++) {
    Track track = GetMovieIndTrack(movie_, i);

    if ( track != NULL ) {
      OSType type;

      GetMediaHandlerDescription(GetTrackMedia(track), &type, NULL, NULL);

      if ( type == VideoMediaType || type == MPEGMediaType ||
           type == MovieMediaType || type == URLDataHandlerSubType ) {
        track_ = track;
        i = trackcount + 1; //we only need one
      }
    }
  }

  return track_ != NULL;
}
#else //Linux
bool movReader::find_video_track()
{
  //bgav_codecs_dump();
  //bgav_inputs_dump();

  int tracks = bgav_num_tracks(movie_);

  if ( tracks == 0 )
    return false;

  for (int i = 0; i < tracks; i++) {
    int streams = bgav_num_video_streams(movie_, i);

    if ( streams > 0 ) {
      track_ = i;
      bgav_select_track(movie_, i);
    }

    for (int s = 0; s < streams; s++) {
      stream_ = s;
      bgav_set_video_stream(movie_, s, BGAV_STREAM_DECODE);
    }

    if ( streams > 0 ) {
      bgav_start(movie_);
      return true;
    }
  }

  return false;
}
#endif

/*
   get_info:

   Handles height, width, aspect ratio and movie length
 */
#if QUICKTIME_API
static long getFrameCount( Track track )
{
  long count = -1;
  short flags;
  TimeValue time = 0;
  OSErr error = noErr;

  error = GetMoviesStickyError();

  flags = nextTimeMediaSample + nextTimeEdgeOK;
  while ( time >= 0 ) {
    count++;
    GetTrackNextInterestingTime( track, flags, time, fixed1, &time, NULL);
    flags = nextTimeStep;
  }
  if ( error == noErr )
    ClearMoviesStickyError();

  return count;
}

void movReader::get_info()
{
  Fixed height, width;
  GetTrackDimensions(track_, &width, &height);

  //The track width is really the "display" width. We need
  //to divide by the aspect ratio to get the actual width
  //of the data in the frame.
  aspect_ = pixelAspect( GetTrackMedia(track_) );

  set_info( FixedToInt(width), FixedToInt(height), depth_, aspect_);

  info_.first_frame(1);
  info_.last_frame( getFrameCount( track_ ) );
}
#else //Linux
void movReader::get_info()
{
  int height, width;

  format_ = bgav_get_video_format(movie_, stream_);

  height = format_->image_height;
  width = format_->image_width;

  set_info(width, height, 3, aspect_);

  info_.first_frame(1);

  /*
     The format definition includes a value for the frame duration
     (in timescale units) and a value for the timescal (in number
     of timescale units per second).

     bgav_get_duration returns the duration of the track in
     different timescale units.  So, to get the number of
     frames in the track, we have to convert both values
     to seconds and divide.

     I believe this will only work correctly for constant
     framerates, so more work will probably need to be done on
     this in the future.
   */
  gavl_time_t duration = bgav_get_duration(movie_, track_);

  frame_duration_sec_ =
    double(format_->frame_duration) / double(format_->timescale);

  double track_duration = duration / double(GAVL_TIME_SCALE);

  info_.last_frame( int(track_duration / frame_duration_sec_) );
  //info_.last_frame( duration/format_->timescale);
  outformat_ = *format_;
}
#endif

#if QUICKTIME_API
float movReader::pixelAspect(Media media)
{
  PixelAspectRatioImageDescriptionExtension aspect;
  float val = 0.0f;
  TimeValue64 t;
  ImageDescriptionHandle imageDesc = (ImageDescriptionHandle)NewHandle(0);

  SampleNumToMediaDecodeTime(media, 1, &t, NULL);

  if ( GetMoviesError() != noErr ) {
    DisposeHandle( (Handle)imageDesc );
    return 0.0f;
  }

  err_ = GetMediaSample2(media, NULL, 0, NULL, t, NULL, NULL, NULL,
                         (SampleDescriptionHandle) imageDesc,
                         NULL, 1, NULL, NULL);

  if ( err_ != noErr ) {
    DisposeHandle( (Handle)imageDesc );
    return 0.0f;
  }

  err_ = ICMImageDescriptionGetProperty( imageDesc,
                                         kQTPropertyClass_ImageDescription,
                                         kICMImageDescriptionPropertyID_PixelAspectRatio,
                                         sizeof(aspect),
                                         &aspect,
                                         NULL);

  if ( err_ == noErr )
    val = float(aspect.hSpacing) / float(aspect.vSpacing);

  #if 0
  NCLCColorInfoImageDescriptionExtension nclc;

  err_ = ICMImageDescriptionGetProperty( imageDesc,
                                         kQTPropertyClass_ImageDescription,
                                         kICMImageDescriptionPropertyID_NCLCColorInfo,
                                         sizeof(nclc),
                                         &nclc,
                                         NULL);

  if ( err_ == noErr ) {
    cout << "NCLC!\n";
    cout << "Primaries: " << nclc.primaries << endl;
    cout << "Transfer: " << nclc.transferFunction << endl;
    cout << "Matrix: " << nclc.matrix << endl;
  }
  #endif

  DisposeHandle( (Handle)imageDesc );
  return val;
}
#endif

#if QUICKTIME_API
void movReader::createDecompressionSession(Media media)
{
  CFMutableDictionaryRef pixelAttributes = NULL;
  MediaSampleFlags sampleFlags = 0;
  TimeValue64 decodeTime;

  //We use the first sample for the session regardless of the
  //current frame
  SampleNumToMediaDecodeTime(media, 1, &decodeTime, NULL);

  if ( GetMoviesError() != noErr )
    return iop->error("Error finding decode time for start of movie");

  ImageDescriptionHandle imageDesc = (ImageDescriptionHandle)NewHandle(0);

  /* Create Decompression Session */
  //We have to run this first to get the sample description
  err_ = GetMediaSample2(media,
                         NULL, //data out
                         0,   //max data size
                         NULL, //bytes
                         decodeTime, //decode time
                         NULL, //returned decode time
                         NULL, //duration per sample
                         NULL, //offset
                         (SampleDescriptionHandle) imageDesc,
                         NULL, //sample description index
                         1, //max number of samples
                         NULL, //number of samples
                         &sampleFlags //flags
                         );

  if ( err_ != noErr ) {
    DisposeHandle( (Handle)imageDesc );
    return iop->error("Failed to size up media sample");
  }

  codec_ = (*imageDesc)->cType;

  ComponentDescription cd = {
    decompressorComponentType, 0, 0, 0, cmpIsMissing
  };
  Component decompressor = 0;
  cd.componentSubType = codec_;
  decompressor = FindNextComponent( 0, &cd );

  manufacturer_ = 0;
  if ( GetComponentInfo( decompressor, &cd, NULL, NULL, NULL ) == noErr )
    manufacturer_ = cd.componentManufacturer;

  //Setup output pixel format
  pixelAttributes = CFDictionaryCreateMutable( kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks );

  if ( !pixelAttributes ) {
    DisposeHandle( (Handle)imageDesc );
    return iop->error("Couldn't create dictionary");
  }

  addIntToDictionary( pixelAttributes, kCVPixelBufferWidthKey, width() );
  addIntToDictionary( pixelAttributes, kCVPixelBufferHeightKey, height() );

  OSType pixelFormat;
  getCodecInfo( decompressor, &pixelFormat, &codecFlags_ );

  // The BlackMagic r210 decoder ciaims to support k64ARGBPixelFormat, but actually returns 8-bit data
  addIntToDictionary( pixelAttributes, kCVPixelBufferPixelFormatTypeKey, pixelFormat );

  // Create Decompression Session
  ICMDecompressionTrackingCallbackRecord callbackRecord;
  callbackRecord.decompressionTrackingCallback = movReader::emitFrame;
  callbackRecord.decompressionTrackingRefCon = this;

  // Turn on high quality for decompression
  ICMDecompressionSessionOptionsRef sessionOptions = NULL;
  err_ = ICMDecompressionSessionOptionsCreate( kCFAllocatorDefault, &sessionOptions );
  if ( err_ == noErr ) {
    CodecQ codecAccuracy = codecHighQuality;
    ICMDecompressionSessionOptionsSetProperty( sessionOptions, kQTPropertyClass_ICMDecompressionSessionOptions, kICMDecompressionSessionOptionsPropertyID_Accuracy, sizeof(CodecQ), &codecAccuracy );
  }

  err_ = ICMDecompressionSessionCreate(kCFAllocatorDefault, //allocator
                                       imageDesc, //image description
                                       sessionOptions, //decompression options
                                       pixelAttributes, //buffer attributes
                                       &callbackRecord,
                                       &session_
                                       );

  if ( sessionOptions )
    ICMDecompressionSessionOptionsRelease( sessionOptions );

  if (err_ == noCodecErr)
    iop->error("No suitable codec found for this movie");
  else if (err_ != noErr)
    iop->error("Failed to create decompression session");

  CFRelease(pixelAttributes);
  DisposeHandle( (Handle)imageDesc );
}

std::string movReader::nativeFilename() const
{
  std::string fn( filename() );
  #ifdef FN_OS_WINDOWS
  std::replace( fn.begin(), fn.end(), '/', '\\' );
  #endif
  return fn;
}

#endif

/*
   The bulk of the work occurs here. We can't really use Quicktime in the
   scanline-threaded portion, so we have to decode the data here, buffer
   it, then provide it to nuke in 'execute'.
 */
#if QUICKTIME_API
void movReader::open()
{
  //Each frame should only be decoded once.
  if ( framenum_ == frame() && pixelBuffer_ )
    return;

  // As there's no close() call, we release the previous pixel buffer here.
  if ( pixelBuffer_ ) {
    CVPixelBufferUnlockBaseAddress( pixelBuffer_, 0 );
    CVPixelBufferRelease( pixelBuffer_ );
  }
  pixelBuffer_ = NULL;

  framenum_ = frame();

  Media media = GetTrackMedia(track_);

  TimeValue64 decodeTime, displayTime, syncTime;
  TimeValue64 duration;
  SInt64 sampleNum, syncSample;
  ByteCount dataSize = 0;
  MediaSampleFlags sampleFlags = 0;
  UInt8* data = NULL;
  TimeValue64 startTime, endTime, displayDuration;

  startTime = GetMediaDisplayStartTime(media);
  endTime = GetMediaDisplayEndTime(media);
  displayDuration = GetTrackDuration( track_ );
  displayTime = ( GetTrackDuration( track_ ) / info_.last_frame() * (frame() - 1) );
  displayTime = TrackTimeToMediaTime( displayTime, track_ );

  // avoid to read out side the range time definition
  if ( displayTime < startTime ) {
    displayTime = startTime;
  }

  if ( displayTime > endTime - 1 ) {
    displayTime = endTime - 1;
  }

  MediaDisplayTimeToSampleNum(media,
                              displayTime,
                              &sampleNum,
                              NULL,
                              NULL);

  //Find the time value that corresponds to our current frame
  SampleNumToMediaDecodeTime(media, sampleNum, &decodeTime, NULL);

  if ( GetMoviesError() != noErr )
    return iop->error("Error finding decode time for frame %d", frame());

  if ( !session_ )
    createDecompressionSession(media);
  if ( !session_ )
    return;

  //Get the closest previous key frame
  GetMediaNextInterestingDecodeTime(media,
                                    nextTimeSyncSample | nextTimeEdgeOK,
                                    decodeTime,
                                    -fixed1,
                                    &syncTime,
                                    NULL);

  if ( GetMoviesError() != noErr )
    return iop->error("Error finding sync time for frame %d", frame());

  MediaDecodeTimeToSampleNum(media, syncTime, &syncSample, NULL, NULL);

  if ( GetMoviesError() != noErr )
    return iop->error("Error finding sync sample for frame %d", frame());

  for (; syncSample <= sampleNum; syncSample++) {
    SampleNumToMediaDecodeTime(media, syncSample, &decodeTime, NULL);

    //Get the frame's data size and flags
    err_ = GetMediaSample2(media, NULL, 0, &dataSize, decodeTime,
                           NULL, NULL, NULL, NULL, NULL, 1, NULL,
                           &sampleFlags);

    if ( err_ != noErr )
      return iop->error("Couldn't find frame %d in movie", frame());

    data =  (UInt8*)malloc( dataSize );

    err_ = GetMediaSample2( media,
                            data, //data out
                            dataSize, //max data size
                            NULL, //bytes
                            decodeTime, //decodeTime
                            NULL, //sampledecodetime
                            &duration, //sample duration
                            NULL,
                            NULL,
                            NULL, //sampledescription index
                            1, //max number of samples
                            NULL, //number of samples
                            &sampleFlags //flags
                            );

    if ( err_ != noErr ) {
      delete data;
      return iop->error("Couldn't get media for sample %d", syncSample);
    }

    //Decode desired frame
    ICMFrameTimeRecord frameTime = {
      0
    };
    frameTime.recordSize = sizeof(ICMFrameTimeRecord);
    *(TimeValue64*)& frameTime.value = decodeTime;
    frameTime.decodeTime = decodeTime;
    frameTime.scale = GetMediaTimeScale(media);
    frameTime.rate = fixed1;
    frameTime.frameNumber = syncSample;
    frameTime.duration = duration;

    if ( syncSample != sampleNum )
      frameTime.flags = icmFrameTimeDoNotDisplay;
    else
      frameTime.flags = icmFrameTimeDecodeImmediately;

    err_ = ICMDecompressionSessionDecodeFrame(session_,
                                              data,
                                              dataSize,
                                              NULL, //frame options
                                              &frameTime,
                                              data //reference value
                                              );

    if ( err_ != noErr ) {
      delete data;
      return iop->error("Failed to decode sample %d", syncSample);
    }
  }

  //Flush
  ICMDecompressionSessionFlush(session_);
}
#else //Linux
void movReader::open()
{
  if ( framenum_ == frame() && imageBuffer_)
    return;

  if ( !imageBuffer_ )
    imageBuffer_ = new unsigned short[height() * width() * 3];

  if ( !converter_ ) {
    converter_ = gavl_video_converter_create();

    outformat_.pixelformat = GAVL_RGB_48;

    gavl_video_converter_init(converter_, format_, &outformat_);
  }

  gavl_video_frame_t* vidframe = gavl_video_frame_create_nopad(format_);
  gavl_video_frame_t* outframe = gavl_video_frame_create_nopad(&outformat_);

  /*
     If we're decoding the next frame in the sequence, we can simply call
     'read_video'. But, if not, we need to find the closes previous
     keyframe and decode all of the frames from there to here.
   */
  if ( frame() != framenum_ + 1 ) {
    int decodeframes;

    if ( !bgav_can_seek(movie_) ) {
      /*
         Hopefully we don't run in this, but if we can't seek in the
         movie, we're going to have to close, re-open it, and decode
         every frame up to the one we're searching for.
       */
      open_quicktime();
      find_video_track();
      decodeframes = frame() - 1;
    }
    else {
      gavl_time_t frame_duration =
        U64( (frame_duration_sec_ * GAVL_TIME_SCALE) + 0.5 );
      gavl_time_t current_frametime = frame_duration * (frame() - 1);
      gavl_time_t seektime = current_frametime + 1;

      for (gavl_time_t i = current_frametime;
           i >= 0 && seektime > current_frametime;
           i -= U64( frame_duration )) {
        seektime = i;
        bgav_seek(movie_, &seektime);
      }

      decodeframes = (current_frametime - seektime) / frame_duration;
    }

    for (int i = 0; i < decodeframes; i++ ) {
      if ( !bgav_read_video(movie_, vidframe, stream_) )
        return iop->error("Couldn't decode video");
    }
  }

  framenum_ = frame();

  if ( !bgav_read_video(movie_, vidframe, stream_) )
    return iop->error("Couldn't decode video");

  gavl_video_convert(converter_, vidframe, outframe);

  if ( !imageBuffer_ )
    return iop->error("image buffer missing");

  if ( !outframe->planes[0] )
    return iop->error("Can't find plane 0");

  memcpy(imageBuffer_, outframe->planes[0], width() * height() * 6);

  gavl_video_frame_destroy(outframe);
  gavl_video_frame_destroy(vidframe);
}
#endif

#if QUICKTIME_API
void movReader::addIntToDictionary(CFMutableDictionaryRef dictionary,
                                   CFStringRef key, SInt32 number)
{
  CFNumberRef cfnumber = CFNumberCreate(NULL, kCFNumberSInt32Type, &number);
  if ( !number )
    return;
  CFDictionaryAddValue(dictionary, key, cfnumber);
  CFRelease(cfnumber);
}
#endif

/*
   Converts 4:2:2 YCbCr data to RGB.

   4:2:2 data is arranged in 8-bit chunks in the form Cb, Y, Cr, Y such
   that Cb and Cr are shared by the two y values. Apple also appears to
   do something slightly odd with the colors in that it bakes in a small
   gamma curve in addition to the 16-235 processing that normally occurs.
 */
void movReader::ycbcrconvert(unsigned char* src, float* r, float* g, float* b, int bytes)
{
  static float mult = 1.0f / 219.0f;

  static float A = 1.5966f;

  static float B = -0.813202f;
  static float C = -0.391998f;

  static float D = 2.0184f;

  float cb, y, cr, val;
  unsigned char* end = src + bytes;

  while ( src < end ) {
    cb = ( float(*src++) - 128.0f ) / 255.0f;
    y = max(0.0f, float(*src++) - 16.0f);
    cr = ( float(*src++) - 128.0f ) / 255.0f;

    //Correct for the 16-235 compression as well as the
    //baked-in gamma.
    y = mult * y;

    val = y + A * cr;
    *r++ = min(max(0.0f, val), 1.0f);

    val = y + B * cr + C * cb;
    *g++ = min(max(0.0f, val), 1.0f);

    val = y + D * cb;
    *b++ = min(max(0.0f, val), 1.0f);

    if ( src < end ) {
      y = max(0.0f, float(*src++) - 16.0f);
      y = mult * y;

      val = y + A * cr;
      *r++ = min(max(0.0f, val), 1.0f);

      val = y + B * cr + C * cb;
      *g++ = min(max(0.0f, val), 1.0f);

      val = y + D * cb;
      *b++ = min(max(0.0f, val), 1.0f);
    }
  }
}

//MPEG and H.264 use slightly different numbers for
//the individual colors.
void movReader::ycbcrconvert_mpeg(unsigned char* src, float* r, float* g, float* b, int bytes)
{
  static float mult = 1.0f / 218.0f;

  static float A = 1.5883f;

  static float B = -0.813202f;
  static float C = -0.391998f;

  static float D = 2.0457f;

  float cb, y, cr, val;
  unsigned char* end = src + bytes;

  while ( src < end ) {
    cb = ( float(*src++) - 128.0f ) / 255.0f;
    y = max(0.0f, float(*src++) - 16.0f);
    cr = ( float(*src++) - 128.0f ) / 255.0f;

    // Correct for the 16-235 compression
    y = mult * y;

    val = y + A * cr;
    *r++ = min(max(0.0f, val), 1.0f);

    val = y + B * cr + C * cb;
    *g++ = min(max(0.0f, val), 1.0f);

    val = y + D * cb;
    *b++ = min(max(0.0f, val), 1.0f);

    if ( src < end ) {
      y = max(0.0f, float(*src++) - 16.0f);
      y = mult * y;

      val = y + A * cr;
      *r++ = min(max(0.0f, val), 1.0f);

      val = y + B * cr + C * cb;
      *g++ = min(max(0.0f, val), 1.0f);

      val = y + D * cb;
      *b++ = min(max(0.0f, val), 1.0f);
    }
  }
}

/*
   A utility function for 4:2:2 YCbCr images. This simply copies over
   the Y values into the green channel and Cb and Cr into blue and
   red, respectively.
 */
void movReader::ycbcrconvert_raw(unsigned char* src, float* r, float* g, float* b, int bytes)
{
  unsigned char* end = src + bytes;

  unsigned char cb, y, cr;

  while ( src < end ) {
    cb = *src++;
    y = *src++;
    cr = *src++;

    *r++ = cr / 255.0f;
    *g++ = y / 255.0f;
    *b++ = cb / 255.0f;

    y = *src++;
    *r++ = cr / 255.0f;
    *g++ = y / 255.0f;
    *b++ = cb / 255.0f;
  }
}

void movReader::convert4444YpCbCrA8RPixelFormatToARGB( const unsigned char* src, float* a, float* r, float* g, float* b, int length )
{
  for ( int i = 0; i < length; i++ ) {
    if ( a )
      *a++ = *src / 255.0f;
    src++;
    float y = float(*src++);
    float cb = float(*src++) - 128.0f;
    float cr = float(*src++) - 128.0f;

    // Rec 601
    y *= 0.00456621f;
    y = max( 0.0f, y );

    *r++ = min(max(0.0f, y + 0.00625893f * cr), 1.0f);
    *g++ = min(max(0.0f, y - 0.00153632f * cb - 0.00318811f * cr), 1.0f);
    *b++ = min(max(0.0f, y + 0.00791071f * cb), 1.0f);
  }
}

void movReader::convert4444YpCbCrAFPixelFormatToARGB( const float* src, float* a, float* r, float* g, float* b, int length )
{
  for ( int i = 0; i < length; i++ ) {
    if ( a )
      *a++ = *src;
    src++;
    float y = float(255 * *src++);
    float cb = float(255 * *src++) - 128.0f;
    float cr = float(255 * *src++) - 128.0f;

    // Rec 601
    y *= 0.00456621f;

    *r++ = y + 0.00625893f * cr;
    *g++ = y - 0.00153632f * cb - 0.00318811f * cr;
    *b++ = y + 0.00791071f * cb;
  }
}

void movReader::yuvsconvert( unsigned char* src, float* r, float* g, float* b, int bytes )
{
  static float mult = 1.0f / 218.0f;

  static float A = 1.596f;

  static float B = -0.81511f;
  static float C = -0.37294f;

  static float D = 2.0472f;

  float cb, y1, cr, y2, val;
  unsigned char* end = src + bytes;

  while ( src < end ) {
    y1 = max(0.0f, float(*src++) - 16.0f);
    cb = ( float(*src++) - 127.0f ) / 255.0f;
    y2 = max(0.0f, float(*src++) - 16.0f);
    cr = ( float(*src++) - 128.0f ) / 255.0f;

    y1 = mult * y1;
    y2 = mult * y2;

    /* Red 1 */
    val = y1 + A * cr;
    *r++ = min(max(0.0f, val), 1.0f);

    /* Green 1 */
    val = y1 + B * cr + C * cb;
    *g++ = min(max(0.0f, val), 1.0f);

    /* Blue 1 */
    val = y1 + D * cb;
    *b++ = min(max(0.0f, val), 1.0f);

    /* Red 2 */
    val = y2 + A * cr;
    *r++ = min(max(0.0f, val), 1.0f);

    /* Green 2 */
    val = y2 + B * cr + C * cb;
    *g++ = min(max(0.0f, val), 1.0f);

    /* Blue 2 */
    val = y2 + D * cb;
    *b++ = min(max(0.0f, val), 1.0f);
  }
}

void movReader::yuvsconvert_raw(unsigned char* src, float* r, float* g, float* b,
                                int bytes)
{
  unsigned char* end = src + bytes;
  int cb, y1, cr, y2;

  while ( src < end ) {
    y1 = *src++;
    cb = *src++;
    y2 = *src++;
    cr = *src++;

    *r++ = cr / 255.0f;
    *g++ = y1 / 255.0f;
    *b++ = cb / 255.0f;

    *r++ = cr / 255.0f;
    *g++ = y2 / 255.0f;
    *b++ = cb / 255.0f;
  }
}

#if QUICKTIME_API
void movReader::emitFrame( void* reader, OSStatus result,
                           ICMDecompressionTrackingFlags flags,
                           CVPixelBufferRef pixelBuffer,
                           TimeValue64 displayTime,
                           TimeValue64 displayDuration,
                           ICMValidTimeFlags validFlags,
                           void* reserved, void* frameData)
{
  movReader* self = (movReader*)reader;

  if ( result != noErr ) {
    cerr << "Decode Error: " << result << endl;
    return;
  }

  if ( kICMDecompressionTracking_ReleaseSourceData & flags )
    free(frameData);

  self->pixelBuffer_ = NULL;
  if ( (kICMDecompressionTracking_EmittingFrame & flags) && pixelBuffer ) {
    // Retain the pixel buffer for decoding in engine(). ICM guarantees not to reuse the buffer until we release it.
    // We also lock the base address. The documentation doesn't say whether locks are recursive or threadsafe, so it's best to do it here rather than in engine().
    CVPixelBufferRetain( pixelBuffer );
    CVPixelBufferLockBaseAddress( pixelBuffer, 0) ;
    self->pixelBuffer_ = pixelBuffer;

    OSType format = CVPixelBufferGetPixelFormatType( pixelBuffer );
    if ( format == k64ARGBPixelFormat || format == k48RGBPixelFormat ) {
      unsigned short* base = (unsigned short*)CVPixelBufferGetBaseAddress( pixelBuffer );

      // Byte swap 64-bit pixels if necessary
      // k64ARGBPixelFormat and k48RGBPixelFormat are defined to be big-endian, so we'd expect to have to swap it on little-endian machines.
      // However, many codec authors, Apple included, seem to have missed that vital part of the definition meaning that we don't have a clue
      // what byte order we're actually receiving. What we do is The Right Thing, but will fail with many codecs.
      // Check for codecs which get the byte order wrong for 16-bits on Intel Mac
      if ( !(self->codecFlags_ & k64ARGBPixelFormatNativeByteOrder) )
        frommsb( base, CVPixelBufferGetBytesPerRow( pixelBuffer ) * CVPixelBufferGetHeight( pixelBuffer ) / sizeof(short) );
    }

    // Get the pixel buffer gamma and save it
    float gamma = 1.0f;
    CFNumberRef gammaRef = (CFNumberRef)CVBufferGetAttachment( pixelBuffer, kCVImageBufferGammaLevelKey, NULL );
    if ( gammaRef )
      CFNumberGetValue(gammaRef, kCFNumberFloat32Type, &gamma);
    self->gamma_ = gamma;

  #if 0
    cout << "gamma: " << gamma << endl;
    char* pz = (char*)&format;
    cout << "format: ";
    if ( (format & 0xff000000) == 0 )
      cout << format << endl;
    else
      cout << pz[3] << pz[2] << pz[1] << pz[0] << endl;
    cout << endl;
  #endif

  }
}
#endif

void movReader::engine(int y, int x, int rx, ChannelMask channels, Row& row)
{
#ifdef __linux__
  foreach ( z, channels ) {
    float* TO = row.writable(z) + x;
    unsigned short* FROM = imageBuffer_;
    FROM += (height() - y - 1) * width() * 3;
    FROM += x * 3;

    from_short(z, TO, FROM + z - 1, NULL, rx - x, 16, 3);
  }
#else
  void* base = CVPixelBufferGetBaseAddress( pixelBuffer_ );
  unsigned int height = CVPixelBufferGetHeight( pixelBuffer_ );
  int rowbytes = CVPixelBufferGetBytesPerRow( pixelBuffer_ );
  OSType format = CVPixelBufferGetPixelFormatType( pixelBuffer_ );

  // For non-RGB formats, decode the whole row so we don't have to worry about trying to decode from the middle of a 4:2:2 pixel block.
  if ( format != k32ARGBPixelFormat && format != k64ARGBPixelFormat ) {
    x = max( 0, row.getLeft() );
    rx = min( (int)CVPixelBufferGetWidth( pixelBuffer_ ), row.getRight() );
  }

  base = (unsigned char*)base + (height - y - 1) * rowbytes;
  float* r = row.writable( Chan_Red ) + x;
  float* g = row.writable( Chan_Green ) + x;
  float* b = row.writable( Chan_Blue ) + x;
  float* a = depth_ == 4 ? row.writable( Chan_Alpha ) + x : NULL;
  float* rRow = r, * gRow = g, * bRow = b, * aRow = a;
  int j;

  switch ( format ) {
    case k32ARGBPixelFormat:
    {
      unsigned char* s = (unsigned char*)base + x * 4;
      float denom = 1.0f / 255.0f;

      for ( j = x; j < rx; j++ ) {
        if ( a )
          *a++ = *s++ *denom;
        else
          s++;
        *r++ = *s++ *denom;
        *g++ = *s++ *denom;
        *b++ = *s++ *denom;
      }
    }
    break;
    case k64ARGBPixelFormat:
    {
      /*
         64-bit RGB values. These are still in the 16-234 space (except
         the 16-bit equivalents).
       */
      unsigned short* s = (unsigned short*)base + x * 4;
      static float denom = 1.0f / 65536.0f;

      for ( j = x; j < rx; j++ ) {
        if ( a )
          *a++ = *s++ *denom;
        else
          s++;
        *r++ = *s++ *denom;
        *g++ = *s++ *denom;
        *b++ = *s++ *denom;
      }
    }
    break;
    case k48RGBPixelFormat:
    {
      /*
         48-bit RGB values.
       */
      unsigned short* s = (unsigned short*)base + x * 3;
      static float denom = 1.0f / 65536.0f;

      for ( j = x; j < rx; j++ ) {
        if ( a )
          *a++ = 1.0f;
        else
          s++;
        *r++ = *s++ *denom;
        *g++ = *s++ *denom;
        *b++ = *s++ *denom;
      }
    }
    break;
    case k422YpCbCr8PixelFormat:
    {
      unsigned char* src = (unsigned char*)base + x * 2;

      if ( raw() )
        ycbcrconvert_raw(src, r, g, b, (rx - x) * 2);
      else if ( codec_ == 'avc1' || codec_ == 'mp4v' )
        ycbcrconvert_mpeg(src, r, g, b, (rx - x) * 2);
      else
        ycbcrconvert(src, r, g, b, (rx - x) * 2);
      if ( a )
        for ( j = x; j < rx; j++ )
          *a++ = 1.0f;
    }
    break;
    case kYUVSPixelFormat:
    {
      unsigned char* src = (unsigned char*)base + x * 2;

      if ( raw() )
        yuvsconvert_raw(src, r, g, b, (rx - x) * 2);
      else
        yuvsconvert(src, r, g, b, (rx - x) * 2);
      if ( a )
        for ( j = x; j < rx; j++ )
          *a++ = 1.0f;
    }
    break;
    case k4444YpCbCrA8RPixelFormat:
    {
      unsigned char* src = (unsigned char*)base + x * 4;

      if ( raw() )
        yuvsconvert_raw(src, r, g, b, (rx - x) * 2);
      else
        convert4444YpCbCrA8RPixelFormatToARGB( src, a, r, g, b, rx - x );
    }
    break;
    case k4444YpCbCrAFPixelFormat:
    {
      float* s = (float*)base + x * 4;

      if ( raw() ) {
        for ( j = x; j < rx; j++ ) {
          if ( a )
            *a++ = *s;
          s++;
          *r++ = *s++;
          *g++ = *s++;
          *b++ = *s++;
        }
      }
      else
        convert4444YpCbCrAFPixelFormatToARGB( s, a, r, g, b, rx - x );
    }
    break;
    default:
      OSType format = CVPixelBufferGetPixelFormatType( pixelBuffer_ );
      formaterror("Unhandled pixel format", (char*)&format);
  }

  if ( !raw() ) {
    from_float( Chan_Red, rRow, rRow, aRow, rx - x, 1 );
    from_float( Chan_Green, gRow, gRow, aRow, rx - x, 1 );
    from_float( Chan_Blue, bRow, bRow, aRow, rx - x, 1 );
  }
#endif
}

#ifndef NO_META_DATA
  #if QUICKTIME_API
void movReader::prefetchMetaData()
{
  if ( track_ ) {
    Media media = GetTrackMedia( track_ );
    if ( media ) {
      TimeScale mediaTimeScale = GetMediaTimeScale( media );
      TimeScale mediaDuration = GetMediaDuration( media );
      long mediaSampleCount = GetMediaSampleCount( media );
      double duration = (double)mediaDuration / (double)mediaTimeScale;
      double averageSampleRate = (double)mediaSampleCount / duration;

      _meta.setData(MetaData::FRAME_RATE, averageSampleRate );

      if ( codec_ ) {
        ComponentDescription cd = {
          decompressorComponentType, 0, 0, 0, cmpIsMissing
        };
        Component decompressor = 0;
        cd.componentSubType = codec_;
        decompressor = FindNextComponent( 0, &cd );

        Handle componentName = NewHandle( 0 );
        if ( componentName ) {
          Handle componentInfo = NewHandle( 0 );
          if ( componentInfo ) {
            if ( GetComponentInfo( decompressor, &cd, componentName, componentInfo, NULL ) == noErr ) {
              std::string name( (char*)*componentName + 1, (*(UInt8**)componentName)[0] );
              std::string info( (char*)*componentInfo + 1, (*(UInt8**)componentInfo)[0] );
              _meta.setData(MetaData::QuickTime::CODEC_NAME, name );
              _meta.setData(MetaData::QuickTime::CODEC_INFO, info );
            }
            DisposeHandle( componentInfo );
          }
          DisposeHandle( componentName );
        }
      }

      _meta.setData(MetaData::PIXEL_ASPECT, aspect_ );
    }
  }
}
  #endif
#endif
