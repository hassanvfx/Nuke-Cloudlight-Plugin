#ifndef _PythonGeo_h_
#define _PythonGeo_h_


#include <DDImage/Render.h>
#include <DDImage/Knobs.h>
#include <DDImage/Knob.h>
#include <DDImage/ModifyGeo.h>
#include <DDImage/ViewerContext.h>
#include <DDImage/DDMath.h>

#include "Python.h"

//
// GLOBALS
//

extern const char* const CLASS;
extern const char* const HELP;

//
// TYPES
//

class PythonGeo;


// Define a custom knob which allows us to add handles in the viewer 
class PythonGeo_Knob : public DD::Image::Knob, public DD::Image::PluginPython_KnobI
{
public:
  const char* Class() const;
  PythonGeo_Knob(DD::Image::Knob_Closure* kc, PythonGeo* pgOp, const char* n);
  ~PythonGeo_Knob();

  bool build_handle(DD::Image::ViewerContext*);
  void draw_handle(DD::Image::ViewerContext*);

  bool isHandleSelected( int i );

  PyObject* getGeometry();
  PyObject* getSelection();

  //! Expose this knob to python
  virtual DD::Image::PluginPython_KnobI* pluginPythonKnob();

private:
  static bool handleCallback(DD::Image::ViewerContext*, DD::Image::Knob*, int);

private:
  typedef std::vector<std::vector<unsigned int> > GeoSelection;
  GeoSelection _selection;
  PythonGeo *_pgOp;
  DD::Image::Scene *_scene;
};


// The Op class.
class PythonGeo : public DD::Image::ModifyGeo
{
public:
  static const DD::Image::Op::Description description;

  bool _allowSelection;
  
  PythonGeo(Node* node);

  void knobs(DD::Image::Knob_Callback f);
  void get_geometry_hash();
  void modify_geometry(int obj, DD::Image::Scene& scene, DD::Image::GeometryList& out);
  
  const char* Class() const {return "PythonGeo";}
  const char* node_help() const {return "internal";}
};


typedef struct {
  PyObject_HEAD
  DD::Image::GeometryList *_geo;
} PyGeometryListObject;


typedef struct {
  PyObject_HEAD
  PyGeometryListObject *_geo; // We refer back to the parent object so that when it goes out of scope, so do all the GeoInfos.
  unsigned int _index;
} PyGeoInfoObject;


typedef struct {
  PyObject_HEAD
  PythonGeo_Knob* _knob;
} PythonGeoKnobObject;


//
// FUNCTIONS
//

// Geometry list functions.
PyObject* GeometryList_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
PyGeometryListObject* PyGeometryListObject_FromGeometryList(DD::Image::GeometryList *geometryList);
void GeometryList_dealloc(PyObject *o);
Py_ssize_t GeometryList_slength(PyGeometryListObject *o);
PyObject* GeometryList_item(PyGeometryListObject *o, register Py_ssize_t i);

// GeoInfo functions.
PyObject *GeoInfo_new( PyTypeObject *type, PyObject *args, PyObject *kwds );
PyGeoInfoObject *PyGeoInfoObject_FromGeoInfo( PyGeometryListObject *geometryList, int index );
void GeoInfo_dealloc(PyObject *o);
PyObject *GeoInfo_points( PyGeoInfoObject *self, PyObject *args, PyObject *kwds );
PyObject *GeoInfo_primitives( PyGeoInfoObject *self, PyObject *args, PyObject *kwds );
PyObject *GeoInfo_normals( PyGeoInfoObject *self, PyObject *args, PyObject *kwds );
PyObject *GeoInfo_transform( PyGeoInfoObject *self, PyObject *args, PyObject *kwds );

// PythonGeo_Knob functions
PythonGeo_Knob* PythonGeo_knob(DD::Image::Knob_Callback f, PythonGeo* p, const char* name);
PyObject* PythonGeoKnob_new(PyTypeObject* type, PyObject* args, PyObject* kwargs);
void PythonGeoKnob_dealloc(PyObject *o);
PyObject* PythonGeoKnob_getGeometry(PyObject* self, PyObject* args);
PyObject* PythonGeoKnob_getSelection(PyObject* self, PyObject* args);

// PythonGeo functions
PyGeoInfoObject* PyGeoInfoObject_FromGeoInfo(PyGeometryListObject *o, int index);

// Other functions.
DD::Image::Op* build(Node* node);


#endif // _PythonGeo_h_

