// exrWriter.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

/* Reads exr files using libexr.
   This is an example of a file reader that is not a subclass of
   FileWriter. Instead this uses the library's reader functions and
   a single lock so that multiple threads do not crash the library.

   04/14/03     Initial Release                Charles Henrich
   12/04/03     User selectable compression,   Charles Henrich
        float precision, and autocrop
   10/04    Defaulted autocrop to off    spitzak
   5/06        black-outside and reformatting    spitzak
 */
#include "DDImage/DDWindows.h"
#include "DDImage/Writer.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/Tile.h"
#include "DDImage/DDString.h"
#include "DDImage/MetaData.h"
#include <errno.h>
#include <stdio.h>

#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfCompression.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfBoxAttribute.h>
#include <OpenEXR/ImfDoubleAttribute.h>
#include <OpenEXR/ImfStringVectorAttribute.h>
#include <OpenEXR/ImfTimeCodeAttribute.h>
#include <OpenEXR/ImfStandardAttributes.h>
#include <OpenEXR/ImfFramesPerSecond.h>
#include <OpenEXR/ImfMatrixAttribute.h>
#include <OpenEXR/ImathBox.h>
#include <OpenEXR/half.h>

using namespace DD::Image;

class exrWriter : public Writer
{
  void autocrop_tile(Tile& img, ChannelMask channels, int* bx, int* by, int* br, int* bt);
  int datatype;
  int compression;
  bool autocrop;
  bool writeHash;
  int hero;
  int _metadataMode;

public:

  exrWriter(Write* iop);
  ~exrWriter();

  void metadataToExrHeader(const MetaData::Bundle& metadata, Imf::Header& exrheader);

  void execute();
  void knobs(Knob_Callback f);
  static const Writer::Description d;

  // Make it default to linear colorspace:
  LUT* defaultLUT() const { return LUT::getLut(LUT::FLOAT); }

  int split_input(int i) const
  {
    return int(executingViews().size() ? executingViews().size() : 1);
  }

  /**
   * return the view which we are expecting on input N
   */
  int view(int n) const
  {
    std::set<int> views = executingViews();

    std::set<int>::const_iterator i = views.begin();
    while (i != views.end()) {
      if (!n) {
        return *i;
      }
      n--;
      i++;
    }

    return 0;
  }

  const OutputContext& inputContext(int i, OutputContext& o) const
  {
    o = iop->outputContext();
    o.view(view(i));
    return o;
  }

  const char* help() { return "OpenEXR high dynamic range format from ILM"; }

};

static Writer* build(Write* iop)
{
  return new exrWriter(iop);
}

const Writer::Description exrWriter::d("exr\0sxr\0", build);

exrWriter::exrWriter(Write* iop) : Writer(iop)
{
  datatype = 0;
  compression = 1;
  autocrop = false;
  writeHash = true;
  hero = 1;
  _metadataMode = 1;
}

static const Imf::Compression ctypes[6] = {
  Imf::NO_COMPRESSION,
  Imf::ZIPS_COMPRESSION,
  Imf::ZIP_COMPRESSION,
  Imf::PIZ_COMPRESSION,
  Imf::RLE_COMPRESSION,
  Imf::B44_COMPRESSION
};

static const char* const cnames[] = {
  "none",
  "Zip (1 scanline)",
  "Zip (16 scanlines)",
  "PIZ Wavelet (32 scanlines)",
  "RLE",
  "B44",
  NULL
};

static const char* const dnames[] = {
  "16 bit half", "32 bit float", NULL
};

static const char* const metadata_modes[] = {
  "no metadata",
  "default metadata",
  "default metadata and exr/*",
  "all metadata except input/*",
  "all metadata",
  NULL
};

exrWriter::~exrWriter()
{
}

class RowGroup
{
  std::vector<Row*> row;

public:
  RowGroup(size_t n, int x, int r)
  {
    row.resize(n);
    for (size_t i = 0; i < n; i++) {
      row[i] = new Row(x, r);
    }
  }

  ~RowGroup()
  {
    for (size_t i = 0; i < row.size(); i++) {
      delete row[i];
    }
  }

  Row& operator[](int i)
  {
    if (i >= int(row.size()))
      abort();
    return *row[i];
  }

  const Row& operator[](int i) const
  {
    if (i >= int(row.size()))
      abort();
    return *row[i];
  }
};

bool timeCodeFromString(const std::string& str, Imf::TimeCode& attr, Write* iop)
{
  if (str.length() != 11)
    return false;

  int hours = 0, mins = 0, secs = 0, frames = 0;

  sscanf(str.c_str(), "%02d:%02d:%02d:%02d", &hours, &mins, &secs, &frames);

  try {
    // if some thing is out of range an exception is throw
    // in this case just report a warning on console
    Imf::TimeCode a;
    a.setHours(hours);
    a.setMinutes(mins);
    a.setSeconds(secs);
    a.setFrame(frames);
    attr = a;
  }
  catch (const std::exception& exc) {
    iop->warning("EXR: Time Code Metadata warning [%s]\n", exc.what());
    return false;
  }
  return true;
}

bool edgeCodeFromString(const std::string& str, Imf::KeyCode& attr, Write* iop)
{
  int mfcCode, filmType, prefix, count, perfOffset;
  sscanf(str.c_str(), "%d %d %d %d %d", &mfcCode, &filmType, &prefix, &count, &perfOffset);

  try {
    // if some thing is out of range an exception is throw
    // in this case just report a warning on console
    Imf::KeyCode a;
    a.setFilmMfcCode(mfcCode);
    a.setFilmType(filmType);
    a.setPrefix(prefix);
    a.setCount(count);
    a.setPerfOffset(perfOffset);

    attr = a;
  }
  catch (const std::exception& exc) {
    iop->warning("EXR: Edge Code Metadata warning [%s]\n", exc.what());
    return false;
  }

  return true;
}


void exrWriter::metadataToExrHeader(const MetaData::Bundle& metadata, Imf::Header& exrheader)
{
  if (_metadataMode != 0) {
    // NB: if specific things are added to this list the tooltip for the "metadata" knob needs
    // updating

    std::string timeCodeStr = metadata.getString(MetaData::TIMECODE);
    if (!timeCodeStr.empty()) {
      Imf::TimeCode attr;
      if (timeCodeFromString(timeCodeStr, attr, iop)) {
        Imf::addTimeCode(exrheader, attr);
      }
    }

    std::string edgeCodeStr = metadata.getString(MetaData::EDGECODE);
    if (!edgeCodeStr.empty()) {
      Imf::KeyCode attr;
      if (edgeCodeFromString(edgeCodeStr, attr, iop)) {
        Imf::addKeyCode(exrheader, attr);
      }
    }

    double frameRate = metadata.getDouble(MetaData::FRAME_RATE);
    if (frameRate != 0) {
      Imf::Rational fps = Imf::guessExactFps(frameRate);
      Imf::addFramesPerSecond(exrheader, fps);
    }

    double exposure = metadata.getDouble(MetaData::EXPOSURE);
    if (exposure != 0) {
      Imf::addExpTime(exrheader, (float)exposure);
    }

    if ( writeHash ) {
      Hash inputHash = iop->getHashOfInputs();

      std::ostringstream hashString;
      hashString << std::hex << inputHash.value();

      Imf::StringAttribute hashAttr;
      hashAttr.value() = hashString.str();
      exrheader.insert(MetaData::Nuke::NODE_HASH, hashAttr);
    }
  }

  if (_metadataMode) {
    for (MetaData::Bundle::const_iterator it = metadata.begin();
         it != metadata.end();
         it++) {

      std::string exrPropName = "";

      if (it->first.substr(0, strlen(MetaData::EXR::EXR_PREFIX)) == MetaData::EXR::EXR_PREFIX && _metadataMode >= 2) {
        exrPropName = it->first.substr(strlen(MetaData::EXR::EXR_PREFIX));
      }
      else if (it->first.substr(0, strlen(MetaData::INPUT_PREFIX)) != MetaData::INPUT_PREFIX && _metadataMode >= 3) {
        exrPropName = MetaData::Nuke::NUKE_PREFIX + it->first;
      }
      else if (_metadataMode >= 4) {
        exrPropName = MetaData::Nuke::NUKE_PREFIX + it->first;
      }

      Imf::Attribute* attr = 0;

      const MetaData::Bundle::PropertyPtr prop = it->second;
      size_t psize = MetaData::getPropertySize(prop);

      if (!exrPropName.empty()) {
        if ( MetaData::isPropertyDouble(prop) ) {
          if (psize == 1) {
            attr = new Imf::FloatAttribute( (float)MetaData::getPropertyDouble(prop, 0) );
          }
          else if (psize == 2) {
            attr = new Imf::V2fAttribute(Imath::V2f( (float)MetaData::getPropertyDouble(prop, 0),
                                                     (float)MetaData::getPropertyDouble(prop, 1) ));
          }
          else if (psize == 3) {
            attr = new Imf::V3fAttribute(Imath::V3f( (float)MetaData::getPropertyDouble(prop, 0),
                                                     (float)MetaData::getPropertyDouble(prop, 1),
                                                     (float)MetaData::getPropertyDouble(prop, 2) ));
          }
          else if (psize == 4) {
            attr = new Imf::Box2fAttribute(Imath::Box2f( Imath::V2f((float)MetaData::getPropertyDouble(prop, 0), (float)MetaData::getPropertyDouble(prop, 1)),
                                                         Imath::V2f((float)MetaData::getPropertyDouble(prop, 2), (float)MetaData::getPropertyDouble(prop, 3) )));
          }
          else if (psize == 9) {
            float val[3][3];
            for (size_t i = 0; i < psize; i++) {
              val[i / 3][i % 3] = (float)MetaData::getPropertyDouble(prop, i);
            }
            attr = new Imf::M33fAttribute(Imath::M33f(val));
          }
          else if (psize == 16) {
            float val[4][4];
            for (size_t i = 0; i < psize; i++) {
              val[i / 4][i % 4] = (float)MetaData::getPropertyDouble(prop, i);
            }
            attr = new Imf::M44fAttribute(Imath::M44f(val));
          }
        }
        else if (MetaData::isPropertyInt( prop )) {
          if (psize == 1) {
            attr = new Imf::IntAttribute(MetaData::getPropertyInt(prop, 0));
          }
          else if (psize == 2) {
            attr = new Imf::V2iAttribute( Imath::V2i(MetaData::getPropertyInt(prop, 0), MetaData::getPropertyInt(prop, 1)) );
          }
          else if (psize == 3) {
            attr = new Imf::V3iAttribute( Imath::V3i(MetaData::getPropertyInt(prop, 0), MetaData::getPropertyInt(prop, 1), MetaData::getPropertyInt(prop, 2)));
          }
          else if (psize == 4) {
            attr = new Imf::Box2iAttribute( Imath::Box2i(Imath::V2i(MetaData::getPropertyInt(prop, 0), MetaData::getPropertyInt(prop, 1)),
                                                         Imath::V2i(MetaData::getPropertyInt(prop, 2), MetaData::getPropertyInt(prop, 3)))) ;
          }
        }
        else if ( MetaData::isPropertyString(prop) ) {
          if (psize == 1) {
            attr = new Imf::StringAttribute( MetaData::getPropertyString(prop, 0) );
          }
        }
      }

      if (attr && exrheader.find(exrPropName) == exrheader.end()) {
        exrheader.insert(exrPropName, *attr);
      }

      delete attr;
    }
  }
}


void exrWriter::execute()
{
  int floatdepth = datatype ? 32 : 16;

  Imf::Compression compression = ctypes[this->compression];

  ChannelSet channels(input0().channels());
  channels &= (iop->channels());
  if (!channels) {
    iop->error("exrWriter: No channels selected (or available) for write\n");
    return;
  }
  if (premult() && !lut()->linear() &&
      (channels & Mask_RGB) && (input0().channels() & Mask_Alpha))
    channels += (Mask_Alpha);

  std::vector<int> views;
  std::vector<std::string> viewstr;

  viewstr.push_back(OutputContext::viewname(hero));

  std::set<int> execViews = executingViews();
  std::set<int> wantViews = iop->executable()->viewsToExecute();

  if (wantViews.size() == 0) {
    wantViews = execViews;
  }

  if (wantViews.size() == 1) {
    hero = *wantViews.begin();
  }

  for (std::set<int>::const_iterator i = execViews.begin(); i != execViews.end(); i++) {
    views.push_back(*i);
    if (*i != hero) {
      viewstr.push_back(OutputContext::viewname(*i));
    }
  }

  DD::Image::Box bound;

  // Multi-thread flag is off because the Write operator has already
  // launched multiple threads to fill in the lines. This would just
  // create extra ones.

  bool sizewarn = false;

  bool firstInputBbox = true;

  for (int i = 0; i < iop->inputs(); i++) {

    if (wantViews.find(view(i)) == wantViews.end())
      continue;

    Iop* input = iop->input(i);
    input->validate(true);

    int bx = input->x();
    int by = input->y();
    int br = input->r();
    int bt = input->t();
    if (input->black_outside()) {
      if (bx + 2 < br) {
        bx++;
        br--;
      }
      if (by + 2 < bt) {
        by++;
        bt--;
      }
    }

    input->request(bx, by, br, bt, channels, 1);

    if (br - bx > input0().format().width() * 1.5 ||
        bt - by > input0().format().height() * 1.5) {
      // print this warning before it possibly crashed due to requesting a
      // huge buffer!
      if (sizewarn) {
        fprintf(stderr, "!WARNING! Bounding Box Area is > 1.5 times larger "
                        "than format. You may want crop your image before writing it.\n");
        sizewarn = true;
      }
    }

    if (autocrop) {

      Tile img(*input, input->x(), input->y(),
               input->r(), input->t(), channels, false);

      if (iop->aborted()) {
        //iop->error("exrWriter: Write failed [Unable to get input tile]\n");
        return;
      }

      autocrop_tile(img, channels, &bx, &by, &br, &bt);
      bt++; /* We (aka nuke) want r & t to be beyond the last pixel */
      br++;
    }

    if (firstInputBbox) {
      bound.y(by);
      bound.x(bx);
      bound.r(br);
      bound.t(bt);
    }
    else {
      bound.y(std::min(bound.y(), by));
      bound.x(std::min(bound.x(), bx));
      bound.r(std::max(bound.r(), br));
      bound.t(std::max(bound.t(), bt));
    }

    firstInputBbox = false;
  }

  Imath::Box2i C_datawin;
  C_datawin.min.x = bound.x();
  C_datawin.min.y = input0().format().height() - bound.t();
  C_datawin.max.x = bound.r() - 1;
  C_datawin.max.y = input0().format().height() - bound.y() - 1;

  Imath::Box2i C_dispwin;
  C_dispwin.min.x = 0;
  C_dispwin.min.y = 0;
  C_dispwin.max.x = input0().format().width() - 1;
  C_dispwin.max.y = input0().format().height() - 1;

  try {
    int numchannels = channels.size();
    Imf::OutputFile* outfile;
    Imf::FrameBuffer fbuf;

    RowGroup renderrow(executingViews().size(), bound.x(), bound.r());
    RowGroup writerow(executingViews().size(), bound.x(), bound.r());

    Imf::Header exrheader(C_dispwin, C_datawin, iop->format().pixel_aspect(),
                          Imath::V2f(0, 0), 1, Imf::INCREASING_Y, compression);

    if (wantViews.size() > 1) {
      // only write multi view string if a stereo file
      Imf::StringVectorAttribute multiViewAttr;

      multiViewAttr.value() = viewstr;
      exrheader.insert("multiView", multiViewAttr);
    }

    Iop* metaInput = NULL;
    for (size_t viewIdx = 0; viewIdx < views.size(); viewIdx ++) {
      if (wantViews.find(views[viewIdx]) == wantViews.end()) {
        continue;
      }
      if (metaInput == NULL || views[viewIdx] == hero) {
        metaInput = iop->input(viewIdx);
      }
    }
    if (metaInput == NULL) {
      metaInput = iop->input(0);
    }

    const MetaData::Bundle& metadata = metaInput->fetchMetaData(NULL);

    metadataToExrHeader(metadata, exrheader);

    Imf::Array2D<half> halfwriterow(numchannels * views.size(), bound.r() - bound.x());

    std::map<int, ChannelSet> channelsperview;

    for (int v = 0; v < int(views.size()); v++) {

      if (wantViews.find(views[v]) == wantViews.end()) {
        continue;
      }

      int curchan = 0;
      foreach(z, channels) {
        std::string channame;

        switch (z) {
          case Chan_Red: channame = "R";
            break;
          case Chan_Green: channame = "G";
            break;
          case Chan_Blue: channame = "B";
            break;
          case Chan_Alpha: channame = "A";
            break;
          default: channame = iop->channel_name(z);
            break;
        }

        if (executingViews().size() > 1 && views[v] != hero) {
          channame = OutputContext::viewname(views[v]) + "." + channame;

          if (z == Chan_Stereo_Disp_Left_X ||
              z == Chan_Stereo_Disp_Left_Y ||
              z == Chan_Stereo_Disp_Right_X ||
              z == Chan_Stereo_Disp_Right_Y) {
            continue;
          }
        }

        channelsperview[v].insert(z);

        if (floatdepth == 32) {
          exrheader.channels().insert(channame.c_str(), Imf::Channel(Imf::FLOAT));
        }
        else {
          exrheader.channels().insert(channame.c_str(), Imf::Channel(Imf::HALF));
        }

        writerow[v].writable(z);

        if (floatdepth == 32) {
          fbuf.insert(channame.c_str(),
                      Imf::Slice(Imf::FLOAT, (char*)(float*)writerow[v][z],
                                 sizeof(float), 0));
        }
        else {
          fbuf.insert(channame.c_str(),
                      Imf::Slice(Imf::HALF,
                                 (char*)(&halfwriterow[v * numchannels + curchan][0] - C_datawin.min.x),
                                 sizeof(halfwriterow[v * numchannels * curchan][0]), 0));
          curchan++;
        }
      }
    }

    char temp_name[1024];
    strlcpy(temp_name, filename(), 1024 - 5);
    strcat(temp_name, ".tmp");
    outfile = new Imf::OutputFile(temp_name, exrheader);
    outfile->setFrameBuffer(fbuf);

    for (int scanline = bound.t() - 1;
         scanline >= bound.y();
         scanline--) {

      for (int v = 0; v < int(views.size()); v++) {

        channels = channelsperview[v];

        if (wantViews.find(views[v]) == wantViews.end()) {
          continue;
        }

        writerow[v].pre_copy(renderrow[v], channels);

        iop->inputnget(v, scanline, bound.x(), bound.r(), channels, renderrow[v]);
        if (iop->aborted())
          break;

        int curchan = 0;

        foreach(z, channels) {

          const float* from = renderrow[v][z];
          const float* alpha = renderrow[v][Chan_Alpha];
          float* to = writerow[v].writable(z);

          if (!lut()->linear() && z <= Chan_Blue) {
            to_float(z - 1, to + C_datawin.min.x,
                     from + C_datawin.min.x,
                     alpha + C_datawin.min.x,
                     C_datawin.max.x - C_datawin.min.x + 1);
            from = to;
          }

          if (bound.r() > iop->input(v)->r()) {
            float* end = renderrow[v].writable(z)   + bound.r();
            float* start = renderrow[v].writable(z) + iop->input(v)->r();
            while (start < end) {
              *start = 0;
              start++;
            }
          }

          if (bound.x() < iop->input(v)->x()) {
            float* end = renderrow[v].writable(z)   + bound.x();
            float* start = renderrow[v].writable(z) + iop->input(v)->x();
            while (start > end) {
              *start = 0;
              start--;
            }
          }

          if (floatdepth == 32) {
            if (to != from)
              for (int count = C_datawin.min.x; count < C_datawin.max.x + 1; count++)
                to[count] = from[count];
          }
          else {
            for (int count = C_datawin.min.x; count < C_datawin.max.x + 1; count++)
              halfwriterow[v * numchannels + curchan][count - C_datawin.min.x] = from[count];
            curchan++;
          }
        }

        progressFraction(double(bound.t() - scanline) / (bound.t() - bound.y()));
      }
      outfile->writePixels(1);
    }

    delete outfile;
#ifdef _WIN32
    remove(filename());  // Stupid windoze fails if the destination already exists!
#endif
    if (rename(temp_name, filename()))
      iop->error("Can't rename .tmp to final, %s", strerror(errno));

  }
  catch (const std::exception& exc) {
    iop->error("EXR: Write failed [%s]\n", exc.what());
    return;
  }
}

void exrWriter::knobs(Knob_Callback f)
{
  Bool_knob(f, &autocrop, "autocrop");
  Tooltip(f, "Reduce the bounding box to the non-zero area. This is normally "
             "not needed as the zeros will compress very small, and it is slow "
             "as the whole image must be calculated before any can be written. "
             "However this may speed up some programs reading the files.");

  Bool_knob(f, &writeHash, "write_hash", "write hash");
  SetFlags(f, Knob::INVISIBLE);
  Tooltip(f, "Write the hash of the node graph into the exr file.  Useful to see if your image is up to date when doing a precomp.");

  Enumeration_knob(f, &datatype, dnames, "datatype");
  Enumeration_knob(f, &compression, cnames, "compression");

  Obsolete_knob(f, "stereo", 0);

  OneView_knob(f, &hero, "heroview");
  Tooltip(f, "If stereo is on, this is the view that is written as the \"main\" image");

  Enumeration_knob(f, &_metadataMode, metadata_modes, "metadata");
  Tooltip(f, "Which metadata to write out to the EXR file."
             "<p>'no metadata' means that no custom attributes will be created and only metadata that fills required header fields will be written.<p>'default metadata' means that the optional timecode, edgecode, frame rate and exposure header fields will also be filled using metadata values.");
}

void exrWriter::autocrop_tile(Tile& img, ChannelMask channels,
                              int* bx, int* by, int* br, int* bt)
{
  int xcount, ycount;

  *bx = img.r();
  *by = img.t();
  *br = img.x();
  *bt = img.y();

  foreach (z, channels) {
    for (ycount = img.y(); ycount < img.t(); ycount++) {
      for (xcount = img.x(); xcount < img.r(); xcount++) {
        if (img[z][ycount][xcount] != 0) {
          if (xcount < *bx)
            *bx = xcount;
          if (ycount < *by)
            *by = ycount;
          break;
        }
      }
    }

    for (ycount = img.t() - 1; ycount >= img.y(); ycount--) {
      for (xcount = img.r() - 1; xcount >= img.x(); xcount--) {
        if (img[z][ycount][xcount] != 0) {
          if (xcount > *br)
            *br = xcount;
          if (ycount > *bt)
            *bt = ycount;
          break;
        }
      }
    }
  }

  if (*bx > *br || *by > *bt)
    *bx = *by = *br = *bt = 0;
}
