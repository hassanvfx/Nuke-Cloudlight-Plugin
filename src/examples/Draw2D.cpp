// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "Draw2D";
static const char* const HELP =
  "Sample source code to draw arbitrary 2d graphics in the viewer.";

#include "DDImage/NoIop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/gl.h"
#include "DDImage/ViewerContext.h"
#include "DDImage/DDMath.h"
#include "DDImage/Knob.h"

using namespace DD::Image;

class TestOp : public NoIop
{
  float x, y, r, t;
  int recursion;
public:
#if DD_IMAGE_VERSION_MAJOR >= 5
  TestOp(Node* node) : NoIop(node)
#else
  TestOp() : NoIop()
#endif
  { x = y = r = t = 0;
    recursion = 12; }
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;
  void build_handles(ViewerContext* ctx);
  void draw_handle(ViewerContext* ctx);
};

void TestOp::knobs(Knob_Callback f)
{
  BBox_knob(f, &x, "box");
  Int_knob(f, &recursion, IRange(0, 20), "recursion");
  SetFlags(f, Knob::SLIDER);
}

#if DD_IMAGE_VERSION_MAJOR >= 5
static Iop* build(Node* node) { return new TestOp(node); }
#else
static Iop* build() { return new TestOp(); }
#endif
const Iop::Description TestOp::d(CLASS, 0, build);

// This method is called to build a list of things to call to actually draw the viewer
// overlay. The reason for the two passes is so that 3D movements in the viewer can be
// faster by not having to call every node, but only the ones that asked for draw_handle
// to be called:
void TestOp::build_handles(ViewerContext* ctx)
{

  // Cause any input iop's to draw (you may want to skip this depending on what you do)
  build_input_handles(ctx);

  // Cause any knobs to draw (we don't have any so this makes no difference):
  build_knob_handles(ctx);

  // Don't draw anything unless viewer is in 2d mode:
  if (ctx->transform_mode() != VIEWER_2D)
    return;

  // make it call draw_handle():
  add_draw_handle(ctx);

}

// Recursive dragon-curve drawer
static void dragon(int recursion, bool flip, bool flip2)
{
  if (recursion > 0) {
    glPushMatrix();
    glScalef(flip ? -M_SQRT1_2 : M_SQRT1_2, M_SQRT1_2, 1);
    glRotatef(45, 0, 0, 1);
    dragon(recursion - 1, false, flip ^ flip2);
    glTranslatef(1, 1, 0);
    if (flip)
      glScalef(-1, 1, 0);
    else
      glScalef(1, -1, 0);
    dragon(recursion - 1, true, !flip);
    glPopMatrix();
  }
  else {
    glBegin(GL_LINE_STRIP);
    const float c = 1 / (2 + M_SQRT2_F);
    glVertex3f(flip2 ? -c : c, 0, 0);
    glVertex3f(0, c, 0);
    glVertex3f(0, 1 - c, 0);
    glVertex3f(c, 1, 0);
    glVertex3f(1 - c, 1, 0);
    glEnd();
  }
}

// And then it will call this when stuff needs to be drawn:
void TestOp::draw_handle(ViewerContext* ctx)
{

  // You may want to do this if validate() calcuates anything you need to draw:
  // validate(false);

  // There are several "passes" and you should draw things during the correct
  // passes. There are a list of true/false tests on the ctx object to see if
  // something should be drawn during this pass.
  //
  // For 2D this will draw both a "shadow" line and a "real" line, but skip
  // all the other calls to draw_handle():
  if ( !ctx->draw_lines() )
    return;
  glColor(ctx->node_color());
  glPushMatrix();
  float w = r - x;
  float h = t - y;
  glTranslatef(x + w / 3, y + h / 5, 0);
  glScalef(w / 2, h * 3 / 5, 1);
  dragon(recursion, false, false);
  glPopMatrix();
}
