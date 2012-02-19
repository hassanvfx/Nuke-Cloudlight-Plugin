// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include <fstream>
#include <sstream>

#include "DDImage/Knobs.h"
#include "DDImage/DDMath.h"
#include "DDImage/ViewerContext.h"
#include "DDImage/Iop.h"
#include "DDImage/PixelIop.h"
#include "DDImage/Row.h"

using namespace DD::Image;

////////////////////////////////////////////////////////////////
/// GPU File Shader Op.

class GPUFileShader : public DD::Image::PixelIop
{

  const char* shaderFile_;
  std::string currShaderFile_;
  std::string shader_;
  int version_;
  int currVersion_;

  const char* gpuEngine_body() const
  {
    return shader_.c_str();
  }

  void pixel_engine(const Row& in, int y, int x, int r, ChannelMask channels, Row& out)
  {
    foreach (z, channels) {
      const float* inptr = in[z] + x;
      const float* END = inptr + (r - x);
      float* outptr = out.writable(z) + x;
      while (inptr < END)
        *outptr++ = *inptr++;
    }
  }

  void in_channels(int, DD::Image::ChannelSet& c) const { } // return c unchanged

public:
  GPUFileShader(Node* node)
    : PixelIop(node)
    , shaderFile_(0)
    , version_(0)
    , currVersion_(0)
  { }

  void knobs(Knob_Callback f)
  {
    File_knob(f, &shaderFile_, "shader_file", "OpenGL Shading Language file");
  }

  void _validate(bool)
  {
    if (!shaderFile_)
      return;

    if (version_ != currVersion_ || currShaderFile_ != std::string(shaderFile_)) {
      std::ifstream ifs(shaderFile_);
      if (!ifs) {
        Iop::error("Error reading shader file.");
        return;
      }
      std::stringstream str;
      std::copy(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>(), std::ostreambuf_iterator<char>(str));
      shader_ = str.str();
      currVersion_ = version_;
      currShaderFile_.assign(shaderFile_);
    }

    copy_info(0);
  }

  static const DD::Image::Op::Description d;
  const char* Class() const { return d.name; }
  const char* node_help() const { return "GPU Op which gets initialised from a file. Customise for proprietary formats. Default assumes OpenGL shading language code."; }

};

static Op* GPUFileShader_c(Node* node) { return new GPUFileShader(node); }
const Op::Description GPUFileShader::d("GPUFileShader", GPUFileShader_c);
