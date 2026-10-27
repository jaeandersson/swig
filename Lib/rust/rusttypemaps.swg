// Include fundamental fragment definitions
%include <typemaps/fragments.swg>

// Rust fragments for primitive types
%include <rustprimtypes.swg>

#define SWIG_Object SwigRV*
#define VOID_Object SWIG_Rust_NewPointerObj(0, 0, 0)

// append output: results go to resv, see the wrapper
#define SWIG_AppendOutput(result, obj)  obj
#define SWIG_SetConstant(name, obj)     (obj)
#define SWIG_Raise(obj, type, desc)     SWIG_Error(SWIG_RuntimeError, "C++ side threw an exception of type " type)

#ifndef SWIG_DIRECTOR_TYPEMAPS
#define SWIG_DIRECTOR_TYPEMAPS
#endif

// Include the unified typemap library
%include <typemaps/swigtypemaps.swg>

%typecheck(SWIG_TYPECHECK_SWIGOBJECT) SWIG_Object "$1 = $input != 0;";
