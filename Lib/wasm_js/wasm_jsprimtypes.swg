/* wasm_jsprimtypes.swg
 *
 * Primitive-type SWIG_AsVal/SWIG_From fragments for the -wasm-js target.
 *
 * Direct wasm-export ABI handles primitives in the typical call path;
 * SWIG_AsVal / SWIG_From are the conversions of JS values held as
 * EM_VAL handles (containers, variants, user typemaps).
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

#include <emscripten/val.h>
#include <type_traits>
%}

/* Number/BigInt <-> arithmetic types over a borrowed EM_VAL; integers reject non-integral or out-of-range values */
%fragment("SWIG_WASMJS_Number", "header") {
%#include <cmath>
%#include <cerrno>
%#include <cstdlib>
%#include <limits>
template <class T> SWIGINTERN bool SWIG_WASMJS_NumberFits(double d) {
  if (!std::numeric_limits<T>::is_integer) return true;
  if (std::trunc(d) != d) return false;
  double hi = std::ldexp(1.0, std::numeric_limits<T>::digits);
  double lo = std::numeric_limits<T>::is_signed ? -hi : 0.0;
  return d >= lo && d < hi;
}

template <class T> SWIGINTERN int SWIG_WASMJS_AsNumber(void* p, T* val) {
  if (!p) return SWIG_ERROR;
  emscripten::val v = SWIG_WASMJS_Borrow(static_cast<EM_VAL>(p));
  std::string ty = v.typeOf().as<std::string>();
  if (ty == "number") {
    double d = v.as<double>();
    if (!SWIG_WASMJS_NumberFits<T>(d)) return SWIG_ERROR;
    if (val) *val = static_cast<T>(d);
    return SWIG_OK;
  }
  if (ty != "bigint") return SWIG_ERROR;
  if (!std::numeric_limits<T>::is_integer) {
    if (val) *val = static_cast<T>(emscripten::val::global("Number")(v).as<double>());
    return SWIG_OK;
  }
  /* Exact: parse the decimal representation */
  std::string str = v.call<std::string>("toString");
  char* end = 0;
  errno = 0;
  if (std::numeric_limits<T>::is_signed) {
    long long r = std::strtoll(str.c_str(), &end, 10);
    if (errno || *end || r < static_cast<long long>(std::numeric_limits<T>::min())
        || r > static_cast<long long>(std::numeric_limits<T>::max())) return SWIG_ERROR;
    if (val) *val = static_cast<T>(r);
  } else {
    if (str[0] == '-') return SWIG_ERROR;
    unsigned long long r = std::strtoull(str.c_str(), &end, 10);
    if (errno || *end || r > static_cast<unsigned long long>(std::numeric_limits<T>::max())) return SWIG_ERROR;
    if (val) *val = static_cast<T>(r);
  }
  return SWIG_OK;
}

template <class T> SWIGINTERN EM_VAL SWIG_WASMJS_FromNumber(T value) {
  if (std::numeric_limits<T>::is_integer && std::numeric_limits<T>::digits > 53) {
    if (std::numeric_limits<T>::is_signed) {
      long long v = static_cast<long long>(value);
      if (v > 9007199254740992LL || v < -9007199254740992LL) return emscripten::val(v).release_ownership();
    } else {
      unsigned long long v = static_cast<unsigned long long>(value);
      if (v > 9007199254740992ULL) return emscripten::val(v).release_ownership();
    }
  }
  return emscripten::val(static_cast<double>(value)).release_ownership();
}
}

%define %wasm_js_primtype_asval(T)
%fragment(SWIG_AsVal_frag(T), "header", fragment="SWIG_WASMJS_Number") {
SWIGINTERN int SWIG_AsVal_dec(T)(void* p, T* val) {
  return SWIG_WASMJS_AsNumber< T >(p, val);
}
}
%enddef

%define %wasm_js_primtype_from(T)
%fragment(SWIG_From_frag(T), "header", fragment="SWIG_WASMJS_Number") {
SWIGINTERNINLINE EM_VAL SWIG_From_dec(T)(T value) {
  return SWIG_WASMJS_FromNumber< T >(value);
}
}
%enddef

%define %wasm_js_primtype(T)
%wasm_js_primtype_asval(T)
%wasm_js_primtype_from(T)
%enddef

/* bool: only a JS boolean, so numbers never dispatch as bool */
%fragment(SWIG_AsVal_frag(bool), "header") {
SWIGINTERN int SWIG_AsVal_dec(bool)(void* p, bool* val) {
  if (!p) return SWIG_ERROR;
  emscripten::val v = SWIG_WASMJS_Borrow(static_cast<EM_VAL>(p));
  if (v.typeOf().as<std::string>() != "boolean") return SWIG_ERROR;
  if (val) *val = v.as<bool>();
  return SWIG_OK;
}
}
%fragment(SWIG_From_frag(bool), "header") {
SWIGINTERNINLINE EM_VAL SWIG_From_dec(bool)(bool value) {
  return emscripten::val(value).release_ownership();
}
}

%wasm_js_primtype(int)
%wasm_js_primtype(long)
%wasm_js_primtype(long long)
%wasm_js_primtype(double)
%wasm_js_primtype(float)
%wasm_js_primtype(unsigned int)
%wasm_js_primtype(unsigned long)
%wasm_js_primtype(unsigned long long)
