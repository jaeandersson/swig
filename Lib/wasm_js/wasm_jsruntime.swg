/* wasm_jsruntime.swg -- inserts the SWIG runtime into the generated C++
   output via the "runtime" file slot.  Mirrors matlabruntime.swg.

   The runtime is composed of:
     - swigrun.swg     core SWIG types (swig_type_info, swig_module_info,
                       SWIG_TypeCheck, SWIG_TypeCast, SWIG_TypeQueryModule, ...)
     - swigerrors.swg  SWIG_OK / SWIG_ERROR / error-code constants
     - wasm_jsrun.swg wasm-specific SWIG_ConvertPtr / SWIG_NewPointerObj,
                       SWIG_Error glue (no host runtime; JS proxy enforces
                       types, so these are mostly identity casts).

   swiginit.swg goes into "initbeforefunc" -- it defines the lazy
   SWIG_InitializeModule body that builds the swig_module_info type table
   on first use. */

%insert(runtime) %{
#include <emscripten.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
%}

%insert(runtime) "swigrun.swg";
%insert(runtime) "swigerrors.swg";
%insert(runtime) "wasm_jsrun.swg";

/* swiginit.swg defines SWIG_InitializeModule which walks the type table
   to resolve cast chains.  wasm-js does identity ConvertPtr (no runtime
   type check), so we don't call SWIG_InitializeModule -- the type table
   is emitted for completeness but never consulted.  Skip swiginit.swg. */
