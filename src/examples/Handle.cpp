// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "Handle";
static const char* const HELP =
  "Sample source code to make your Op get mouse interaction from the Viewer. "
  "Clicking and dragging in the viewer prints messages on stdout.";

#include "DDImage/NoIop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/ViewerContext.h"

using namespace DD::Image;

class TestOp : public NoIop
{
public:
#if DD_IMAGE_VERSION_MAJOR >= 5
  TestOp(Node* node) : NoIop(node)
#else
  TestOp() : NoIop()
#endif
  { }
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;

  bool handle(ViewerContext* ctx, int index);
};

// This is the eventual function that will be called, where you put
// in whatever you really want to have happen as the user clicks on
// the viewer:
bool TestOp::handle(ViewerContext* ctx, int index)
{
  printf("Index %d: ", index);
  switch (ctx->event()) {
    case PUSH: printf("PUSH");
      break;
    case DRAG: printf("DRAG");
      break;
    case RELEASE: printf("RELEASE");
      break;
    case MOVE: printf("MOVE");
      break;
    // MOVE will only work if you use ANYWHERE_MOUSEMOVES instead of
    // ANYWHERE below.
    case KEY: printf("KEY");
      break;
    // KEY appears to be broken in Qt version of Nuke. Probably won't
    // be fixed soon. You can define popup menus with shortcuts that
    // work but that is a whole further subject...
    default: printf("event()==%d", ctx->event());
      break;
  }
  printf(" xyz=%g,%g,%g", ctx->x(), ctx->y(), ctx->z());
  printf(" mousexy=%d,%d", ctx->mouse_x(), ctx->mouse_y());
  printf(" key=%d", ctx->key());
  printf("\n");
  return true; // true means we are interested in the event
}

// Ugly glue needed to fool Nuke into calling this:
// Interaction requires a "knob". This will probably be fixed in the
// future so that all this glue code is not needed and you can directly
// tell it to call your handle() method. For now you have to make
// this dummy knob, and your control panel must be open to get any
// interaction.

class GlueKnob : public Knob
{
  TestOp* theOp;
  const char* Class() const { return "Glue"; }
public:

  // This is what Nuke will call once the below stuff is executed:
  static bool handle_cb(ViewerContext* ctx, Knob* knob, int index)
  {
    return ((GlueKnob*)knob)->theOp->handle(ctx, index);
  }

  // Nuke calls this to draw the handle, this then calls make_handle
  // which tells Nuke to call the above function when the mouse does
  // something...
  void draw_handle(ViewerContext* ctx)
  {
    if (ctx->event() == DRAW_OPAQUE
        || ctx->event() == PUSH // true for clicking hit-detection
        || ctx->event() == DRAG // true for selection box hit-detection
        ) {

      // Draw something in OpenGL that can be hit-detected. If this
      // is hit-detected then handle() is called with index = 1
      begin_handle(ctx, handle_cb, 1 /*index*/, 0, 0, 0);
      glBegin(GL_POLYGON);
      glVertex2i(10, 10);
      glVertex2i(30, 5);
      glVertex2i(35, 35);
      glVertex2i(10, 35);
      glEnd();

      // Draw a dot that is hit-detected. If it is then handle() is
      // called with index = 2
      make_handle(ctx, handle_cb, 2 /*index*/, 50, 50, 0 /*xyz*/);

      // Make clicks anywhere in the viewer call handle() with index = 0.
      // This takes the lowest precedence over, so above will be detected
      // first.
      begin_handle(Knob::ANYWHERE, ctx, handle_cb, 0 /*index*/, 0, 0, 0 /*xyz*/);
      end_handle(ctx);
    }
  }

  // And you need to implement this just to make it call draw_handle:
  bool build_handle(ViewerContext* ctx)
  {
    // If your handles only work in 2D or 3D mode, only return true
    // in those cases:
    // return (ctx->transform_mode() == VIEWER_2D);
    return true;
  }

  GlueKnob(Knob_Closure* kc, TestOp* t, const char* n) : Knob(kc, n)
  {
    theOp = t;
  }

};

void TestOp::knobs(Knob_Callback f)
{
  // create the knob needed to get mouse interaction:
  CustomKnob1(GlueKnob, f, this, "kludge");
  // create other knobs here!
}

#if DD_IMAGE_VERSION_MAJOR >= 5
static Iop* build(Node* node) { return new TestOp(node); }
#else
static Iop* build() { return new TestOp(); }
#endif
const Iop::Description TestOp::d(CLASS, 0, build);
