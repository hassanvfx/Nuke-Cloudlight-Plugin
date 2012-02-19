// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

static const char* const CLASS = "TemporalMedian";

static const char* const HELP =
  "Removes grain by selecting, for each pixel, the median of this frame, "
  "the frame before, and the frame after.";

/*! \class TemporalMedian TemporalMedian.C

   This file implements a NUKE3 plugin that performs a time-based filtering
   functioning for removing grain from images. This is based on a NUKE3
   widget written by Jonathan Egstad.

   The Calculations in this plugin are as follows. Note that I use
   single variables to represent intermediate values, but mean that the
   computation is performed for all RGBA input channels.

   For all RGBA input channels z:

   A = in[z] at Time T
   B = in[z] at Time T-1
   C = in[z] at Time T+1

   Take pairwise maximums:

   D = max(A,C)
   E = max(B,C)
   F = max(A,B)

   And then find the smallest of these maximums:

   G = min(D,E)
   H = min(F,G)

   Now we convert this into a difference from the current frame:

   I = H - A

   And now we do some math:

   We use three values specified by the user:

   rcore, gcore, bcore, acore (referred to here as Xcore, meaning take the
                       correct one for the channel you are looking at)

   J =  (I > Xcore) ? max(2 * Xcore-I,0):I
   K =  (J < -Xcore) ? min(-2 * Xcore - J, 0):J

   Finally, we add back in the current value:

   L = K + A

   And we are done.

   \author Daniel Maskit
   \date September 26th, 2001 File creation.  */

// Standard plug-in include files.

#include "DDImage/Iop.h"
#include "DDImage/NukeWrapper.h"
using namespace DD::Image;
#include "DDImage/Row.h"
#include "DDImage/Tile.h"
#include "DDImage/Knobs.h"
#include "DDImage/Convolve.h"
#include "DDImage/DDMath.h"
using namespace std;

class TemporalMedian : public Iop
{
public:
  //
  // These are necessary to make it possible to multiplex an input and
  // get multiple frames worth of input.
  //
  int maximum_inputs() const { return 1; }
  int minimum_inputs() const { return 1; }
  // Tell it that the single input is now 3 inputs
  int split_input(int n) const { return 3; }
  // Routine to return the frame attached to each input
  const OutputContext& inputContext(int, int, OutputContext&) const;

  //! Constructor. Initialize user controls to their default values.

  TemporalMedian (Node* node) : Iop (node)
  {
    core[0] = core[1] = core[2] = core[3] = 0.05f;
  } // TemporalMedian

  //! Destructor.

  ~TemporalMedian ()
  { } // ~TemporalMedian

  // The default _validate() and _request work good for this

  //! This function does all the work. Blur the color channels using other
  //! channels as your vector data.

  void engine ( int y, int x, int r, ChannelMask channels, Row& out );

  //! Describe each user control so the user interface can be built. In this
  //! class we'll have two sets of inputs. Typically, the user will specify
  //! the two channels that will have the vector information. But, if they are
  //! (none), the values in the two double inputs will be used.

  virtual void knobs ( Knob_Callback f )
  {
    AColor_knob(f, core, "core");
    Tooltip(f, "Differences greater than this are left unchanged, as they "
               "probably indicate something other than film grain.");
    Obsolete_knob(f, "Red Core", "knob core.r $value");
    Obsolete_knob(f, "Green Core", "knob core.g $value");
    Obsolete_knob(f, "Blue Core", "knob core.b $value");
    Obsolete_knob(f, "Alpha Core", "knob core.a $value");
  } // knobs

  //! Return the name of the class.

  const char* Class() const { return CLASS; }
  const char* node_help() const { return HELP; }

  //! Information to the plug-in manager of DDNewImage/Nuke.

  static const Iop::Description description;

protected:

  // Variables that are attached to knobs.
  //
  float core[4];
}; // class TemporalMedian

// The time for image input n :-
const OutputContext& TemporalMedian::inputContext(int i, int n, OutputContext& context) const
{
  context = outputContext();
  switch (n) {
    case 0:
      break;
    case 1:
      context.setFrame(context.frame() - 1);
      break;
    case 2:
      context.setFrame(context.frame() + 1);
      break;
  }
  return context;
}

/*! This is a function that creates an instance of the operator, and is
   needed for the Iop::Description to work.
 */
static Iop* TemporalMedianCreate(Node* node)
{
  return new NukeWrapper (new TemporalMedian(node));
}

/*! The Iop::Description is how NUKE knows what the name of the operator is,
   how to create one, and the menu item to show the user. The menu item may be
   0 if you do not want the operator to be visible.
 */
const Iop::Description TemporalMedian::description ( CLASS, "Filter/TemporalMedian",
                                                     TemporalMedianCreate );

/*! For each line in the area passed to request(), this will be called. It must
   calculate the image data for a region at vertical position y, and between
   horizontal positions x and r, and write it to the passed row
   structure. Usually this works by asking the input for data, and modifying
   it.

 */
void TemporalMedian::engine ( int y, int x, int r,
                              ChannelMask channels, Row& row )
{
  row.get(input0(), y, x, r, channels);
  Row prevrow(x, r);
  Row nextrow(x, r);
  prevrow.get(input1(), y, x, r, channels);
  nextrow.get(*input(2), y, x, r, channels);

  foreach ( z, channels ) {
    const float* PREV = prevrow[z] + x;
    const float* CUR  = row[z] + x;
    const float* NEXT = nextrow[z] + x;
    float* outptr = row.writable(z) + x;
    const float* END = outptr + (r - x);
    const float core = this->core[z <= Chan_Alpha ? z - 1 : 0];

    while (outptr < END) {
      // We use single letter variable names here to correspond with the
      // text description of the algorithm above.

      float A = *CUR++;
      float B = *PREV++;
      float C = *NEXT++;

      float D = MAX(A, C);
      float E = MAX(B, C);
      float F = MAX(A, B);

      float G = MIN(D, E);
      float H = MIN(F, G);
      float I = H - A;

      float J =  (I > core) ? MAX(2 * core - I, 0.0f) : I;
      float K =  (J < -core) ? MIN(-2 * core - J, 0.0f) : J;
      *outptr++ = K + A;
    }
  }
} // TemporalMedian::engine
