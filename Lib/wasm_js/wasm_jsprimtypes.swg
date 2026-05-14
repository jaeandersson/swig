/* wasm_jsprimtypes.swg
 *
 * Primitive-type SWIG_AsVal/SWIG_From fragments for the -wasm-js target.
 *
 * On wasm_js, primitives don't round-trip through GUESTOBJECT at
 * runtime -- the direct ctype/in/out typemaps in wasm_js.swg handle
 * them via the wasm-export ABI (int, double, etc.).
 *
 * These fragments exist purely to satisfy casadi.i's structural fragment
 * dependencies (e.g. casadi_bool depends on SWIG_AsVal_frag(bool)).
 * They use a "boxed primitive" convention: a void* p is interpreted as a
 * pointer to the underlying primitive's storage.  If a SWIG-generated
 * path ever calls these at runtime (it shouldn't in our flow -- direct
 * typemaps win first), they'll work correctly on a properly-boxed
 * pointer.
 *
 * Companion to wasm_js.swg (top-level typemap library).
 */

// SWIG_AsVal_frag / SWIG_From_frag macros (SWIG-level, used in %fragment names).
// fragments.swg depends on swigmacros.swg for %string_name / %symbol_name.
%include <typemaps/swigmacros.swg>
%include <typemaps/fragments.swg>

// SWIG runtime macros (status codes, ownership flag).  SWIG_ConvertPtr
// and SWIG_NewPointerObj live in wasm_jsrun.swg (with the proper
// EM_VAL signatures) -- do NOT redefine them here.
%{
#ifndef SWIG_OK
#define SWIG_OK         (0)
#define SWIG_ERROR      (-1)
#define SWIG_TypeError  (-5)
#define SWIG_IsOK(r)    ((r) >= 0)
#endif

#ifndef SWIG_POINTER_OWN
#define SWIG_POINTER_OWN  0x1
#endif
%}

%define %wasm_js_primtype_asval(T)
%fragment(SWIG_AsVal_frag(T), "header") {
SWIGINTERN int SWIG_AsVal_dec(T)(void* p, T* val) {
  if (!p) return SWIG_ERROR;
  if (val) *val = *static_cast<T*>(p);
  return SWIG_OK;
}
}
%enddef

%define %wasm_js_primtype_from(T)
%fragment(SWIG_From_frag(T), "header") {
SWIGINTERNINLINE void* SWIG_From_dec(T)(T value) {
  T* p = (T*)std::malloc(sizeof(T));
  *p = value;
  return p;
}
}
%enddef

%define %wasm_js_primtype(T)
%wasm_js_primtype_asval(T)
%wasm_js_primtype_from(T)
%enddef

/* bool: special-cased because we want strict 0/1 normalisation. */
%fragment(SWIG_AsVal_frag(bool), "header") {
SWIGINTERN int SWIG_AsVal_dec(bool)(void* p, bool* val) {
  if (!p) return SWIG_ERROR;
  if (val) *val = (*static_cast<int*>(p)) != 0;
  return SWIG_OK;
}
}
%fragment(SWIG_From_frag(bool), "header") {
SWIGINTERNINLINE void* SWIG_From_dec(bool)(bool value) {
  int* p = (int*)std::malloc(sizeof(int));
  *p = value ? 1 : 0;
  return p;
}
}

/* Integer + floating types referenced by casadi.i fragments. */
%wasm_js_primtype(int)
%wasm_js_primtype(long)
%wasm_js_primtype(long long)
%wasm_js_primtype(double)
%wasm_js_primtype(float)
%wasm_js_primtype(unsigned int)
%wasm_js_primtype(unsigned long)
%wasm_js_primtype(unsigned long long)
