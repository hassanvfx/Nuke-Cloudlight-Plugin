// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

/*! \class CornerPin2DIop
    Allows four points to fit an image to another in translation, rotaion and scale
 */

#include "DDImage/DDWindows.h"
#include "DDImage/Transform.h"
#include "DDImage/Format.h"
#include "DDImage/ViewerContext.h"
#include "DDImage/gl.h"
#include "DDImage/Knobs.h"

using namespace DD::Image;

static const char* const CLASS = "CornerPin2D";
static const char* const HELP = 
  "Allows four points to fit an image to another in translation, rotation and scale.";


struct xyStruct
{
  double x, y;
  bool enable;
};

class CornerPin2D : public Transform
{

  xyStruct sc[4];    // source coordinates
  xyStruct dc[4];    // destination coordinates

  // map 0,0,1,1 square to the four corners:
  void setCornerPinMatrix(xyStruct c[4], Matrix4& q)
  {
    q.makeIdentity();
    double dx3 = (c[0].x - c[1].x) + (c[2].x - c[3].x);
    double dy3 = (c[0].y - c[1].y) + (c[2].y - c[3].y);

    if (dx3 == 0 && dy3 == 0) {
      q.a00 = c[1].x - c[0].x;
      q.a01 = c[2].x - c[1].x;
      q.a03 = c[0].x;
      q.a10 = c[1].y - c[0].y;
      q.a11 = c[2].y - c[1].y;
      q.a13 = c[0].y;
    }
    else {
      double dx1 = c[1].x - c[2].x;
      double dy1 = c[1].y - c[2].y;
      double dx2 = c[3].x - c[2].x;
      double dy2 = c[3].y - c[2].y;
      double z = (dx1 * dy2 - dx2 * dy1);
      q.a30 = (dx3 * dy2 - dx2 * dy3) / z;
      q.a31 = (dx1 * dy3 - dx3 * dy1) / z;
      q.a00 = (c[1].x - c[0].x) + q.a30 * c[1].x;
      q.a01 = (c[3].x - c[0].x) + q.a31 * c[3].x;
      q.a03 = c[0].x;
      q.a10 = (c[1].y - c[0].y) + q.a30 * c[1].y;
      q.a11 = (c[3].y - c[0].y) + q.a31 * c[3].y;
      q.a13 = c[0].y;
    }
  }

  void setMatrix(const xyStruct sc[4], const xyStruct dc[4], Matrix4& matrix)
  {

    // pack the enabled points together into start of array:
    xyStruct sc2[4], dc2[4];
    int ix, cnt = 0;
    for (ix = 0; ix < 4; ix++) {
      if (sc[ix].enable) {
        //point is enabled
        sc2[cnt] = sc[ix];
        dc2[cnt] = dc[ix];
        cnt++;
      }
    }

    Matrix4 p, q;

    // copy the last point to the unenabled points to get 4 of them:
    switch (cnt) {
      case 0:
        matrix.makeIdentity();
        return;

      case 1: //translate by a single point;
        matrix.translation(dc2[0].x - sc2[0].x, dc2[0].y - sc2[0].y);
        return;

      case 2: //create a third point
        sc2[2].x = sc2[0].x - (sc2[1].y - sc2[0].y);
        sc2[2].y = sc2[0].y + (sc2[1].x - sc2[0].x);

        dc2[2].x = dc2[0].x - (dc2[1].y - dc2[0].y);
        dc2[2].y = dc2[0].y + (dc2[1].x - dc2[0].x);

      case 3: //create a fourth point
        sc2[3].x = sc2[1].x + (sc2[2].x - sc2[0].x);
        sc2[3].y = sc2[1].y + (sc2[2].y - sc2[0].y);

        dc2[3].x = dc2[1].x + (dc2[2].x - dc2[0].x);
        dc2[3].y = dc2[1].y + (dc2[2].y - dc2[0].y);

    }

    //transform from source coordinates to a 0,0,1,1, square
    setCornerPinMatrix(sc2, p);
    setCornerPinMatrix(dc2, q);
    matrix = q * p.inverse();

  }

public:

  void _validate(bool for_real)
  {
    setMatrix(sc, dc, *matrix());
    Transform::_validate(for_real);
  }

  void matrixAt(const OutputContext& context, Matrix4& matrix)
  {
    xyStruct sc[4];
    xyStruct dc[4];
    Hash hash;
    knob("to1")->store(DoublePtr, &dc[0].x, hash, context);
    knob("enable1")->store(BoolPtr, &sc[0].enable, hash, context);
    knob("to2")->store(DoublePtr, &dc[1].x, hash, context);
    knob("enable2")->store(BoolPtr, &sc[1].enable, hash, context);
    knob("to3")->store(DoublePtr, &dc[2].x, hash, context);
    knob("enable3")->store(BoolPtr, &sc[2].enable, hash, context);
    knob("to4")->store(DoublePtr, &dc[3].x, hash, context);
    knob("enable4")->store(BoolPtr, &sc[3].enable, hash, context);
    knob("from1")->store(DoublePtr, &sc[0].x, hash, context);
    knob("from2")->store(DoublePtr, &sc[1].x, hash, context);
    knob("from3")->store(DoublePtr, &sc[2].x, hash, context);
    knob("from4")->store(DoublePtr, &sc[3].x, hash, context);
    setMatrix(sc, dc, matrix);
  }

  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }
  static const Description desc;

  CornerPin2D(Node* node) : Transform(node)
  {
    const Format& format = input_format();

    sc[0].x = sc[3].x = format.x();
    sc[1].x = sc[2].x = format.r();
    sc[0].y = sc[1].y = format.y();
    sc[2].y = sc[3].y = format.t();

    dc[0].x = dc[3].x = sc[0].x; //(format.x()*3+format.r())/4;
    dc[1].x = dc[2].x = sc[1].x; //(format.x()+format.r()*3)/4;
    dc[0].y = dc[1].y = sc[0].y; //(format.y()*3+format.t())/4;
    dc[2].y = dc[3].y = sc[2].y; //(format.y()+format.t()*3)/4;

    sc[0].enable = sc[1].enable = sc[2].enable = sc[3].enable = true;
    dc[0].enable = dc[1].enable = dc[2].enable = dc[3].enable = true;
  }

  void knobs(Knob_Callback f)
  {
    XY_knob(f, &dc[0].x, "to1");
    SetFlags(f, DD::Image::Knob::ALWAYS_SAVE);
    Bool_knob(f, &sc[0].enable, "enable1");
    XY_knob(f, &dc[1].x, "to2");
    SetFlags(f, DD::Image::Knob::ALWAYS_SAVE);
    Bool_knob(f, &sc[1].enable, "enable2");
    XY_knob(f, &dc[2].x, "to3");
    SetFlags(f, DD::Image::Knob::ALWAYS_SAVE);
    Bool_knob(f, &sc[2].enable, "enable3");
    XY_knob(f, &dc[3].x, "to4");
    SetFlags(f, DD::Image::Knob::ALWAYS_SAVE);
    Bool_knob(f, &sc[3].enable, "enable4");
    Transform::knobs(f);
    Tab_knob(f, 0, "From");
    XY_knob(f, &sc[0].x, "from1");
    SetFlags(f, DD::Image::Knob::ALWAYS_SAVE);
    XY_knob(f, &sc[1].x, "from2");
    SetFlags(f, DD::Image::Knob::ALWAYS_SAVE);
    XY_knob(f, &sc[2].x, "from3");
    SetFlags(f, DD::Image::Knob::ALWAYS_SAVE);
    XY_knob(f, &sc[3].x, "from4");
    SetFlags(f, DD::Image::Knob::ALWAYS_SAVE);
  }

  /*! Draw the outlines of the source and destination quadrilaterals. */
  void draw_handle(ViewerContext* ctx)
  {
    if (ctx->draw_lines()) {
      glColor(ctx->fg_color());
      glBegin(GL_LINE_LOOP);
      glVertex2d(sc[0].x, sc[0].y);
      glVertex2d(sc[1].x, sc[1].y);
      glVertex2d(sc[2].x, sc[2].y);
      glVertex2d(sc[3].x, sc[3].y);
      glEnd();
      //       glBegin(GL_LINE_LOOP);
      //       glVertex2d(dc[0].x, dc[0].y);
      //       glVertex2d(dc[1].x, dc[1].y);
      //       glVertex2d(dc[2].x, dc[2].y);
      //       glVertex2d(dc[3].x, dc[3].y);
      //       glEnd();
    }
    Transform::draw_handle(ctx);
  }
};

static Iop* build(Node* node) { return new CornerPin2D(node); }
const Iop::Description CornerPin2D::desc(CLASS, "Transform/CornerPin2D", build);

// end of CornerPin2D.C
