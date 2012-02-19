// SimpleAxis.C
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/Op.h"
#include "DDImage/Matrix4.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"
#include "DDImage/ViewerContext.h"
#include "DDImage/gl.h"

using namespace DD::Image;

class SimpleAxis : public Op
{
protected:
  Matrix4 local_;        //!< Local matrix that SimpleAxis_Knob fills in
  Matrix4 matrix_;       //!< Object matrix - local&parent
  int display3d_;        //!< GUI display setting
  bool selectable_;      //!< GUI selectable checkmark

  /*! Validate our parent axis
   */
  void _validate(bool for_real)
  {
    if (input0()) {
      // Validate input0 and concatenate with it's matrix:
      input0()->validate(for_real);
      matrix_ = input0()->matrix() * local_;
    }
    else {
      // Use local matrix only:
      matrix_ = local_;
    }
  }


public:
  static const Description description;

  const char* Class() const
  {
    return description.name;
  }

  const char* node_help() const
  {
    return "SimpleAxis:\n"
           "Defines a 3D transformation. Connecting this as the input to "
           "another SimpleAxis will cause that object's "
           "transformation to be parented to this one.";
  }

  SimpleAxis(Node* node) : Op(node)
  {
    local_.makeIdentity();
    matrix_.makeIdentity();
    display3d_ = DISPLAY_WIREFRAME;
    selectable_ = true;
  }

  virtual ~SimpleAxis()
  {
  }

  int minimum_inputs() const { return 1; }
  int maximum_inputs() const { return 1; }
  SimpleAxis* input0() const { return (SimpleAxis*)(Op::input0()); }
  const Matrix4& local()  const { return local_; }
  const Matrix4& matrix() const { return matrix_; }

  int display3d() const { return display3d_; }
  bool selectable() const { return selectable_; }
  void display3d(int v) { display3d_ = v; }
  void selectable(bool v) { selectable_ = v; }

  /*! Only SimpleAxis and NULL work.
   */
  bool test_input(int input, Op* op) const
  {
    if (input == 0)
      return dynamic_cast<SimpleAxis*>(op) != 0;
    return false;
  }

  /*! Specifies the GUI node shape
   */
  const char* node_shape() const
  {
    return "O";
  }

  /*! Axis knobs
   */
  void knobs(Knob_Callback f)
  {
    Enumeration_knob(f, &display3d_, display3d_names_source, "display");
    Bool_knob(f, &selectable_, "selectable");
    Axis_knob(f, &local_, "transform");
  }

  /*! This version will always cause draw_handle() to be called when in 3D mode.
   */
  void build_handles(ViewerContext* ctx)
  {
    if (ctx->transform_mode() == VIEWER_2D)
      return;

    validate(false);
    build_input_handles(ctx);  // inputs are drawn in current world space

    // knobs are drawn in parent's space:
    Matrix4 saved_matrix = ctx->modelmatrix;

    if (input0())
      ctx->modelmatrix *= input0()->matrix();

    build_knob_handles(ctx);

    // We only draw the object if viewer is in 3D display mode:
    if (ctx->viewer_mode() && display3d_) {
      add_draw_handle(ctx);
      ctx->expand_bbox(node_selected(), local_.a03, local_.a13, local_.a23);
    }
    ctx->modelmatrix = saved_matrix;
  }

  /*! Draws any geometry attached to this axis. Note that the SimpleAxis knob
     will draw the 3-arrow axis control in the center.
   */
  void draw_handle(ViewerContext* ctx)
  {
    if (selectable_ ? !ctx->draw_lines() : !ctx->draw_unpickable_lines())
      return;
    bool selected = node_selected();
    int display3d = ctx->display3d(this->display3d_);
    if (!display3d && !selected)
      return;

    if (selected)
      glColor(ctx->selected_color());
    else
      glColor(ctx->node_color());

    glPushMatrix();
    glMultMatrixf(local().array());

    glBegin(GL_LINES);
    glVertex3f(-1.0f,  0.0f,  0.0f);
    glVertex3f( 1.0f,  0.0f,  0.0f);
    glVertex3f(  0.0f, -1.0f,  0.0f);
    glVertex3f(  0.0f, 1.0f,  0.0f);
    glVertex3f(  0.0f,  0.0f, -1.0f);
    glVertex3f(  0.0f,  0.0f, 1.0f);
    glEnd();

    glColor(ctx->fg_color());
    glRasterPos3f(0, 0, 0);
    gl_text( "My string");

    glPopMatrix();
  }

};

static Op* SimpleAxis_constructor(Node* node)
{
  return new SimpleAxis(node);
}

const Op::Description SimpleAxis::description("SimpleAxis", SimpleAxis_constructor);
