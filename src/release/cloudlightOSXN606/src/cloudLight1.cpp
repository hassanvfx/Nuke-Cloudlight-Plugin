// cloudLight1.C
// Sphere demo Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.
// Cloudlight Copyright Hassan Uriostegui (c) 2012.

static const char* const CLASS = "cloudLight1";
static const char* const HELP = "Generates a 3D cloudLight1";


#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Tile.h"

#include "DDImage/SourceGeo.h"
#include "DDImage/Scene.h"
#include "DDImage/Triangle.h"
#include "DDImage/Knobs.h"
#include "DDImage/Knob.h"
#include "DDImage/Channel3D.h"
#include <assert.h>
#include <vector>

#include "cloudlet.h"

using namespace DD::Image;

class cloudLight1 : public SourceGeo
{
private:
    
    double resolution;
    double radius;
    bool useLuma;
    double depth;
    
    unsigned columns, rows,grid_stream;
    bool useTop, useBottom, useLeft, useRight, useFront , useBack;
    
    // local matrix that Axis_Knob fills in
    Matrix4 _local;
    bool fix;
    Knob* _pAxisKnob;
    std::vector<cloudlet> clouds;
    
protected:
    void _validate(bool for_real)
    {
 
        //resolution    = MIN(MAX(resolution,    1.0), 0.1);
        SourceGeo::_validate(for_real);
        
    }
    
   
    
public:
    static const Description description;
    const char* Class() const { return CLASS; }
    const char* node_help() const { return HELP; }
    
    //----------
    int minimum_inputs() const
    {
        return 2;
    }
    int maximum_inputs() const
    {
        return 2;
    }
    
    const char* input_label(int input, char* buffer) const
    {
        switch (input) {
            default: return "";
            case 0: return "colorMap";
            case 1: return "pointMap";
        }
    }
    
  
  
    cloudLight1(Node* node) : SourceGeo(node)
    {
        
        resolution =0.25;
        radius = 1.0;
        rows = columns = 10;
        useTop= useLeft = useRight = useFront = true;
        useBottom = useBack = false;
        useLuma=false;
        depth=1.0;
        
        _local.makeIdentity();
        fix = false;
        _pAxisKnob = NULL;
    }
    
    void knobs(Knob_Callback f)
    {
        SourceGeo::knobs(f);
        
        Double_knob(f, &resolution, "resolution","Resolution %");
        Double_knob(f, &radius, "radius","Cloudlet Scale");
        Divider( f);
        Bool_knob(f, &useLuma, "useLuma"  , "Use PointPass luma as depth");
        Newline(f);
        Double_knob(f, &depth, "depth","Depth scale");
        Divider( f);
        
        Text_knob(f, "Select wich faces to draw:");
        Bool_knob(f, &useFront, "useFront"  , "Front");
        Bool_knob(f, &useBack, "useBack"   , "Back");
        Bool_knob(f, &useTop, "useTop"    , "Top");
        Bool_knob(f, &useBottom, "useBottom" , "Bottom");
        Bool_knob(f, &useLeft, "useLeft"   , "Left");
        Bool_knob(f, &useRight, "useRight"  , "Right");
        Divider( f);
        Text_knob(f, "Cloud Light V2012.1 ( hassan.uriostegui@gmail.com )");
        
        // transform knobs
        _pAxisKnob = Axis_knob(f, &_local, "transform");
        
        if (_pAxisKnob != NULL) {
            if (GeoOp::selectable() == true)
                _pAxisKnob->enable();
            else
                _pAxisKnob->disable();
        }
        
        // This knob is set by knob_default so that all new instances execute
        // the "fix" code, which rotates the cloudLight1 180 degrees so that the
        // seam is on the far side from the default camera position.
        Bool_knob(f, &fix, "fix", INVISIBLE);
    }
    
    /*! The will handle the knob changes.
     */
    int knob_changed(Knob* k)
    {
        if (k != NULL) {
            if (strcmp(k->name(), "selectable") == 0) {
                if (GeoOp::selectable() == true)
                    _pAxisKnob->enable();
                else
                    _pAxisKnob->disable();
                return 1;
            }
        }
        
        return SourceGeo::knob_changed(k);
    }
    
    // Hash up knobs that affect the cloudLight1:
    void get_geometry_hash()
    {
        SourceGeo::get_geometry_hash();   // Get all hashes up-to-date
        
        Iop* colorMap = (Iop*)input0();
        Iop* pointMap = (Iop*)input1();
        
        // Knobs that change the geometry structure:
        geo_hash[Group_Primitives].append(outputContext().frame());
        geo_hash[Group_Primitives].append(colorMap->hash());
        geo_hash[Group_Primitives].append(pointMap->hash());
        
        geo_hash[Group_Primitives].append(useTop);
        geo_hash[Group_Primitives].append(useBottom);
        geo_hash[Group_Primitives].append(useFront);
        geo_hash[Group_Primitives].append(useBack);
        geo_hash[Group_Primitives].append(useLeft);
        geo_hash[Group_Primitives].append(useRight);
        
        
        geo_hash[Group_Primitives].append(useLuma);
        geo_hash[Group_Primitives].append(depth);
    
        geo_hash[Group_Primitives].append(resolution);
        geo_hash[Group_Primitives].append(radius);
        
        // Knobs that change the point locations:
        //geo_hash[Group_Points].append(outputContext().frame());
        geo_hash[Group_Points].append(colorMap->hash());
        geo_hash[Group_Points].append(pointMap->hash());
        
        geo_hash[Group_Points].append(resolution);
        geo_hash[Group_Points].append(radius);
        
        geo_hash[Group_Points].append(useBottom);
        geo_hash[Group_Points].append(useFront);
        geo_hash[Group_Points].append(useBack);
        geo_hash[Group_Points].append(useLeft);
        geo_hash[Group_Points].append(useRight);
        
        
        geo_hash[Group_Points].append(useLuma);
        geo_hash[Group_Points].append(depth);
        
        geo_hash[Group_Matrix].append(_local.a00);
        geo_hash[Group_Matrix].append(_local.a01);
        geo_hash[Group_Matrix].append(_local.a02);
        geo_hash[Group_Matrix].append(_local.a03);
        
        geo_hash[Group_Matrix].append(_local.a10);
        geo_hash[Group_Matrix].append(_local.a11);
        geo_hash[Group_Matrix].append(_local.a12);
        geo_hash[Group_Matrix].append(_local.a13);
        
        geo_hash[Group_Matrix].append(_local.a20);
        geo_hash[Group_Matrix].append(_local.a21);
        geo_hash[Group_Matrix].append(_local.a22);
        geo_hash[Group_Matrix].append(_local.a23);
        
        geo_hash[Group_Matrix].append(_local.a30);
        geo_hash[Group_Matrix].append(_local.a31);
        geo_hash[Group_Matrix].append(_local.a32);
        geo_hash[Group_Matrix].append(_local.a33);
    }
    
    void append(DD::Image::Hash& hash)
    {
         //hash.append(outputContext().frame());
        
        Iop* colorMap = (Iop*)input0();
        hash.append(colorMap->hash());
        
        Iop* pointMap = (Iop*)input1();
        hash.append(pointMap->hash());
    }

    
    // Apply the concat matrix to all the GeoInfos.
    void geometry_engine(Scene& scene, GeometryList& out)
    {

        SourceGeo::geometry_engine(scene, out);
 
        // multiply the node matrix
        for (unsigned i = 0; i < out.size(); i++)
            out[i].matrix = _local * out[i].matrix;
    }
    
    void create_geometry(Scene& scene, GeometryList& out)
    {
                
  
        int obj = 0;
        //=============================================================
        // Calculate number of visible faces
        int cube_faces = 0; 
        
        if(useFront) cube_faces++;
        if(useBack) cube_faces++;
        if(useTop) cube_faces++;
        if(useBottom) cube_faces++;
        if(useLeft) cube_faces++;
        if(useRight) cube_faces++;
        //=============================================================
        // Calculate neededPoints
        
        unsigned cube_points   = cube_faces * (2 * 3); //2 triangles per face
        unsigned num_points  = cube_points*clouds.size();
        
        //=============================================================
        // Build the cloud & primitives:
        if (rebuild(Mask_Primitives)) {
            
            //Free cloudlets
            clouds.clear();
            
            //Prepare maps
            Iop* colorMap = (Iop*)input0();
            colorMap->validate(true);
            colorMap->request(0, 0,  colorMap->w(),  colorMap->h(), Mask_RGBA, 0);
            
            Iop* pointMap = (Iop*)input1();
            pointMap->validate(true);
            pointMap->request(0, 0,  pointMap->w(),  pointMap->h(), Mask_RGBA, 0);
            
            //Get dimensions
            rows=colorMap->h();
            columns=colorMap->w();
            grid_stream = rows*columns;
            
            //Prepare receivers
            Pixel colorPixel(Mask_RGBA);
            Pixel pointPixel(Mask_RGBA); 
            
            float scale = 1.0 / resolution;
            
            for( int x=0; x< (columns*resolution); x++){
                for( int y=0; y<(rows*resolution); y++){
                    
                    colorMap->sample(x*scale,y*scale,1,1,colorPixel);
                    pointMap->sample(x*scale,y*scale,1,1,pointPixel);
                    
                    if(colorPixel[Chan_Alpha] > 0.5 ){
                        //only create if its solid
                        cloudlet CL;
                        
                        CL.r= colorPixel[Chan_Red];
                        CL.g= colorPixel[Chan_Green];
                        CL.b= colorPixel[Chan_Blue];
                        
                        CL.x= pointPixel[Chan_Red];
                        CL.y= pointPixel[Chan_Green];
                        CL.z= pointPixel[Chan_Blue];
                        
                        if(useLuma){
                            float lum = (CL.x+CL.y+CL.z)/3.0f;
                        
                            CL.x= x;
                            CL.y= y ;                      
                            CL.z= lum * ((columns +rows)/10.0f);
                        } 
                        
                        CL.p= ( y*colorMap->h() ) +x;
                        clouds.push_back(CL);
                    }
                    
                    colorPixel.erase();
                    pointPixel.erase();
                    
                }
            }
            colorMap->close();
            pointMap->close();
            
            out.delete_objects();
            out.add_object(obj);
            
            //Update points number
            num_points  = cube_points*clouds.size();
           
            //Populate primitives
            for (int t = 0; t < num_points/3; t++) {
                
                out.add_primitive(obj, new Triangle( (t*3) , (t*3 +1) , (t*3 +2) ));       
    
            }

            
            // Force points and attributes to update:
            set_rebuild(Mask_Points | Mask_Attributes);
        }
        
        
        
        //=============================================================
        // Create points and assign their coordinates:
        if (rebuild(Mask_Points)) {
            // Generate points:
            PointList* points = out.writable_points(obj);
            points->resize(num_points);
            
            float scale = 1.0;
            
            int p=0;
           
            float size = (radius) / resolution;
            float center = size/2.0f;

           
            
            // Assign the point locations:
            for (int cube = 0; cube < clouds.size(); cube++) {
                
                cloudlet cloud=clouds[cube];
                
                //fixed big mistake of iint data type
            
                float x=cloud.x;
                float y=cloud.y;
                float z=cloud.z;
                
                if(useLuma ){
                    x=cloud.x * (1.0/resolution);
                    y=cloud.y * (1.0/resolution);
                    z=cloud.z * depth;
                }
                
                //BACK
                if(useBack){
                    (*points)[ p++].set( x + (( 0.0f -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[ p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[ p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    
                    (*points)[ p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[ p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[ p++].set( x + (( size -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                }
                //FRONT
                if(useFront){
                    (*points)[ p++].set( x + (( 0.0f -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[ p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[ p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( size -center ) *scale ));
                    
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( size -center ) *scale ));
                }
                
                //TOP
                if(useTop){
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( size -center ) *scale ));
                }
                //BOTTOM
                if(useBottom){
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                }
                //LEFT
                if(useLeft){
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( 0.0f -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( size -center ) *scale ));
                }
                //RIGHT
                if(useRight){
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( 0.0f -center ) *scale ), z  + (( size -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( 0.0f -center ) *scale ));
                    (*points)[  p++].set( x + (( size -center ) *scale ) ,y + (( size -center ) *scale ), z  + (( size -center ) *scale ));
                }
                


                //p+=36;
            }
        }
        
        //=============================================================
        // Assign the normals and uvs:
        if (rebuild(Mask_Attributes)) {
            GeoInfo& info = out[obj];
            //---------------------------------------------
            // NORMALS:
            const Vector3* PNTS = info.point_array();
            Attribute* N = out.writable_attribute(obj, Group_Points, "N", NORMAL_ATTRIB);
            assert(N);
            for (unsigned p = 0; p < num_points; p++)
                N->normal(p) = PNTS[p] / radius;
            
            //---------------------------------------------
            // CF:

            Attribute* cf = out.writable_attribute(obj, Group_Points, "Cf", VECTOR4_ATTRIB);
            assert(cf);

             
            int rowLenght = columns * cube_points;
            
            for (unsigned cube = 0; cube < clouds.size(); cube++){
                
                cloudlet cloud=clouds[cube];
                
                float r=cloud.r;
                float g=cloud.g;
                float b=cloud.b;
                
                for(int i=0; i<cube_points; i++){
                    int p = (cube*cube_points)+i;
                    
                    cf->vector4(p).set(   
                                       r, 
                                       g, 
                                       b, 1.0f);
                }

            }
            
            
            /*
            //---------------------------------------------
            // UVs:
            const Primitive** PRIMS = info.primitive_array();
            
            Attribute* uv = out.writable_attribute(obj, Group_Vertices, "uv", VECTOR4_ATTRIB);
             
            assert(uv);
            
            float ds = (360.0f / float(my_u_extent)) / float(columns); // U change per column
            float ss = 0.5f - (360.0f / float(my_u_extent)) / 2.0f;     // Starting U
            float dt = (180.0f / float(my_v_extent)) / float(rows);     // V change per row
            float st = 0.5 - (180.0 / my_v_extent) / 2.0;              // Starting V
            float s, t;                                             // Current UV
            t = st;
            // Bottom center:
            if (close_bottom) {
                s = ss;
                for (int i = 0; i < columns; i++) {
                    unsigned v = (*PRIMS++)->vertex_offset();
                    
                    uv->vector4(v++).set(   s, 0.0f, 0.0f, 1.0f);
                 
                    
                    uv->vector4(v++).set(s + ds, t + dt, 0.0f, 1.0f);
                    
                    uv->vector4(v++).set(   s, t + dt, 0.0f, 1.0f);
                   
                    s += ds;
                }
                t += dt;
            }
            
            // Create the poly mesh in center:
            for (int j = 0; j < rows - 2; j++) {
                s = ss;
                for (int i = 0; i < columns; i++) {
                    unsigned v = (*PRIMS++)->vertex_offset();
                    uv->vector4(v++).set(   s,    t, 0.0f, 1.0f);
                    uv->vector4(v++).set(s + ds,    t, 0.0f, 1.0f);
                    uv->vector4(v++).set(   s, t + dt, 0.0f, 1.0f);
                    v = (*PRIMS++)->vertex_offset();
                    uv->vector4(v++).set(   s, t + dt, 0.0f, 1.0f);
                    uv->vector4(v++).set(s + ds,    t, 0.0f, 1.0f);
                    uv->vector4(v++).set(s + ds, t + dt, 0.0f, 1.0f);
                    s += ds;
                }
                t += dt;
            }
            
            // Top endcap:
            if (close_top) {
                s = ss;
                for (int i = 0; i < columns; i++) {
                    unsigned v = (*PRIMS++)->vertex_offset();
                    uv->vector4(v++).set(   s,    t, 0.0f, 1.0f);
                    uv->vector4(v++).set(s + ds,    t, 0.0f, 1.0f);
                    uv->vector4(v++).set(   s, 1.0f, 0.0f, 1.0f);
                    s += ds;
                }
            }*/
        }
    }
    
    // virtual
    void build_handles(ViewerContext* ctx)
    {
        // call build_matrix_handle to multiply the context model matrix with the local matrix so the
        // nodes above it will display correctly
        build_matrix_handles(ctx, _local);
    }
};

static Op* build(Node* node) { return new cloudLight1(node); }
const Op::Description cloudLight1::description(CLASS, build);

// end of cloudLight1.C
