// exrReader.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

/* Reads exr files using libexr.

   04/14/03     Initial Release                Charles Henrich (henrich@d2.com)
   10/14/03     Added channel name conforming  Charles Henrich (henrich@d2.com)
   10/16/04    Lots of channel changes        Bill Spitzak
   03/27/05    Single frame buffer        Bill Spitzak
   01/17/08     all channel sorting done by Nuke spitzak
 */
#include "DDImage/DDWindows.h"
#include "DDImage/Reader.h"
#include "DDImage/Row.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"
#include "DDImage/Thread.h"
#include "DDImage/Memory.h"

#ifdef _WIN32
  #define OPENEXR_DLL
  #include <io.h>
#endif
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfVecAttribute.h>
#include <OpenEXR/ImfBoxAttribute.h>
#include <OpenEXR/ImfStringVectorAttribute.h>
#include <OpenEXR/ImfTimeCodeAttribute.h>
#include <OpenEXR/ImfStandardAttributes.h>
#include <OpenEXR/ImfMatrixAttribute.h>
#include <OpenEXR/half.h>

#ifdef __linux
  #define EXR_USE_MMAP
#endif

#ifdef EXR_USE_MMAP
  #include <unistd.h>
  #include <sys/mman.h>
  #include <fcntl.h>
  #include <errno.h>
#endif

using namespace DD::Image;

// This structure is just to access the validchanname() function below:
class ChannelName
{
public:
  ChannelName(const char* name) { setname(name); }
  ~ChannelName() {}
  void setname(const char*);
  std::string chan;
  std::string layer;
  const std::string name();
};

static bool endswith(const std::string& target, const std::string& suffix)
{
  if (target.length() < suffix.length())
    return false;

  return target.substr(target.length() - suffix.length()) == suffix;
}

static bool startswith(const std::string& target, const std::string& prefix)
{
  if (target.length() < prefix.length())
    return false;

  return target.substr(0, prefix.length()) == prefix;
}

std::string tolower(const std::string& s)
{
  std::string r = s;
  for (size_t i = 0; i < r.size(); i++) {
    r[i] = tolower(r[i]);
  }
  return r;
}

class exrReaderFormat : public ReaderFormat
{

  friend class exrReader;

  bool _disable_mmap;

public:

  bool disable_mmap() const
  {
    return _disable_mmap;
  }

  exrReaderFormat()
  {
    _disable_mmap = false;
  }

  void knobs(Knob_Callback c)
  {
    Bool_knob(c, &_disable_mmap, "disable_mmap", "disable use of mmap()");
    Tooltip(c, "Some EXR files are compressed such that is is much faster to decompress the entire image at once, rather than decompressing each line individually. Decompressing the image at once may take more memory than is available.  This option is provided to disable this.");
#ifndef EXR_USE_MMAP
    SetFlags(c, Knob::INVISIBLE);
#endif
  }

  void append(Hash& hash)
  {
  }
};

class exrReader : public Reader
{
  Imf::InputFile* inputfile;

  Lock C_lock;
  std::map<Channel, const char*> channel_map;
  bool fileStereo_;
  std::vector<std::string> views;
  std::string heroview;

  MetaData::Bundle _meta;

#ifdef EXR_USE_MMAP
  static bool exr_mmap_bad;
  bool exr_use_mmap;
  int fd;
  bool fileLoaded;
  int pagesize;
#endif

public:

  const MetaData::Bundle& fetchMetaData(const char* key);

  exrReader(Read*);
  ~exrReader();

#ifdef EXR_USE_MMAP
  bool mmap_engine(const Imath::Box2i& datawin,
                   const Imath::Box2i& dispwin,
                   const ChannelSet&   channels,
                   int                 exrY,
                   Row&                row,
                   int                 x,
                   int                 X,
                   int                 r,
                   int                 R);
#endif

  void normal_engine(const Imath::Box2i& datawin,
                     const Imath::Box2i& dispwin,
                     const ChannelSet&   channels,
                     int                 exrY,
                     Row&                row,
                     int                 x,
                     int                 X,
                     int                 r,
                     int                 R);

  void engine(int y, int x, int r, ChannelMask, Row &);
  void _validate(bool for_real);
  static const Description d;

  bool supports_stereo() const
  {
    return true;
  }

  bool fileStereo() const
  {
    return fileStereo_;
  }

  void lookupChannels(std::set<Channel>& channel, const char* name)
  {
    if (strcmp(name, "y") == 0 || strcmp(name, "Y") == 0) {
      channel.insert(Chan_Red);
      if (!iop->raw()) {
        channel.insert(Chan_Green);
        channel.insert(Chan_Blue);
      }
    }
    else {
      channel.insert(Reader::channel(name));
    }
  }

  bool getChannels(const char* name, int view, std::set<Channel>& channel)
  {

    std::string viewpart = heroview;
    std::string otherpart = name;

    for (unsigned i = 0; i < views.size(); i++) {
      if (startswith(name, views[i] + ".")) {
        viewpart = views[i];
        otherpart = otherpart.substr(views[i].length() + 1);
      }
      else if (startswith(name, views[i] + "_")) {
        viewpart = views[i];
        otherpart = otherpart.substr(views[i].length() + 1);
      }
      else if (endswith(name, "_" + views[i])) {
        viewpart = views[i];
        otherpart = otherpart.substr(0, otherpart.length() - viewpart.length() - 1);
      }
    }

    if (OutputContext::viewname(view) == viewpart) {
      fileStereo_ = true;
      lookupChannels(channel, otherpart.c_str());
      return true;
    }

    if (viewpart != "" && viewpart != heroview) {
      return false;
    }

    lookupChannels(channel, name);

    return true;
  }
};

static Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
  close(fd);
  return new exrReader(iop);
}

static ReaderFormat* buildformat(Read* iop)
{
  return new exrReaderFormat();
}

static bool test(int fd, const unsigned char* block, int length)
{
  return block[0] == 0x76 && block[1] == 0x2f &&
         block[2] == 0x31 && block[3] == 0x01;
}

const Reader::Description exrReader::d("exr\0sxr\0", build, test, buildformat);

const MetaData::Bundle& exrReader::fetchMetaData(const char* key)
{
  return _meta;
}

#ifdef EXR_USE_MMAP
bool exrReader::exr_mmap_bad = false;
#endif

exrReader::exrReader(Read* r) : Reader(r), inputfile(0), fileStereo_(false)
{

  // Make it default to linear colorspace:
  lut_ = LUT::getLut(LUT::FLOAT);

#ifdef EXR_USE_MMAP
  pagesize = sysconf(_SC_PAGE_SIZE) / sizeof(float);
  fileLoaded = false;
  exr_use_mmap = false;
  fd = -1;
#endif

  std::map<Imf::PixelType, int> pixelTypes;

  try {

    inputfile = new Imf::InputFile(r->filename());
    int view = r->view_for_reader();

#ifdef EXR_USE_MMAP
    Imf::Compression compression = inputfile->header().compression();
    exr_use_mmap = (compression == Imf::PIZ_COMPRESSION);

    exrReaderFormat* trf = dynamic_cast<exrReaderFormat*>(r->handler());
    if (trf) {
      exr_use_mmap = exr_use_mmap && !trf->disable_mmap();
    }
    else {
      exr_use_mmap = false;
    }

    if (exr_mmap_bad) {
      exr_use_mmap = false;
    }
    // might also want to try:
    // exr_use_mmap = inputfile->header().hasTileDescription();
    // but we need a test file where these are different
#endif

    const Imf::StringAttribute* stringMultiView = 0;
    const Imf::StringVectorAttribute* vectorMultiView = 0;

    //     if (inputfile->header().hasTileDescription()) {
    //       const Imf::TileDescription& t = inputfile->header().tileDescription();
    //       printf("%s Tile Description:\n", filename());
    //       printf(" %d %d mode %d rounding %d\n", t.xSize, t.ySize, t.mode, t.roundingMode);
    //     }

    // should change this to use header->findTypedAttribute<TYPE>(name)
    // rather than the exception mechanism as it is nicer code and
    // less of it. But I'm too scared to do this at the moment.
    try {
      vectorMultiView = inputfile->header().findTypedAttribute<Imf::StringVectorAttribute>("multiView");
      stringMultiView = inputfile->header().findTypedAttribute<Imf::StringAttribute>("multiView");
    }
    catch (...) {
    }

    if (vectorMultiView) {
      std::vector<std::string> s = vectorMultiView->value();

      for (size_t i = 0; i < s.size(); i++) {
        views.push_back(s[i]);
      }

      if (views.size() > 0) {
        heroview = views[0];
      }
      else {
        views.push_back("left");
        views.push_back("right");
        heroview = "left";
      }

    }
    else {
      views.push_back("left");
      views.push_back("right");
      heroview = "left";
    }

    // For each channel in the file, create or locate the matching Nuke channel
    // number, and store it in the channel_map
    ChannelSet mask;
    const Imf::ChannelList& imfchannels = inputfile->header().channels();
    Imf::ChannelList::ConstIterator chan;
    for (chan = imfchannels.begin(); chan != imfchannels.end(); chan++) {

      pixelTypes[chan.channel().type]++;

      ChannelName cName(chan.name());
      std::set<Channel> channels;

      if (this->getChannels(cName.name().c_str(), view, channels)) {
        if (channels.size()) {
          for (std::set<Channel>::iterator it = channels.begin();
               it != channels.end();
               it++) {
            Channel channel = *it;
            channel_map[channel] = chan.name();
            mask += channel;
          }
        }
        else {
          iop->warning("Cannot assign channel number to %s", cName.name().c_str());
        }
      }

    }

    const Imath::Box2i& datawin = inputfile->header().dataWindow();
    const Imath::Box2i& dispwin = inputfile->header().displayWindow();

    double aspect = inputfile->header().pixelAspectRatio();
    /*
       '0' is obviously an invalid aspect ratio, but we also can't
       trust values of '1'. Internal images were previously always
       created with this value (it was hard-coded into the writer)
       so we can't trust that a '1' really means '1'.
     */
    if (aspect == 1.0)
      aspect = 0;

    set_info(dispwin.max.x - dispwin.min.x + 1,
             dispwin.max.y - dispwin.min.y + 1,
             4, aspect);
    info_.channels(mask);

    // Add a black pixel around the edge of the box, to match what other
    // programs do with exr files, rather than replicate the edge pixels as
    // Nuke does by default. However this is not done if the bounding box
    // matches the display window, so that blurring an image that fills the
    // frame does not leak black in
    int bx = datawin.min.x;
    int by = datawin.min.y;
    int br = datawin.max.x;
    int bt = datawin.max.y;
    if (bx != dispwin.min.x || br != dispwin.max.x ||
        by != dispwin.min.y || bt != dispwin.max.y) {
      bx--;
      by--;
      br++;
      bt++;
      info_.black_outside(true);
    }
    info_.set(bx - dispwin.min.x,
              dispwin.max.y - bt,
              br - dispwin.min.x + 1,
              dispwin.max.y - by + 1);

    if (inputfile->header().lineOrder() == Imf::INCREASING_Y)
      info_.ydirection(-1);
    else
      info_.ydirection(1);

  }
  catch (const std::exception& exc) {
    iop->error(exc.what());
#ifdef EXR_USE_MMAP
    if (fd != -1) {
      close(fd);
    }
#endif
    delete inputfile;
    return;
  }

  const Imf::Header& header = inputfile->header();

  if (pixelTypes[Imf::FLOAT] > 0) {
    _meta.setData(MetaData::DEPTH, MetaData::DEPTH_FLOAT);
  }
  else if (pixelTypes[Imf::UINT] > 0) {
    _meta.setData(MetaData::DEPTH, MetaData::DEPTH_32);
  }
  if (pixelTypes[Imf::HALF] > 0) {
    _meta.setData(MetaData::DEPTH, MetaData::DEPTH_HALF);
  }

  for (Imf::Header::ConstIterator i = header.begin();
       i != header.end();
       i++) {
    const char* type = i.attribute().typeName();

    std::string key = std::string(MetaData::EXR::EXR_PREFIX) + i.name();

    if (!strcmp(i.name(), "timeCode")) {
      key = MetaData::TIMECODE;
    }

    if (!strcmp(i.name(), "expTime")) {
      key = MetaData::EXPOSURE;
    }

    if (!strcmp(i.name(), "framesPerSecond")) {
      key = MetaData::FRAME_RATE;
    }

    if (!strcmp(i.name(), "keyCode")) {
      key = MetaData::EDGECODE;
    }

    if (!strcmp(i.name(), MetaData::Nuke::NODE_HASH )) {
      key = MetaData::Nuke::NODE_HASH;
    }

    if (!strcmp(type, "string")) {
      const Imf::StringAttribute* attr = static_cast<const Imf::StringAttribute*>(&i.attribute());
      _meta.setData(key, attr->value());
    }
    else if (!strcmp(type, "int")) {
      const Imf::IntAttribute* attr = static_cast<const Imf::IntAttribute*>(&i.attribute());
      _meta.setData(key, attr->value());
    }
    else if (!strcmp(type, "v2i")) {
      const Imf::V2iAttribute* attr = static_cast<const Imf::V2iAttribute*>(&i.attribute());
      int values[2] = {
        attr->value().x, attr->value().y
      };
      _meta.setData(key, values, 2);
    }
    else if (!strcmp(type, "v3i")) {
      const Imf::V3iAttribute* attr = static_cast<const Imf::V3iAttribute*>(&i.attribute());
      int values[3] = {
        attr->value().x, attr->value().y, attr->value().z
      };
      _meta.setData(key, values, 3);
    }
    else if (!strcmp(type, "box2i")) {
      const Imf::Box2iAttribute* attr = static_cast<const Imf::Box2iAttribute*>(&i.attribute());
      int values[4] = {
        attr->value().min.x, attr->value().min.y, attr->value().max.x, attr->value().max.y
      };
      _meta.setData(key, values, 4);
    }
    else if (!strcmp(type, "float")) {
      const Imf::FloatAttribute* attr = static_cast<const Imf::FloatAttribute*>(&i.attribute());
      _meta.setData(key, attr->value());
    }
    else if (!strcmp(type, "v2f")) {
      const Imf::V2fAttribute* attr = static_cast<const Imf::V2fAttribute*>(&i.attribute());
      float values[2] = {
        attr->value().x, attr->value().y
      };
      _meta.setData(key, values, 2);
    }
    else if (!strcmp(type, "v3f")) {
      const Imf::V3fAttribute* attr = static_cast<const Imf::V3fAttribute*>(&i.attribute());
      float values[3] = {
        attr->value().x, attr->value().y, attr->value().z
      };
      _meta.setData(key, values, 3);
    }
    else if (!strcmp(type, "box2f")) {
      const Imf::Box2fAttribute* attr = static_cast<const Imf::Box2fAttribute*>(&i.attribute());
      float values[4] = {
        attr->value().min.x, attr->value().min.y, attr->value().max.x, attr->value().max.y
      };
      _meta.setData(key, values, 4);
    }
    else if (!strcmp(type, "m33f")) {
      const Imf::M33fAttribute* attr = static_cast<const Imf::M33fAttribute*>(&i.attribute());
      std::vector<float> values;
      for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
          values.push_back((attr->value())[i][j]);
        }
      }
      _meta.setData(key, values);
    }
    else if (!strcmp(type, "m44f")) {
      const Imf::M44fAttribute* attr = static_cast<const Imf::M44fAttribute*>(&i.attribute());
      std::vector<float> values;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          values.push_back((attr->value())[i][j]);
        }
      }
      _meta.setData(key, values);
    }
    else if (!strcmp(type, "timecode")) {
      const Imf::TimeCodeAttribute* attr = static_cast<const Imf::TimeCodeAttribute*>(&i.attribute());
      char timecode[20];
      sprintf(timecode, "%02i:%02i:%02i:%02i", attr->value().hours(), attr->value().minutes(), attr->value().seconds(), attr->value().frame());
      _meta.setData(key, timecode);
    }
    else if (!strcmp(type, "keycode")) {
      const Imf::KeyCodeAttribute* attr = static_cast<const Imf::KeyCodeAttribute*>(&i.attribute());
      char keycode[30];
      sprintf(keycode, "%02i %02i %06i %04i %02i",
              attr->value().filmMfcCode(),
              attr->value().filmType(),
              attr->value().prefix(),
              attr->value().count(),
              attr->value().perfOffset());
      _meta.setData(key, keycode);
    }
    else if (!strcmp(type, "rational")) {
      const Imf::RationalAttribute* attr = static_cast<const Imf::RationalAttribute*>(&i.attribute());
      _meta.setData(key, (double)attr->value());
      //    } else {
      //      _meta.setData(key, "type " + std::string(type));
    }
  }

}

exrReader::~exrReader()
{
#ifdef EXR_USE_MMAP
  if (fd != -1) {
    close(fd);
  }
#endif
  delete inputfile;
}

void exrReader::_validate(bool for_real)
{
}

#ifdef EXR_USE_MMAP
bool exrReader::mmap_engine(const Imath::Box2i& datawin,
                            const Imath::Box2i& dispwin,
                            const ChannelSet&   channels,
                            int                 exrY,
                            Row&                row,
                            int                 x,
                            int                 X,
                            int                 r,
                            int                 R)
{
  int exrwd = datawin.max.x - datawin.min.x + 1;
  int exrht = datawin.max.y - datawin.min.y + 1;
  int effwd = exrwd;
  if ((effwd % pagesize) != 0) {
    effwd = ((effwd / pagesize) + 1) * pagesize;
  }

  std::map<std::string, int> fileChans;
  foreach(z, info_.channels()) {
    if (fileChans.find(channel_map[z]) == fileChans.end()) {
      int newNo = fileChans.size();
      fileChans[channel_map[z]] = newNo;
    }
  }

  if (!fileLoaded) {
    Guard guard(C_lock);
    if (!fileLoaded) {

      char* s = getenv("NUKE_EXR_TEMP_DIR");
      if (!s)
        s = getenv("NUKE_TEMP_DIR");

      char fn[1024];
      mkdir(s, 0700);
      sprintf(fn, "%s/exr-temporary-XXXXXX", s);
      mktemp(fn);

      fd = ::open(fn, O_RDWR | O_CREAT, 0700);
      unlink(fn);
      if (fd == -1) {
        return false;
      }

      Imf::FrameBuffer fbuf;

      std::vector<float*> buffers;
      unsigned long long planesize = effwd * exrht * sizeof(float);
      unsigned long long filesize = planesize * info_.channels().size();
      ::ftruncate(fd, filesize);

      unsigned c = 0;

      foreach(z, info_.channels()) {

        if (fileChans.find(channel_map[z]) != fileChans.end()) {
          c = fileChans[channel_map[z]];
        }
        else {
          c = 0;
        }

        float* dest = (float*)mmap(NULL, planesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, planesize * c);

        if (dest == (float*)-1) {
          close(fd);
          return false;
        }

        buffers.push_back(dest);

        fbuf.insert(channel_map[z],
                    Imf::Slice(Imf::FLOAT,
                               (char*)(dest - datawin.min.x - effwd * datawin.min.y),
                               sizeof(float),
                               sizeof(float) * effwd));
      }

      try {
        if (iop->aborted())
          return true;                   // abort if another thread does so
        inputfile->setFrameBuffer(fbuf);
        inputfile->readPixels(datawin.min.y, datawin.max.y);
      }
      catch (const std::exception& exc) {
        iop->error(exc.what());
        return true;
      }
      ;

      c = 0;

      foreach(z, info_.channels()) {
        munmap(buffers[c], effwd * exrht * sizeof(float));
        c++;
      }

      fileLoaded = true;
    }
  }

  foreach(z, info_.channels()) {
    if (channels & z) {
      int source_chan = 0;

      if (fileChans.find(channel_map[z]) != fileChans.end()) {
        source_chan = fileChans[channel_map[z]];
      }
      else {
        continue;
      }

      size_t offset = sizeof(float) * (source_chan * effwd * exrht + (exrY - datawin.min.y) * effwd);
      size_t rowsize = exrwd * sizeof(float);

      struct stat s;
      fstat(fd, &s);

      const float* src = (const float*)mmap(NULL,
                                            rowsize,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            fd,
                                            offset);
      if (src == (float*)-1) {
        iop->error("EXR reader failed.");
        return true;
      }

      float* dest = row.writable(z);
      for (int xx = x; xx < X; xx++)
        dest[xx] = 0;
      for (int xx = R; xx < r; xx++)
        dest[xx] = 0;
      memcpy(dest + X, src + (X - (datawin.min.x - dispwin.min.x)), (R - X) * sizeof(float));

      munmap((void*)src, rowsize);
    }
  }

  return true;
}
#endif

void exrReader::normal_engine(const Imath::Box2i& datawin,
                              const Imath::Box2i& dispwin,
                              const ChannelSet&   channels,
                              int                 exrY,
                              Row&                row,
                              int                 x,
                              int                 X,
                              int                 r,
                              int                 R)
{
  row.range(this->x(), this->r());

  std::map<std::string, Channel> usedChans;
  std::map<Channel, Channel> toCopy;

  Imf::FrameBuffer fbuf;
  foreach (z, channels) {
    if (usedChans.find(channel_map[z]) != usedChans.end()) {
      toCopy[z] = usedChans[channel_map[z]];
      continue;
    }

    usedChans[channel_map[z]] = z;

    float* dest = row.writable(z);
    for (int xx = x; xx < X; xx++)
      dest[xx] = 0;
    for (int xx = R; xx < r; xx++)
      dest[xx] = 0;
    fbuf.insert(channel_map[z],
                Imf::Slice(Imf::FLOAT, (char*)(dest - dispwin.min.x), sizeof(float), 0));
  }

  {
    Guard guard(C_lock);
    try {
      if (iop->aborted())
        return;                     // abort if another thread does so
      inputfile->setFrameBuffer(fbuf);
      inputfile->readPixels(exrY);
    }
    catch (const std::exception& exc) {
      iop->error(exc.what());
      return;
    }
  }

  foreach (z, channels) {
    if (toCopy.find(z) != toCopy.end()) {
      float* dest = row.writable(z);
      const float* src = row[toCopy[z]];

      for (int col = x; col < r; col++) {
        dest[col] = src[col];
      }
    }
  }
}

void exrReader::engine(int y, int x, int r, ChannelMask c1, Row& row)
{

  const Imath::Box2i& dispwin = inputfile->header().displayWindow();
  const Imath::Box2i& datawin = inputfile->header().dataWindow();

  // Invert to EXR y coordinate:
  int exrY = dispwin.max.y - y;

  // Figure out intersection of x,r with the data in exr file:
  const int X = MAX(x, datawin.min.x - dispwin.min.x);
  const int R = MIN(r, datawin.max.x + 1 - dispwin.min.x);

  // Black outside the box:
  if (exrY < datawin.min.y || exrY > datawin.max.y || R <= X) {
    row.erase(c1);
    return;
  }

  ChannelSet channels(c1);
  if (premult() && !lut()->linear() &&
      (channels & Mask_RGB) && (this->channels() & Mask_Alpha))
    channels += (Mask_Alpha);

#ifdef EXR_USE_MMAP
  if (exr_use_mmap) {
    if (!mmap_engine(datawin, dispwin, channels, exrY, row, x, X, r, R)) {
      exr_use_mmap = false;
      exr_mmap_bad = true;
      normal_engine(datawin, dispwin, channels, exrY, row, x, X, r, R);
    }
  }
  else {
#endif
  normal_engine(datawin, dispwin, channels, exrY, row, x, X, r, R);
#ifdef EXR_USE_MMAP
  }
#endif

  // Do colorspace conversion, now that we have the alpha for premultiplied:
  if (!lut()->linear()) {
    const float* alpha = (channels & Mask_Alpha) ? row[Chan_Alpha] + X : 0;
    for (Channel chan = Chan_Red; chan <= Chan_Blue; chan = Channel(chan + 1)) {
      if (intersect(channels, chan)) {
        const float* src = row[chan] + X;
        float* dest = row.writable(chan) + X;
        from_float(chan, dest, src, alpha, R - X);
      }
    }
  }
}

/*! Convert the channel name from the exr file into a nuke name.
   Currently this does:
   - splits the word at each period
   - Deletes all digits at the start and after each period.
   - Changes all non-alphanumeric characters into underscores.
   - ignores empty parts between periods
   - appends all but the last with underscores into a layer name
   - the last word is the channel name.
   - Changes all variations of rgba into "red", "green", "blue", "alpha"
   - Changes layer "Ci" to the main layer
 */
void validchanname(const char* channelname, std::string& chan,
                   std::string& layer)
{

  chan.clear();
  layer.clear();

  for (const char* q = channelname; *q;) {
    std::string word;
    while (isdigit(*q))
      q++;
    while (*q) {
      char c = *q++;
      if (c == '.')
        break;
      if (!isalnum(c))
        c = '_';
      word += c;
    }
    if (word.empty())
      continue;
    if (!chan.empty()) {
      if (layer.empty())
        layer.swap(chan);
      else {
        layer += '_';
        layer += chan;
      }
    }
    chan.swap(word);
  }

  //Ci is the primary layer in prman renders.
  if (layer == "Ci")
    layer.clear();

  if (chan.empty())
    chan = "unnamed";
  else if (chan == "R" || chan == "r" ||
           chan == "Red" || chan == "RED")
    chan = "red";
  else if (chan == "G" || chan == "g" ||
           chan == "Green" || chan == "GREEN")
    chan = "green";
  else if (chan == "B" || chan == "b" ||
           chan == "Blue" || chan == "BLUE")
    chan = "blue";
  else if (chan == "A" || chan == "a" ||
           chan == "Alpha" || chan == "ALPHA")
    chan = "alpha";

  //std::cout << "Turned '"<<channelname<<"' into "<<layer<<"."<<chan<<std::endl;

}

void ChannelName::setname(const char* name)
{
  validchanname(name, chan, layer);
}

const std::string ChannelName::name()
{
  if (!layer.empty())
    return layer + "." + chan;
  return chan;
}
