// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/DDWindows.h"
#include "DDImage/Writer.h"
#include "DDImage/Row.h"
#include "DDImage/Knob.h"
#include "DDImage/Memory.h"
#include "DDImage/DDString.h"
#undef newstring
#include <algorithm>


#ifdef _WIN32
  #include <Files.h>
  #include <Movies.h>
  #include <QTML.h>
  #include <QuickTimeComponents.h>
  #include <Script.h>
  #include <GXMath.h>
  #include <Endian.h>
  #define QUICKTIME_API 1
#elif defined(__APPLE__)
  #include <QuickTime/Movies.h>
  #include <QuickTime/QuickTimeComponents.h>
  #include <QuickTime/ImageCodec.h>
  #define QUICKTIME_API 1
#else
  #define QUICKTIME_API 0
extern "C" {
  // Building on Linux requires libquicktime
  #include <lqt/quicktime.h>
  #include <lqt/lqt.h>
  #include <lqt/colormodels.h>
}
#endif

#if QUICKTIME_API
  #include "movCommon.h"
#endif

using namespace DD::Image;
using namespace std;

#if QUICKTIME_API
static const CodecQ table[6] = {
  codecMinQuality,
  codecLowQuality,
  codecNormalQuality,
  codecHighQuality,
  codecMaxQuality,
  codecLosslessQuality
};

static int indexForQuality( CodecQ quality )
{
  int count = sizeof(table) / sizeof(CodecQ);
  for ( int i = 0; i < count; i++)
    if ( quality <= table[i] )
      return i;
  return count - 1;
}
#endif

class movWriter : public Writer
{
#if QUICKTIME_API
  std::string tempFilePath_;

  //QuickTime data types
  Movie movie_;
  Handle dataref_;
  DataHandler moviehandler_;
  Track track_;
  Media media_;
  ICMCompressionSessionRef session_;
  ICMCompressionPassModeFlags passflags_;
  OSType pixelFormat_;
  int codecFlags_;

  float gamma_;
  const char* settings_;
  bool swapBytes_;
  bool tagGamma_;

  static int specsort(const void*, const void*);

  void createMovie();
  void addAudio();
  void flattenMovie();
  static OSStatus addFrame(void* refcon, ICMCompressionSessionRef session,
                           OSStatus err, ICMEncodedFrameRef encodedFrame,
                           void* reserved);
  static void bufferRelease(void* releaseref, const void* baseaddr);
  QTAtomContainer decodeSettings();
  void encodeSettings( QTAtomContainer container );
#else
  quicktime_t* movie_;
  int track_;

  void createMovie();
#endif
  bool force_aspect_;
  bool _valid;
  
  static const char* const* codecList();
  static int default_codec();

  int frame() const { return int(iop->outputContext().frame()); }

#if QUICKTIME_API
  #ifdef _WIN32
  const static QTPathStyle kQTPlatformPathStyle = kQTWindowsPathStyle;
  #else
  const static QTPathStyle kQTPlatformPathStyle = kQTPOSIXPathStyle;
  #endif
  CFStringRef CFStringCreateFromPath( const char* path ) const;
#endif

public:
  int codec; // enumeration value;
  const char* audiofile;
  float audio_offset;
  int offset_unit;
  float fps;
  int quality;
  int keyframerate;
  bool flatten;
  int _timescale;
  int _frameDuration;

  movWriter(Write* iop);
  ~movWriter();
  void execute();
  void finish();
  void knobs(Knob_Callback f);
  int knob_changed(DD::Image::Knob* knob);
  bool movie() const { return true; }
  static const Writer::Description d;

  //These exist primarily to allow the static function 'addFrame' to
  //access the media and aspect ratio.
#if QUICKTIME_API
  Media* media() { return &media_; }
  LUT* defaultLUT() const { return LUT::getLut(LUT::GAMMA1_8); }
#endif
  float aspect() { return (float)info().format().pixel_aspect(); }

  const char* help() { return "Apple QuickTime. "; }

private:
  static void convertARGBTo4444YpCbCrA8RPixelFormat( const float* pr, const float* pg, const float* pb, unsigned char* q, int length );
  static void convertARGBTo422YpCbCr8PixelFormat( const float* pr, const float* pg, const float* pb, unsigned char* q, int length );
  static void convertARGBTo4444YpCbCrAFPixelFormat( const float* pa, const float* pr, const float* pg, const float* pb, float* q, int length );
};

static Writer* build(Write* iop)
{
  return new movWriter(iop);
}

const Writer::Description movWriter::d("mov\0", build);

#if QUICKTIME_API
static CodecNameSpecListPtr codecs_;
#else
static lqt_codec_info_t** codecs_;
#endif

movWriter::movWriter(Write* iop) :
  Writer(iop)
#if QUICKTIME_API
  , movie_(NULL),
  dataref_(NULL),
  moviehandler_(NULL),
  track_(NULL),
  media_(NULL),
  session_(NULL),
  passflags_(0),
  pixelFormat_( 0 ),
  codecFlags_( 0 ),
  gamma_(2.2f),
  settings_(NULL),
  swapBytes_( false ),
  tagGamma_( false )
#else
  , movie_(0),
  track_(-1)
#endif
  , force_aspect_( false )
{
  audiofile = NULL;
  audio_offset = 0.0f;
  offset_unit = 0;
  fps = 24.0f;
  quality = 2; // Normal
  keyframerate = 1;
  flatten = true;
  _timescale = 2400;
  _frameDuration = 100;
  codec = 0;

#ifdef _WIN32
  //Initialize Quicktime. This isn't necessary on Macs.
  if ( InitializeQTML(kInitializeQTMLUseGDIFlag) != noErr ) {
    iop->error("QuickTime is not installed.");
    _valid = false;
    return;
  }
#endif

#if QUICKTIME_API
  if ( EnterMovies() != noErr ) {
    iop->error("Error initializing QuickTime.");
    _valid = false;
    return;
  }
#endif

  _valid = true;
  codec = default_codec();
}

movWriter::~movWriter()
{
#if QUICKTIME_API
  ExitMovies();
#endif

#ifdef _WIN32

  // Don't call TerminateQTML here on Windows.  It seems to cause a crash on subsequent calls to InitializeQTML.
  // In the absence of a static class destructor, we could either call it on static deinit (which would probably
  // fail, since since InitializeQTML fails on static init), or just leave it to be cleaned up when the process
  // exits, so we'll do that.
  // The same issue is present in mov Reader

  //TerminateQTML();
#endif
}

void movWriter::convertARGBTo4444YpCbCrA8RPixelFormat( const float* pr, const float* pg, const float* pb, unsigned char* q, int length )
{
  for ( int i = 0; i < length; i++ ) {
    float r = *pr++;
    float g = *pg++;
    float b = *pb++;
    *q++ = 255;

    // Rec 601
    *q++ = max( 0.0f, min( 218.0f, 219 * ( r * 0.299f + g * 0.58700f + b * 0.11400f ) + 0.5f ) );
    *q++ = max( 16.0f, min( 235.0f, 128 + 224 * ( -r * 0.16874f - g * 0.33126f + b * 0.50000f ) + 0.5f ) );
    *q++ = max( 16.0f, min( 235.0f, 128 + 224 * ( r * 0.50000f - g * 0.41869f - b * 0.08131f ) + 0.5f ) );
  }
}

void movWriter::convertARGBTo422YpCbCr8PixelFormat( const float* pr, const float* pg, const float* pb, unsigned char* q, int length )
{
  float lr = 0;
  float lg = 0;
  float lb = 0;
  float ly = 0;
  for ( int i = 0; i < length; i++ ) {
    float r = *pr++;
    float g = *pg++;
    float b = *pb++;

    // Rec 601
    float y = 16 + min( 219.0f, 219 * ( r * 0.299f + g * 0.58700f + b * 0.11400f ) + 0.5f );
    if ( i & 1 ) {
      r = (r + lr) * 0.5f;
      g = (g + lg) * 0.5f;
      b = (b + lb) * 0.5f;
      *q++ = max( 16.0f, min( 235.0f, 128 + 224 * ( -r * 0.16874f - g * 0.33126f + b * 0.50000f ) + 0.5f ) );
      *q++ = ly;
      *q++ = max( 16.0f, min( 235.0f, 128 + 224 * ( r * 0.50000f - g * 0.41869f - b * 0.08131f ) + 0.5f ) );
      *q++ = y;
    }
    lr = r;
    lg = g;
    lb = b;
    ly = y;
  }
}

void movWriter::convertARGBTo4444YpCbCrAFPixelFormat( const float* pa, const float* pr, const float* pg, const float* pb, float* q, int length )
{
  const float r255 = 1.0f / 255.0f;
  for ( int i = 0; i < length; i++ ) {
    float r = *pr++;
    float g = *pg++;
    float b = *pb++;

    *q++ = pa ? *pa++ : 1.0f;

    // Rec 601
    *q++ = r255 * (min( 219.0f, 219 * ( r * 0.299f + g * 0.58700f + b * 0.11400f ) ));
    *q++ = r255 * (128 + 224 * ( -r * 0.16874f - g * 0.33126f + b * 0.50000f ));
    *q++ = r255 * (128 + 224 * ( r * 0.50000f - g * 0.41869f - b * 0.08131f ));
  }
}

void movWriter::execute()
{
  if (!_valid) {
    iop->error("Quicktime not installed");
    return;
  }

  if ( !movie_ ) {
    createMovie();
    if ( iop->aborted() )
      return;
  }

#if QUICKTIME_API
  OSStatus serr = noErr;

  //Initialize Media and Track
  if ( !track_ ) {
    track_ = NewMovieTrack( movie_, IntToFixed( width() ), IntToFixed( height() ), 0 );

    media_ = NewTrackMedia(track_, VideoMediaType, _timescale, 0, 0);

    BeginMediaEdits(media_);
  }
#else
  if ( track_ < 0 ) {
    lqt_codec_info_t* encoder = codecs_[this->codec];
    track_ = lqt_add_video_track(movie_, width(), height(),
                                 _timescale, (int)(fps * _timescale + .5f), encoder);

    lqt_set_pixel_aspect(movie_, track_, (int)(aspect() * 1200 + .5f), 1200);
    lqt_set_cmodel(movie_, track_, BC_RGB888);

    //Set quality if selected codec allows
    lqt_parameter_info_t* enc_p = encoder->encoding_parameters;

    for (int i = 0; i < encoder->num_encoding_parameters; i++, enc_p++) {

      if ( strcasecmp(enc_p->real_name, "quality") == 0 &&
           enc_p->type == LQT_PARAMETER_INT ) {
        if ( this->quality > 3 ) {
          lqt_parameter_value_t qualityval = enc_p->val_max;
          lqt_set_video_parameter(movie_, track_, enc_p->name, &qualityval);
        }
      }
    }
  }

  unsigned char** buffer = new unsigned char*[height()];

  Row row(0, width());
  input0().validate();
  input0().request(0, 0, width(), height(), Mask_RGB, 1);
  for (int y = 0; y < height(); ++y) {
    unsigned char* b = buffer[height() - y - 1] = new unsigned char[width() * 3];
    //unsigned char* b = buffer[height()-y-1];
    get(y, 0, width(), Mask_RGB, row);
    if ( iop->aborted() )
      return;

    for (Channel z = Chan_Red; z <= Chan_Blue; incr(z)) {
      const float* from = row[z];

      to_byte(z - 1, b + z - 1, from, NULL, width(), 3);
    }
  }

  lqt_encode_video(movie_, buffer, track_, frame() * _timescale);

  for (int y = 0; y < height(); ++y )
    delete buffer[y];
  delete buffer;
#endif

#if QUICKTIME_API
  #if __BIG_ENDIAN__
  swapBytes_ = false;
  #else
  swapBytes_ = true;
  #endif
  tagGamma_ = false;
  int depth = iop->depth();
  if (depth > 4)
    depth = 4;
  if (depth < 3)
    depth = 3;

  if ( !session_ ) {
    ICMCompressionSessionOptionsRef sessionOptions = NULL;

    // Set the compression options from the "settings" knob, if any
    QTAtomContainer container = decodeSettings();
    if ( container ) {
      ComponentInstance component = OpenDefaultComponent( StandardCompressionType, StandardCompressionSubType );

      if ( component ) {
        if ( container ) {
          ComponentResult error;
          error = SCSetSettingsFromAtomContainer( component, container );
          DisposeHandle( container );
          if ( error == noErr )
            error = SCCopyCompressionSessionOptions( component, &sessionOptions );
        }
        CloseComponent( component );
      }
    }

    // Create the compression sesssion options
    if ( !sessionOptions ) {
      ICMCompressionSessionOptionsCreate(NULL, &sessionOptions);
      ICMCompressionSessionOptionsSetAllowTemporalCompression(sessionOptions, true);
      ICMCompressionSessionOptionsSetAllowFrameReordering(sessionOptions, true);
      ICMCompressionSessionOptionsSetMaxKeyFrameInterval(sessionOptions, keyframerate);

      // Set Quality
      CodecQ quality = table[this->quality];
      ICMCompressionSessionOptionsSetProperty(sessionOptions,
                                              kQTPropertyClass_ICMCompressionSessionOptions,
                                              kICMCompressionSessionOptionsPropertyID_Quality,
                                              sizeof(CodecQ),
                                              &quality);

      if ( depth == 4 || pixelFormat_ == k4444YpCbCrAFPixelFormat ) {
        // Set Depth. The default is k24RGBPixelFormat which removes the alpha channel
        UInt32 cdepth = k32ARGBPixelFormat; // You might think we'd want k64ARGBPixelFormat with k64ARGBPixelFormat. You'd be wrong.
        ICMCompressionSessionOptionsSetProperty(sessionOptions,
                                                kQTPropertyClass_ICMCompressionSessionOptions,
                                                kICMCompressionSessionOptionsPropertyID_Depth,
                                                sizeof(UInt32),
                                                &cdepth);
      }
    }

    CodecType codec = (codecs_->list)[this->codec].cType;

    // Create the compression session
    ICMEncodedFrameOutputRecord frameoutrec = {
      0
    };
    frameoutrec.encodedFrameOutputCallback = movWriter::addFrame;
    frameoutrec.encodedFrameOutputRefCon = (void*)this;
    frameoutrec.frameDataAllocator = NULL;

    serr = ICMCompressionSessionCreate(NULL, width(), height(), codec,
                                       _timescale,
                                       sessionOptions, NULL,
                                       &frameoutrec, &session_);
    ICMCompressionSessionOptionsRelease( sessionOptions );

    if ( serr != noErr ) {
      iop->error("Failed to create compression session");
      return;
    }
  }

  // Now create the pixel buffer
  CVPixelBufferRef pixelBuffer = NULL;
  input0().validate();
  ChannelSetInit channelSet = depth == 4 ? Mask_RGBA : Mask_RGB;
  input0().request( 0, 0, width(), height(), channelSet, 1 );

  Row row(0, width());

  // Read image data
  void* pixels;
  float* r = NULL;
  float* g = NULL;
  float* b = NULL;
  int bytesPerLine;

  switch ( pixelFormat_ ) {
    case k4444YpCbCrA8RPixelFormat:
    {
      r = Memory::allocate<float>( width() );
      g = Memory::allocate<float>( width() );
      b = Memory::allocate<float>( width() );
      bytesPerLine = 4 * width();
      pixels = (void*)Memory::allocate_void( bytesPerLine * height() );
      unsigned char* pc = (unsigned char*)pixels;

      for ( int y = height() - 1; y >= 0; y-- ) {
        get(y, 0, width(), channelSet, row);
        if ( iop->aborted() )
          break;

        to_float( 0, r, row[Chan_Red], NULL, width() );
        to_float( 1, g, row[Chan_Green], NULL, width() );
        to_float( 2, b, row[Chan_Blue], NULL, width() );
        convertARGBTo4444YpCbCrA8RPixelFormat( r, g, b, pc, width() );

        pc += width() * 4;
        progressFraction(double(height() - y) / height());
      }
    }
    break;

    case k4444YpCbCrAFPixelFormat:
    {
      r = Memory::allocate<float>( width() );
      g = Memory::allocate<float>( width() );
      b = Memory::allocate<float>( width() );
      bytesPerLine = sizeof(float) * 4 * width();
      pixels = (void*)Memory::allocate_void( bytesPerLine * height() );
      float* pf = (float*)pixels;

      for ( int y = height() - 1; y >= 0; y-- ) {
        get(y, 0, width(), channelSet, row);
        if ( iop->aborted() )
          break;

        to_float( 0, r, row[Chan_Red], NULL, width() );
        to_float( 1, g, row[Chan_Green], NULL, width() );
        to_float( 2, b, row[Chan_Blue], NULL, width() );
        convertARGBTo4444YpCbCrAFPixelFormat( row[Chan_Alpha], r, g, b, pf, width() );

        pf += width() * 4;
        progressFraction(double(height() - y) / height());
      }
    }
    break;

    case k422YpCbCr8PixelFormat:
    {
      r = Memory::allocate<float>( width() );
      g = Memory::allocate<float>( width() );
      b = Memory::allocate<float>( width() );
      bytesPerLine = 4 * width();
      pixels = (void*)Memory::allocate_void( bytesPerLine * height() );
      unsigned char* pc = (unsigned char*)pixels;

      for ( int y = height() - 1; y >= 0; y-- ) {
        get(y, 0, width(), channelSet, row);
        if ( iop->aborted() )
          break;

        to_float( 0, r, row[Chan_Red], NULL, width() );
        to_float( 1, g, row[Chan_Green], NULL, width() );
        to_float( 2, b, row[Chan_Blue], NULL, width() );
        convertARGBTo422YpCbCr8PixelFormat( r, g, b, pc, width() );

        pc += width() * 4;
        progressFraction(double(height() - y) / height());
      }
    }
    break;

    case k64ARGBPixelFormat:
    {
      bytesPerLine = 8 * width();
      pixels = (void*)Memory::allocate_void( bytesPerLine * height() );
      unsigned short* ps = (unsigned short*)pixels;

      for ( int y = height() - 1; y >= 0; y-- ) {
        get(y, 0, width(), channelSet, row);
        if ( iop->aborted() )
          break;

        for (Channel z = Chan_Red; z <= (depth == 4 ? Chan_Alpha : Chan_Blue); incr(z)) {
          const float* from = row[z];
          const float* alpha = depth > 3 ? row[Chan_Alpha] : 0;

          if ( depth == 4 ) {
            if ( z == Chan_Alpha )
              to_short(z - 1, ps, from, NULL, width(), 16, depth);
            else
              to_short(z - 1, ps + z, from, alpha, width(), 16, depth);
          }
          else
            to_short(z - 1, ps + z - 1, from, NULL, width(), 16, depth);
        }

        // Quicktime API expects big endian shorts, except for the codecs which get it wrong
        if ( pixelFormat_ == k64ARGBPixelFormat && swapBytes_ )
          flip(ps, width() * depth);

        ps += width() * depth;
        progressFraction(double(height() - y) / height());
      }
    }
    break;

    case k32ARGBPixelFormat:
    default:
    {
      bytesPerLine = 4 * width();
      pixels = Memory::allocate_void( bytesPerLine * height() );
      unsigned char* pc = (unsigned char*)pixels;

      for ( int y = height() - 1; y >= 0; y-- ) {
        get(y, 0, width(), channelSet, row);
        if ( iop->aborted() )
          break;

        for (Channel z = Chan_Red; z <= (depth == 4 ? Chan_Alpha : Chan_Blue); incr(z)) {
          const float* from = row[z];
          const float* alpha = depth > 3 ? row[Chan_Alpha] : 0;

          if ( depth == 4 ) {
            if ( z == Chan_Alpha )
              to_byte(z - 1, pc, from, NULL, width(), 4);
            else
              to_byte(z - 1, pc + z, from, alpha, width(), 4);
          }
          else
            to_byte(z - 1, pc + z, from, NULL, width(), 4);
        }

        pc += width() * 4;
        progressFraction(double(height() - y) / height());
      }
    }
    break;
  }

  if ( r )
    Memory::deallocate<float>( r, width() );
  if ( g )
    Memory::deallocate<float>( g, width() );
  if ( b )
    Memory::deallocate<float>( b, width() );

  if ( iop->aborted() ) {
    Memory::deallocate_void( pixels, bytesPerLine * height() );
    return;
  }

  //Package image data as pixel buffer
  CVReturn ret = CVPixelBufferCreateWithBytes(
    kCFAllocatorDefault,
    width(),
    height(),
    pixelFormat_,
    pixels,
    bytesPerLine,
    movWriter::bufferRelease,
    (void*)(size_t)(bytesPerLine * height()),
    NULL,
    &pixelBuffer );

  if ( ret != kCVReturnSuccess ) {
    Memory::deallocate_void( pixels, bytesPerLine * height() );
    iop->error("Failed to create pixel buffer");
    return;
  }

  // Set pixel buffer gamma level
  if ( tagGamma_ ) {
    CFNumberRef gammaLevel = CFNumberCreate(NULL, kCFNumberFloat32Type, &gamma_);
    CVBufferSetAttachment(pixelBuffer, kCVImageBufferGammaLevelKey, gammaLevel, kCVAttachmentMode_ShouldPropagate);
    CFRelease(gammaLevel);
  }

  // Encode Frame
  ICMValidTimeFlags timeflags = kICMValidTime_DisplayDurationIsValid | kICMValidTime_DisplayTimeStampIsValid;
  serr = ICMCompressionSessionEncodeFrame( session_, pixelBuffer,
                                           _frameDuration * frame(),
                                           _frameDuration,
                                           timeflags,
                                           NULL,
                                           NULL,
                                           NULL);

  CVPixelBufferRelease( pixelBuffer );

  if (serr != noErr && serr != noCodecErr ) {
    iop->error("Couldn't encode frame");
    return;
  }
#endif
}

/*
   Closes the destination movie and cleans up memory allocations.
 */
void movWriter::finish()
{
#if QUICKTIME_API
  if ( session_ )
    ICMCompressionSessionCompleteFrames( session_, true, 0, 0 );

  if ( track_ && media_ ) {
    OSErr err = EndMediaEdits(media_);
    err = InsertMediaIntoTrack(track_, 0, 0, GetMediaDuration(media_), fixed1);
  }

  if ( movie_ )
    addAudio();

  if ( movie_ && moviehandler_ ) {
    OSErr err = UpdateMovieInStorage( movie_, moviehandler_ );

    if ( err != noErr )
      iop->error("Failed to update movie file");
  }

  if ( session_ )
    ICMCompressionSessionRelease(session_);
  if ( moviehandler_ )
    CloseMovieStorage(moviehandler_);
  if ( dataref_ )
    DisposeHandle(dataref_);

  if ( movie_ ) {
    if ( flatten )
      flattenMovie();
    else {
      remove(filename());

      if ( rename(tempFilePath_.c_str(), filename()) )
        iop->error("Can't rename .tmp to final: %s", strerror(errno));
    }

    DisposeMovie(movie_);
  }

  remove( tempFilePath_.c_str() );

  movie_ = NULL;
  dataref_ = NULL;
  moviehandler_ = NULL;
  track_ = NULL;
  media_ = NULL;
  session_ = NULL;
#else
  if ( movie_ )
    quicktime_close(movie_);

  movie_ = NULL;
  track_ = -1;
#endif
}

/*
   Sorts a list of CodecNameSpec's alphabetically by name.
 */
#if QUICKTIME_API
int movWriter::specsort(const void* a, const void* b)
{
  CodecNameSpec* cnsa = (CodecNameSpec*)a;
  CodecNameSpec* cnsb = (CodecNameSpec*)b;

  const char* a_c = (const char*) cnsa->typeName;
  const char* b_c = (const char*) cnsb->typeName;

  return strcasecmp(a_c + 1, b_c + 1);
}
#endif

const char* const* movWriter::codecList()
{
  static char** codecnames = 0;
  if ( codecnames )
    return codecnames;

#if !QUICKTIME_API
  //Create plugin registry'
  //lqt_registry_init();

  codecs_ = lqt_query_registry(0, 1, 1, 0);

  int numcodecs = 0;
  lqt_codec_info_t* c = codecs_[0];
  while (c != NULL) {
    numcodecs++;
    c = codecs_[numcodecs];
  }

  //qsort(codecs_, numcodecs, sizeof(lqt_codec_info_t), movWriter::specsort);

  codecnames = new char*[numcodecs + 1];

  char fourcc[5];
  for (int i = 0; i < numcodecs; i++) {
    const char* name = (codecs_[i])->long_name;
    codecnames[i] = new char[strlen(name) + 10];

    snprintf(fourcc, 5, "%s", (codecs_[i])->fourccs[0]);
    for (int j = 0; j < 4; j++)
      fourcc[j] = (char)tolower( fourcc[j] );

    sprintf(codecnames[i], "%s\t%s", fourcc, name);
  }

  codecnames[numcodecs] = NULL;
#else
  if ( GetCodecNameList(&codecs_, 1) == noErr ) {
    qsort(codecs_->list, codecs_->count, sizeof(CodecNameSpec),
          movWriter::specsort);

    codecnames = new char*[ codecs_->count + 1 ];
    char fourcc[5];
    for (short i = 0; i < codecs_->count; i++) {
      const char* codecname = (const char*) (codecs_->list)[i].typeName;
      int length = (int)(*codecname++);

      CodecType cType = (codecs_->list)[i].cType;
      *(CodecType*)fourcc = EndianU32_NtoB( cType );
      fourcc[4] = '\0';

      codecnames[i] = new char[length + 6];
      snprintf(codecnames[i], length + 6, "%s\t%s", fourcc, codecname);
      codecnames[i][length + 5] = '\0';
    }

    codecnames[codecs_->count] = NULL;
  }
#endif

  return codecnames;
}

int movWriter::default_codec()
{
  static int defval = -1;
  if (defval < 0) {
    const char* const* codecnames = codecList();
    // Set the default value to sorenson 3
    for ( int i = 0; codecnames[i]; i++ ) {
      if ( !strcasecmp (codecnames[i], "mjpa\tmotion jpeg a") ) {
        defval = i;
        break;
      }
    }
  }
  return defval;
}

#include "DDImage/Knobs.h"

void movWriter::knobs(Knob_Callback f)
{
  // We can't set up QuickTime-specific knobs if it isn't installed
  if (!_valid)
    return;

  Enumeration_knob(f, &codec, codecList(), "codec");
  Button(f, "advanced");
  Bool_knob(f, &flatten, "Flatten", "Fast Start" );
  Tooltip(f, "Flattens a movie so it can be played while still downloading");
  Float_knob(f, &fps, IRange(0.0, 100.0), "fps");
  SetFlags(f, Knob::INVISIBLE);

  Bool_knob( f, &force_aspect_, "use_format_aspect", "use format aspect" );
  Tooltip( f, "If on, use the incoming format's pixel aspect ratio.\nIf off, allow the codec to set the aspect ratio.\nCodecs wriitng formats such as PAL and NTSC should typically be allowed to set the aspect ratio, but you may want to override this for other codecs which otherwise assume square pixels." );

  static const char* _movqualities[] = {
    "Min", "Low", "Normal", "High", "Max", "Lossless", 0
  };
  Enumeration_knob(f, &quality, _movqualities, "quality");
  SetFlags(f, Knob::INVISIBLE);
  Int_knob(f, &keyframerate, IRange(0, 10), "keyframerate", "keyframe rate");
  SetFlags(f, Knob::INVISIBLE);

  File_knob(f, &audiofile, "audiofile", "audio file");
  Float_knob(f, &audio_offset, IRange(-1000.0, 1000.0),
             "audio_offset", "audio offset");
  Tooltip(f, "Offset the audio file by the given number of seconds/frames. "
             "A value of -10 seconds means the first frame of the image "
             "sequence syncs to the 10 second mark of the audio. A value "
             "of +10 seconds means the audio will start 10 seconds into "
             "the image sequence");
  static const char* _offset_units[] = {
    "Seconds", "Frames", 0
  };
  Enumeration_knob(f, &offset_unit, _offset_units, "units");

#if QUICKTIME_API
  // This knob stores a hex-encoded QTAtomContainer containing any settings made via the Standard Compression Dialog.
  String_knob(f, &settings_, "settings", "settings");
  SetFlags(f, Knob::INVISIBLE);
#endif
}

#if QUICKTIME_API
QTAtomContainer movWriter::decodeSettings()
{
  QTAtomContainer container = NULL;
  Knob* settings = iop->knob("settings");

  if ( settings ) {
    // Decode the settings string
    const char* hex = settings->get_text();
    if ( hex && *hex ) {
      Size size = strlen( hex ) / 2;
      container = NewHandle( size );
      Ptr p = *container;
      bool first = true;
      for ( int i = 0; i < size * 2; i++, first = !first ) {
        char c = hex[i];
        if ( c >= 'a' )
          c = c - 'a' + 10;
        else
          c -= '0';
        if ( first )
          *p = c << 4;
        else {
          *p |= c;
          p++;
        }
      }
    }
  }
  return container;
}

void movWriter::encodeSettings( QTAtomContainer container )
{
  Knob* settings = iop->knob("settings");
  Ptr p = *container;
  Size size = GetHandleSize( container );
  char* hex = new char[2 * size + 1];
  char* q = hex;
  static char* digits = "0123456789abcdef";
  for ( int i = 0; i < size; i++ ) {
    *q++ = digits[(p[i] >> 4) & 0xf];
    *q++ = digits[p[i] & 0xf];
  }
  *q++ = 0;
  settings->set_text( hex );
  delete hex;
}
#endif

int movWriter::knob_changed(DD::Image::Knob* knob)
{
#if QUICKTIME_API
  if ( knob->name() && strcmp( knob->name(), "advanced" ) == 0 ) {
    ComponentInstance component = OpenDefaultComponent( StandardCompressionType, StandardCompressionSubType );

    if ( component ) {
      ComponentResult error;

      // Initialize the settings dialog from the knobs.
      // We need to call SCDefaultPixMapSettings in order to get/set the spatial and temporal info. This is not documented.
      // This is a real pain - we need to create a PixMapHandle just for this call. Note that the APIs we use for this are
      // deprecated and will cause compiler warnings, but Apple in their infinite wisdom, have not provided undeprecated
      // replacements.
      GWorldPtr gworld;
      Rect r = {
        0, 0, height(), width()
      };
      OSErr err = QTNewGWorld( &gworld, k32ARGBPixelFormat, &r, NULL, NULL, 0 );
      if ( err == noErr ) {
        error = SCDefaultPixMapSettings( component, GetGWorldPixMap( gworld ), true );
        DisposeGWorld( gworld );

        SCSpatialSettings scSpatialSettings;
        error = SCGetInfo( component, scSpatialSettingsType, &scSpatialSettings );
        if ( error == noErr ) {
          scSpatialSettings.codecType = (codecs_->list)[codec].cType;
          scSpatialSettings.codec = NULL;
          scSpatialSettings.spatialQuality = table[quality];
          scSpatialSettings.depth = 32;
          error = SCSetInfo( component, scSpatialSettingsType, &scSpatialSettings );
        }

        SCTemporalSettings scTemporalSettings;
        error = SCGetInfo( component, scTemporalSettingsType, &scTemporalSettings );
        if ( error == noErr ) {
          scTemporalSettings.temporalQuality = table[quality];
          scTemporalSettings.frameRate = FloatToFixed( fps );
          scTemporalSettings.keyFrameRate = keyframerate;
          error = SCSetInfo( component, scTemporalSettingsType, &scTemporalSettings );
        }
      }

      long scPreferences = scAllowEncodingWithCompressionSession;
      error = SCSetInfo( component, scPreferenceFlagsType, &scPreferences );

      QTAtomContainer container = decodeSettings();
      if ( container ) {
        error = SCSetSettingsFromAtomContainer( component, container );
        DisposeHandle( container );
      }
      else {
        // This is to work round some QuickTime weirdness: If we don't call SCSetSettingsFromAtomContainer before
        // showing the dialog, we seem to get the original setting back when we call SCGetSettingsAsAtomContainer
        // afterwards.
        error = SCGetSettingsAsAtomContainer( component, &container );
        if ( error == noErr ) {
          error = SCSetSettingsFromAtomContainer( component, container );
          QTDisposeAtomContainer( container );
        }
      }

      error = SCRequestSequenceSettings( component );
      if ( error == noErr ) {
        // Set the knobs from the settings
        SCSpatialSettings scSpatialSettings;
        error = SCGetInfo( component, scSpatialSettingsType, &scSpatialSettings );
        if ( error == noErr ) {
          for ( int i = 0; codecs_->count; i++ )
            if ( scSpatialSettings.codecType == (codecs_->list)[i].cType ) {
              iop->knob("codec")->set_value( i );
              break;
            }

          iop->knob("quality")->set_value( indexForQuality( scSpatialSettings.spatialQuality ) );
        }

        SCTemporalSettings scTemporalSettings;
        error = SCGetInfo( component, scTemporalSettingsType, &scTemporalSettings );
        if ( error == noErr ) {
          iop->knob("fps")->set_value( FixedToFloat( scTemporalSettings.frameRate ) );
          iop->knob("keyframerate")->set_value( scTemporalSettings.keyFrameRate );
        }

        // Encode the settings string
        error = SCGetSettingsAsAtomContainer( component, &container );
        if ( error == noErr ) {
          encodeSettings( container );
          QTDisposeAtomContainer( container );
        }
      }
      CloseComponent( component );
    }
    return 1;
  }
  else if ( knob->name() && strcmp( knob->name(), "codec" ) == 0 ) {
    Knob* settings = iop->knob("settings");
    if ( settings )
      settings->set_text( "" );
    return 1;
  }
#endif
  return 0;
}

/*
   Initializes the storage components for the requested output movie.
 */

#if QUICKTIME_API
void movWriter::createMovie()
{
  OSType reftype;
  OSErr err = noErr;
  CFStringRef tempfile;

  tempFilePath_ = filename();
  tempFilePath_ += ".tmp";

  remove( tempFilePath_.c_str() );

  tempfile = CFStringCreateFromPath( tempFilePath_.c_str() );

  if ( QTNewDataReferenceFromFullPathCFString(tempfile, kQTPlatformPathStyle,
                                              0, &dataref_, &reftype) ) {
    iop->error("Couldn't create data reference for destination");
    return;
  }

  err = CreateMovieStorage(dataref_, reftype, FOUR_CHAR_CODE('TVOD'),
                           smSystemScript,
                           createMovieFileDeleteCurFile | createMovieFileDontCreateResFile,
                           &moviehandler_, &movie_);

  CFRelease(tempfile);

  if ( err != noErr ) {
    iop->error("Couldn't create output movie storage %d", err);
    return;
  }

  if ( AddMovieToStorage(movie_, moviehandler_) != noErr ) {
    iop->error("Failed to write movie to output file");
    return;
  }

  // The following is for the benefit of Final Cut Pro which has a number of problems with decoding frame rates.
  // See Technical Q&A QA1447: Final Cut Pro - Preferred Video Media Time Scales and Sample Durations
  // at http://developer.apple.com/mac/library/qa/qa2005/qa1447.html for details.
  if ( fabs( fps - 23.98 ) < 1e-3 ) {
    _timescale = 23976;
    _frameDuration = 1000;
  }
  else {
    _timescale = int( 100 * fps + 0.5f );
    _frameDuration = 100;
  }

  SetMovieTimeScale( movie_, _timescale );

  // Now find out what pixel formats the codec supports and choose the one we most like.
  CodecType codec = (codecs_->list)[this->codec].cType;
  ComponentDescription cd = {
    compressorComponentType, 0, 0, 0, cmpIsMissing
  };
  Component compressor = 0;
  cd.componentSubType = codec;
  compressor = FindNextComponent( 0, &cd );

  getCodecInfo( compressor, &pixelFormat_, &codecFlags_ );

  #if 0
  // Debugging code to tell us which pixelformat we actually chose to use
  char* pz = (char*)&codec;
  cout << "write: codec=";
  cout << pz[3] << pz[2] << pz[1] << pz[0];
  cout << " format=";
  pz = (char*)&pixelFormat_;
  if ( (pixelFormat_ & 0xff000000) == 0 )
    cout << pixelFormat_ << endl;
  else
    cout << pz[3] << pz[2] << pz[1] << pz[0] << endl;
  cout << endl;
  #endif
}
#else
void movWriter::createMovie()
{
  movie_ = quicktime_open((char*)filename(), 0, 1);

  if ( !movie_ )
    return iop->error("Couldn't open file %s for writing", filename());
}
#endif

#if QUICKTIME_API
/*
   If the user has specified an audio file, add that to the Quicktime
   movie now.
 */
void movWriter::addAudio()
{
  if (!audiofile || !*audiofile)
    return;

  CFStringRef audioinput;
  Movie src = NULL;
  short resID = 0;

  DataHandler audiohandler;
  Handle audioref;
  OSType reftype;
  OSErr err = noErr;

  audioinput = CFStringCreateFromPath( audiofile );

  if ( QTNewDataReferenceFromFullPathCFString(audioinput, kQTPlatformPathStyle,
                                              0, &audioref, &reftype) ) {
    iop->error("Couldn't create data reference for destination");
    return;
  }

  err = OpenMovieStorage(audioref, reftype, kDataHCanRead, &audiohandler);

  if ( err != noErr ) {
    iop->error("Couldn't open audio file for reading: %d", err);
    return;
  }

  err = NewMovieFromDataRef(&src, newMovieActive, &resID, audioref, reftype);

  if ( err != noErr ) {
    iop->error("Didn't recognize format of audio file: %d", err);
    return;
  }

  Track srctrack;
  long trackindex = 0;
  TimeValue destduration = GetMovieDuration(movie_);

  while ( (srctrack = GetMovieIndTrackType(src, ++trackindex,
                                           AudioMediaCharacteristic,
                                           movieTrackCharacteristic))) {
    OSType type;

    Media srcmedia = GetTrackMedia(srctrack);
    Track desttrack = NewMovieTrack(movie_, 0, 0, GetTrackVolume(srctrack));
    GetMediaHandlerDescription(srcmedia, &type, 0, 0);
    Media destmedia = NewTrackMedia(desttrack, type,
                                    GetMediaTimeScale(srcmedia), 0, 0);

    BeginMediaEdits(destmedia);

    TimeRecord srcTime;
    TimeRecord dstTime;
    TimeValue srcIn = 0;
    TimeValue dstIn = 0;

    float offset = audio_offset;
    if ( offset_unit == 1 )
      offset = audio_offset / fps;

    if ( offset < 0 ) {
      GetMovieTime(src, &srcTime);
      srcIn = (TimeValue)(-1 * offset * srcTime.scale);
    }
    else if ( offset > 0 ) {
      GetMovieTime(movie_, &dstTime);
      dstIn = (TimeValue)(offset * dstTime.scale);
    }

    InsertTrackSegment(srctrack, desttrack, srcIn,
                       GetTrackDuration(srctrack), dstIn);
    CopyTrackSettings(srctrack, desttrack);
    SetTrackLayer(desttrack, GetTrackLayer(srctrack));
    EndMediaEdits(destmedia);
  }

  //Remove any audio that extends beyond the video portion of the movie
  DeleteMovieSegment(movie_, destduration,
                     GetMovieDuration(movie_) - destduration);

  DisposeMovie(src);
  CloseMovieStorage(audiohandler);
}

/*
   Flatten movie data so it can be played over a network link
   while the movie is still loading.
 */
void movWriter::flattenMovie()
{
  Handle flatfile;
  OSType reftype;
  CFStringRef path;
  OSErr err = noErr;

  //The flags to delete the current movie doesn't work as well
  //as I would like.
  remove(filename());

  path = CFStringCreateFromPath(filename());

  err = QTNewDataReferenceFromFullPathCFString(path, kQTPlatformPathStyle,
                                               0, &flatfile, &reftype);

  CFRelease(path);

  if ( err != noErr )
    return iop->error("Illegal file name: %s", filename());

  Movie flatMovie =
    FlattenMovieDataToDataRef(movie_,
                              flattenAddMovieToDataFork |
                              flattenForceMovieResourceBeforeMovieData,
                              flatfile,
                              reftype,
                              FOUR_CHAR_CODE('TVOD'),
                              smSystemScript,
                              createMovieFileDeleteCurFile |
                              createMovieFileDontCreateResFile);

  DisposeHandle(flatfile);

  if ( !flatMovie ) {
    OSErr error = GetMoviesError();
    // This is a very common and mystifying case: even having the file selected in the Finder can cause it.
    if ( error == fBsyErr )
      return iop->error("Failed to flatten movie data: the movie is open in another application");
    return iop->error("Failed to flatten movie data");
  }

  DisposeMovie(flatMovie);
}

/*
   This is a static callback function called by the compressor after each
   frame has finished.  The function's main purpose is to take that
   compressed data and add it to the video media for our movie.
 */
OSStatus movWriter::addFrame(void* refcon, ICMCompressionSessionRef session,
                             OSStatus err, ICMEncodedFrameRef encodedFrame,
                             void* reserved)
{
  ImageDescriptionHandle imagedesc = NULL;
  movWriter* writer = (movWriter*)refcon;
  Media* media = writer->media();

  if ( err != noErr )
    return err;

  err = ICMEncodedFrameGetImageDescription( encodedFrame, &imagedesc );

  if ( err != noErr )
    return err;

  if ( writer->force_aspect_ ) {
    //Set pixel aspect ratio accordingly
    PixelAspectRatioImageDescriptionExtension aspect;
    aspect.hSpacing = int(floor(writer->aspect() * 1000));
    aspect.vSpacing = 1000;

    err = ICMImageDescriptionSetProperty( imagedesc,
                                          kQTPropertyClass_ImageDescription,
                                          kICMImageDescriptionPropertyID_PixelAspectRatio,
                                          sizeof(PixelAspectRatioImageDescriptionExtension),
                                          &aspect);

    if ( err != noErr )
      return err;
  }

  if ( writer->pixelFormat_ != k32ARGBPixelFormat && writer->pixelFormat_ != k48RGBPixelFormat && writer->pixelFormat_ != k64ARGBPixelFormat ) {
    NCLCColorInfoImageDescriptionExtension nclc = {
      kVideoColorInfoImageDescriptionExtensionType,
      kQTPrimaries_Unknown,
      kQTTransferFunction_Unknown,
      kQTMatrix_ITU_R_601_4
    };

    err = ICMImageDescriptionSetProperty( imagedesc,
                                          kQTPropertyClass_ImageDescription,
                                          kICMImageDescriptionPropertyID_NCLCColorInfo,
                                          sizeof(nclc),
                                          &nclc);
  }

  err = AddMediaSample2(
    *media,
    ICMEncodedFrameGetDataPtr( encodedFrame ),
    ICMEncodedFrameGetDataSize( encodedFrame ),
    ICMEncodedFrameGetDecodeDuration( encodedFrame ),
    ICMEncodedFrameGetDisplayOffset( encodedFrame ),
    (SampleDescriptionHandle) imagedesc,
    1,
    ICMEncodedFrameGetMediaSampleFlags( encodedFrame ),
    NULL );

  return err;
}

// The callback to release our PixelBuffer's pixel data
void movWriter::bufferRelease( void* releaseRefCon, const void* baseaddr )
{
  if ( baseaddr )
    Memory::deallocate_void( const_cast<void*>( baseaddr ), (size_t)releaseRefCon );
}

CFStringRef movWriter::CFStringCreateFromPath( const char* cpath ) const
{
  CFStringRef ref;
  std::string path( cpath );

  #ifdef _WIN32
  std::replace( path.begin(), path.end(), '/', '\\' );

  // Qt on win32 seems that doesn't like relative path
  // In case of a relative path add the current directory
  if ( path.size() > 2 ) {

    // ignore full-path
    if ( path[1] != ':' && path[0] != '\\' ) {
      char szDirectory[MAX_PATH] = "";

      if ( GetCurrentDirectory(sizeof(szDirectory) - 1, szDirectory) ) {
        path = std::string(szDirectory) + std::string("\\") + path;
      }
    }
  }
  #endif

  ref = CFStringCreateWithBytes(NULL, (const UInt8*)path.c_str(),
                                path.size(), kCFStringEncodingASCII,
                                false);

  return ref;
}

#endif
