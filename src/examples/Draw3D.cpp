// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "Draw3D";
static const char* const HELP =
  "Sample source code to draw arbitrary 3d graphics in the viewer.\n\n"
  "This draws an icosohedron texture-mapped with the input image.";

#include "DDImage/NoIop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/gl.h"
#include "DDImage/ViewerContext.h"

using namespace DD::Image;

class TestOp : public NoIop
{
  float size;
  float tumble;
public:
#if DD_IMAGE_VERSION_MAJOR >= 5
  TestOp(Node* node) : NoIop(node)
#else
  TestOp() : NoIop()
#endif
  { size = .5;
    tumble = 90; }
  virtual void knobs(Knob_Callback);
  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Iop::Description d;
  void build_handles(ViewerContext* ctx);
  void draw_handle(ViewerContext* ctx);
};

////////////////////////////////////////////////////////////////
// Draws an icosohedron

struct Corner {
  float x, y, z, u, v;
};

static const float T = 1.61803398875; // (1+sqrt(5))/2
Corner corners[12] = {
  { 0, -1, -T, 0, 0 },
  { 0, +1, -T, 0, 1 },
  { 0, +1, +T, 1, 1 },
  { 0, -1, +T, 1, 0 },
  { -1, -T, 0, 0, 0 },
  { +1, -T, 0, 0, 1 },
  { +1, +T, 0, 1, 1 },
  { -1, +T, 0, 1, 0 },
  { -T, 0, -1, 0, 0 },
  { -T, 0, +1, 0, 1 },
  { +T, 0, +1, 1, 1 },
  { +T, 0, -1, 1, 0 }
};

int faces[] = {
  8, 2, 9, 0,
  8, 9, 10, 0,
  8, 10, 3, 0,
  8, 3, 7, 0,
  8, 7, 2, 0,

  2, 1, 9, 0,
  9, 1, 5, 0,
  10, 9, 5, 0,
  10, 5, 4, 0,
  3, 10, 4, 0,
  3, 4, 11, 0,
  7, 3, 11, 0,
  7, 11, 12, 0,
  2, 7, 12, 0,
  2, 12, 1, 0,

  5, 1, 6, 0,
  4, 5, 6, 0,
  11, 4, 6, 0,
  12, 11, 6, 0,
  1, 12, 6, 0,

  0
};

void drawfaces()
{
  int i = 0;
  while (faces[i]) {
    glBegin(GL_POLYGON);
    Corner& c0 = corners[faces[i] - 1];
    Corner& c1 = corners[faces[i + 1] - 1];
    Corner& c2 = corners[faces[i + 2] - 1];
    glNormal3f((c1.y - c0.y) * (c2.z - c0.z) - (c1.z - c0.z) * (c2.y - c0.y),
               (c1.z - c0.z) * (c2.x - c0.x) - (c1.x - c0.x) * (c2.z - c0.z),
               (c1.x - c0.x) * (c2.y - c0.y) - (c1.y - c0.y) * (c2.x - c0.x));
    while (faces[i]) {
      Corner& c = corners[faces[i++] - 1];
      glTexCoord2f(c.u, c.v);
      glVertex3f(c.x, c.y, c.z);
    }
    glEnd();
    i++;
  }
}

void drawlines()
{
  int i = 0;
  while (faces[i]) {
    glBegin(GL_LINE_LOOP);
    while (faces[i]) {
      Corner& c = corners[faces[i++] - 1];
      glVertex3f(c.x, c.y, c.z);
    }
    glEnd();
    i++;
  }
}

////////////////////////////////////////////////////////////////

void TestOp::knobs(Knob_Callback f)
{
  Float_knob(f, &size, "size");
  Float_knob(f, &tumble, IRange(-180, 180), "tumble");
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

  // Don't draw anything unless viewer is in 3d mode:
  if (ctx->transform_mode() == VIEWER_2D)
    return;

  // make it call draw_handle():
  add_draw_handle(ctx);

  // Add our volume to the bounding box, so 'f' works to include this object:
  float r = size * T;
  ctx->expand_bbox(node_selected(), r, r, r);
  ctx->expand_bbox(node_selected(), -r, -r, -r);

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
  // Polygon offset, z clipping, and dash patterns are set up before
  // each pass to make this work.  In general the OpenGL state is set
  // up correctly. Only change it if you need to and use
  // GlPush/PopAttrib() to restore it.

  glPushMatrix();
  glRotatef(tumble, 0, 1, 1);
  glScalef(size, size, size);

  if (ctx->draw_solid()) {
    // we are in a pass where we want to draw filled polygons
    if (!ctx->hit_detect()) { // disable clicking on the polygons selecting object
      input0().set_texturemap(ctx);
      // color it based on color chip in control panel
      glColor(ctx->node_color());
      drawfaces();
      input0().unset_texturemap(ctx);
    }
  }

  if (ctx->draw_hidden_lines()) {
    // We are in a pass where we want to draw lines. This is *both* hidden
    // lines drawn with dashes and visible lines. If you only want visible
    // lines use ctx->draw_lines(). If you only want hidden lines use
    // ctx->draw_hidden_lines() && !ctx->draw_lines().
    // This is also true during ctx->hit_detect() pass so user can click
    // on the lines to pick the polygon.

    // color the wireframe depending on whether node is selected:
    glColor(node_selected() ? ctx->selected_color() : ctx->fg_color());

    drawlines();
  }

  glPopMatrix();
}
