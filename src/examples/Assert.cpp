// Plugin to test the validity of a tcl expression. Raises an
// error if the expression resolves to 'false'.
// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "DDImage/NoIop.h"
#include "DDImage/Knobs.h"
#include "DDImage/Knob.h"

using namespace DD::Image;

static const char* const HELP =
  "Tests the validity of a user-specified tcl expression. If that "
  "expression resolves to false, this plugin raises an error. Otherwise, "
  "the image is passed through, unchanged.";

class Assert : public NoIop
{
  int _value;
  const char* _message;
public:
  Assert(Node* node) : NoIop(node), _value(1), _message(0) {}

  void knobs(Knob_Callback f)
  {
    Int_knob(f, &_value, "expression", "expression");
    Tooltip(f, "If this is false, you get an error message. Type an '=' sign "
               "or use the right-mouse popup and pick \"Edit Expression\" to "
               "enter an expression.");
    String_knob(f, &_message, "message", "error message");
    Obsolete_knob(f, "error_message", "knob message $value");
    Tooltip(f, "Error message to produce if above expression is false.");
  }

  void _validate(bool for_real)
  {
    NoIop::_validate(for_real);

    if (for_real && !_value) {
      if (_message)
        Op::error("%s", _message);
      else
        Op::error("Assert failed");
    }
  }

  const char* node_help() const { return HELP; }

  static const Iop::Description d;
  const char* Class() const { return d.name; }
};

static Iop* build(Node* node) { return new Assert(node); }
const Iop::Description Assert::d("Assert", 0, build);
