// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "Socket";
static const char* const HELP =
  "Test of continuosly-changing image, such as would be produced by reading a Socket. In this case it just draws a line that moves every 2 seconds.";

#include "DDImage/PixelIop.h"
#include "DDImage/Row.h"
#include "DDImage/Thread.h"
#include "DDImage/Knobs.h"
#include "DDImage/DDMath.h"
#ifndef WIN32
#include <unistd.h>
#endif

using namespace DD::Image;

static void sleeper(unsigned, unsigned, void* d);

class Test : public PixelIop
{
public:

  unsigned position;
  double speed;
  bool killthread;
  void in_channels(int, ChannelSet& x) const {}

  Test(Node* node) : PixelIop(node)
  {
    position = 0;
    speed = 1.0;
    killthread = false;
    Thread::spawn(::sleeper, 1, this);
  }

  // Destroying the Op should get rid of the parallel threads.
  // Unfortunatly currently Nuke does not destroy one of the Ops on a
  // deleted node, as it is saving it for Undo. This bug will be fixed
  // in an upcoming version, so you should implement this:
  ~Test()
  {
    killthread = true;
    Thread::wait(this);
  }

  void increment()
  {
    position++;
    asapUpdate();
  }

  // The hash value must change or Nuke will think the picture is the
  // same. If you can't determine some id for the picture, you should
  // use the current time or something.
  void append(Hash& hash)
  {
    hash.append(position);
  }

  void pixel_engine(const Row& in, int y, int x, int r, ChannelMask c, Row& out)
  {
    double ang = (this->position % 360) * M_PI / 180;
    double yy = y - info().format().center_y();
    double xx = info().format().center_x();
    double x1 = yy ? xx + cos(ang) * yy / sin(ang) : 0;
    ang += M_PI / 8;
    double x2 = yy ? xx + cos(ang) * yy / sin(ang) : 0;
    if (x1 > x2) { double t = x1;
                   x1 = x2;
                   x2 = t; }
    if (x1 < x)
      x1 = x;
    if (x1 > r)
      x1 = r;
    if (x2 < x)
      x2 = x;
    if (x2 > r)
      x2 = r;
    foreach (z, c) {
      const float* src = in[z];
      float* dst = out.writable(z);
      for (int X = x; X < r; X++)
        if (X >= x1 && X < x2)
          dst[X] = 1;
        else
          dst[X] = src[X];
    }
  }

  void knobs(Knob_Callback f)
  {
    Float_knob(f, &speed, IRange(.0001, 2), "timeout");
    Tooltip(f, "Time in seconds before this changes the white line 1 pixel to the right. This aborts the current rendering and starts it again. Note that if this is too fast, nuke will not actually start and draw anything, this is a problem that needs to be addressed...");
  }

  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;
};

static void sleeper(unsigned index, unsigned nThreads, void* d)
{
  while (!((Test*)d)->killthread) {
#ifndef WIN32
    usleep(unsigned(((Test*)d)->speed * 1000000));
#else
    ::Sleep( unsigned(((Test*)d)->speed * 1000 ));
#endif
    ((Test*)d)->increment();
  }
}

static Iop* build(Node* node) { return new Test(node); }
const Iop::Description Test::d(CLASS, 0, build);
