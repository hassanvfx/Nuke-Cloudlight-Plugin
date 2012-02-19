// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "Build/fnBuild.h"
#include "DDImage/DDString.h"
#include "DDImage/Reader.h"
#include "DDImage/Row.h"

#ifdef _WIN32
  #include <io.h>
#endif

extern "C" {
#include <errno.h>
#include <avformat.h>
#include <avcodec.h>
}

#if __WORDSIZE == 64
  #define INT64_C(c) c ## L
#else
  #define INT64_C(c) c ## LL
#endif

using namespace DD::Image;

namespace
{
  const char* ffmpegError(int error)
  {
    switch (error) {
      case AVERROR_IO:
        return "I/O error";
      case AVERROR_NUMEXPECTED:
        return "number syntax expected in filename";
      case AVERROR_INVALIDDATA:
        return "invalid data found";
      case AVERROR_NOMEM:
        return "not enough memory";
      case AVERROR_NOFMT:
        return "unknown format";
      case AVERROR_NOTSUPP:
        return "operation not supported";
      case AVERROR_NOENT:
        return "no such file or directory";
      case AVERROR_PATCHWELCOME:
      default:
        return "unknown error";
    }
  }
}

class ffmpegReader : public Reader
{
public:
  explicit ffmpegReader(Read* iop);
  ~ffmpegReader();

  virtual bool videosequence() const { return true; }

  void engine(int y, int x, int r, ChannelMask channels, Row& row);
  void open();

private:
  MetaData::Bundle meta;
  static const Reader::Description d;

public:
  const MetaData::Bundle& fetchMetaData(const char* key) { return meta; }

private:
  bool findStreamInfo();
  bool hasVideo() const { return !videoIdx_.empty(); }
  AVStream* getVideoStream() { return currVideoIdx_ >= 0 ? context_->streams[currVideoIdx_] : NULL; }
  double fps() const;
  void openVideoCodec();
  void closeVideoCodec();
  int64_t getTimeStamp(int pos) const;
  bool seek(int pos);
  bool decodeImage();
  int width() const { return width_; }
  int height() const { return height_; }

private:
  AVFormatContext* context_;
  AVInputFormat* format_;
  AVFormatParameters* params_;
  AVFrame* avFrame_;
  AVCodec* videoCodec_;
  AVPacket pkt_;
  AVCodecContext* avctxOptions[CODEC_TYPE_NB];
  AVFormatContext* avformatOptions;
  std::vector<int> videoIdx_;
  int fpsNum_;
  int fpsDen_;
  int currVideoIdx_;
  uint64_t frames_;
  int width_;
  int height_;
  double aspect_;
  std::vector<unsigned char> data_;
  bool offsetTime_;
  int lastSearchPos_;
  int lastDecodedPos_;
};

ffmpegReader::ffmpegReader(Read* iop)
  : Reader(iop)
  , context_(0)
  , format_(0)
  , params_(0)
  , avFrame_(0)
  , videoCodec_(0)
  , avformatOptions(0)
  , fpsNum_(0)
  , fpsDen_(0)
  , currVideoIdx_(-1)
  , frames_(0)
  , width_(720)
  , height_(576)
  , aspect_(1.0f)
  , offsetTime_(true)
  , lastSearchPos_(-1)
  , lastDecodedPos_(-1)
{
  av_log_set_level(AV_LOG_WARNING);
  av_register_all();

  for (int i = 0; i < CODEC_TYPE_NB; ++i)
    avctxOptions[i] = avcodec_alloc_context2(CodecType(i));
  avformatOptions = av_alloc_format_context();
  avFrame_ = avcodec_alloc_frame();

  // FIXME_GC: shouldn't the plugin be passed the filename without the prefix?
  int offset = 0;
  std::string filename(iop->filename());
  if (filename.find("ffmpeg:") != std::string::npos)
    offset = 7;
  int error = av_open_input_file(&context_, iop->filename() + offset, format_, 0, params_);
  if (error < 0) {
    iop->error(ffmpegError(error));
  }
  else {
    // FIXME_GC: needs to know if it's streamable.
    error = av_find_stream_info(context_);
    if (error < 0) {
      iop->error(ffmpegError(error));
    }
    else {
      if (findStreamInfo()) {
        AVCodecContext* codecContext = getVideoStream()->codec;
        if (getVideoStream()->sample_aspect_ratio.num)
          aspect_ = av_q2d(getVideoStream()->sample_aspect_ratio);
        else if (codecContext->sample_aspect_ratio.num)
          aspect_ = av_q2d(codecContext->sample_aspect_ratio);

        info_.channels(Mask_RGBA);
        set_info(width_, height_, 3, aspect_);
        info_.first_frame(1);
        info_.last_frame(frames_);

        data_.resize(width() * height() * 3);

        // hack so seeking works from our intended position.
        if (!strcmp(codecContext->codec->name, "mjpeg") || !strcmp(codecContext->codec->name, "dvvideo"))
          offsetTime_ = false;

        meta.setData(MetaData::CREATOR, context_->author);
        meta.setData(MetaData::COPYRIGHT, context_->copyright);
        meta.setData(MetaData::COMMENT, context_->comment);
        meta.setData(MetaData::PROJECT, context_->album);
        meta.setData(MetaData::FILE_CREATION_TIME, double(context_->timestamp));
        meta.setData("ffmpeg/num_streams", context_->nb_streams);
        meta.setData(MetaData::FRAME_RATE, fps());
        meta.setData("ffmpeg/codec/codecName", codecContext->codec->name);
      }
      else {
        iop->error("unable to find codec");
      }
    }
  }
}

ffmpegReader::~ffmpegReader()
{
  closeVideoCodec();
  if (context_)
    av_close_input_file(context_);
  av_free(avFrame_);
  for (int i = 0; i < CODEC_TYPE_NB; ++i)
    av_free(avctxOptions[i]);
  av_free(avformatOptions);
}

void ffmpegReader::engine(int y, int x, int rx, ChannelMask channels, Row& out)
{
  foreach ( z, channels ) {
    float* TO = out.writable(z) + x;
    unsigned char* FROM = &data_[0];
    FROM += (height() - y - 1) * width() * 3;
    FROM += x * 3;
    from_byte(z, TO, FROM + z - 1, NULL, rx - x, 3);
  }
}

void ffmpegReader::open()
{
  if (lastDecodedPos_ + 1 != frame()) {
    seek(0);
    seek(frame());
  }

  av_init_packet(&pkt_);

  bool hasPicture = false;
  int error = 0;
  while (error >= 0 && !hasPicture) {
    error = av_read_frame(context_, &pkt_);
    if (error < 0)
      break;

    if (error >= 0 && videoIdx_.size() && currVideoIdx_ != -1 && pkt_.stream_index == videoIdx_[currVideoIdx_])
      hasPicture = decodeImage();
    av_free_packet(&pkt_);
  }
}

bool ffmpegReader::findStreamInfo()
{
  for (int i = 0; i < static_cast<int>(context_->nb_streams); ++i) {
    AVCodecContext* codecContext = context_->streams[i]->codec;
    if (avcodec_find_decoder(codecContext->codec_id) == NULL)
      continue;

    switch (codecContext->codec_type) {
      case CODEC_TYPE_VIDEO:
        videoIdx_.push_back(i);
        if (currVideoIdx_ < 0)
          currVideoIdx_ = 0;
        width_ = codecContext->width;
        height_ = codecContext->height;
        break;

      // ignore all audio streams
      case CODEC_TYPE_AUDIO:
      default:
        break;
    }
  }

  if (!hasVideo())
    return false;

  AVStream* stream = getVideoStream();
  if (stream->r_frame_rate.num != 0 && stream->r_frame_rate.den != 0) {
    fpsNum_ = stream->r_frame_rate.num;
    fpsDen_ = stream->r_frame_rate.den;
  }

  openVideoCodec();

  // Set the duration
  if ((uint64_t)context_->duration != AV_NOPTS_VALUE)
    frames_ = uint64_t((fps() * (double)context_->duration / (double)AV_TIME_BASE));
  else
    frames_ = 1 << 29;

  // try to calculate the number of frames
  if (!frames_) {
    seek(0);
    av_init_packet(&pkt_);
    av_read_frame(context_, &pkt_);
    uint64_t firstPts = pkt_.pts;
    uint64_t maxPts = firstPts;
    seek(1 << 29);
    av_init_packet(&pkt_);
    while (stream && av_read_frame(context_, &pkt_) >= 0) {
      uint64_t currPts = av_q2d(getVideoStream()->time_base) * (pkt_.pts - firstPts) * fps();
      if (currPts > maxPts)
        maxPts = currPts;
    }

    frames_ = maxPts;
  }

  return true;
}

double ffmpegReader::fps() const
{
  if (fpsDen_)
    return fpsNum_ / (double)fpsDen_;
  return 1.0f;
}

void ffmpegReader::openVideoCodec()
{
  AVStream* stream = getVideoStream();
  AVCodecContext* codecContext = stream->codec;
  videoCodec_ = avcodec_find_decoder(codecContext->codec_id);
  if (videoCodec_ == NULL || avcodec_open(codecContext, videoCodec_) < 0)
    currVideoIdx_ = -1;
}

void ffmpegReader::closeVideoCodec()
{
  AVStream* stream = getVideoStream();
  if (stream && stream->codec)
    avcodec_close(stream->codec);
}

int64_t ffmpegReader::getTimeStamp(int pos) const
{
  int64_t timestamp = (int64_t)(((double) pos / fps()) * AV_TIME_BASE);
  if ((uint64_t) context_->start_time != AV_NOPTS_VALUE)
    timestamp += context_->start_time;
  return timestamp;
}

bool ffmpegReader::seek(int pos)
{
  int64_t offset = getTimeStamp(pos);
  if (offsetTime_) {
    offset -= AV_TIME_BASE;
    if (offset < context_->start_time)
      offset = 0;
  }

  avcodec_flush_buffers(getVideoStream()->codec);
  if (av_seek_frame(context_, -1, offset, AVSEEK_FLAG_BACKWARD) < 0)
    return false;

  return true;
}

bool ffmpegReader::decodeImage()
{
  // search for our picture.
  double pts = 0;
  if ((uint64_t) pkt_.dts != AV_NOPTS_VALUE)
    pts = av_q2d(getVideoStream()->time_base) * pkt_.dts;

  int curPos = int(pts * fps() + 0.5f);
  if (curPos == lastSearchPos_)
    curPos = lastSearchPos_ + 1;
  lastSearchPos_ = curPos;

  if ((uint64_t)context_->start_time != AV_NOPTS_VALUE)
    curPos -= int(context_->start_time * fps() / AV_TIME_BASE);

  int hasPicture = 0;
  int curSearch = 0;
  AVCodecContext* codecContext = getVideoStream()->codec;
  if (curPos >= frame())
    avcodec_decode_video(codecContext, avFrame_, &hasPicture, pkt_.data, pkt_.size);
  else if (offsetTime_)
    avcodec_decode_video(codecContext, avFrame_, &curSearch, pkt_.data, pkt_.size);
  if (!hasPicture)
    return false;

  lastDecodedPos_ = lastSearchPos_;

  AVPicture output;
  avpicture_fill(&output, &data_[0], PIX_FMT_RGB24, width_, height_);
  img_convert(&output, PIX_FMT_RGB24, (AVPicture*) avFrame_, codecContext->pix_fmt, width_, height_);

  return true;
}

static Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
  ::close(fd);
  return new ffmpegReader(iop);
}

static bool test(int fd, const unsigned char* block, int length)
{
  return true;
}

const Reader::Description ffmpegReader::d("ffmpeg\0", build, test);
