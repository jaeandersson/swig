/* juliaprimtypes.swg
 *
 * Primitive-type SWIG_AsVal / SWIG_From fragments for the -julia target,
 * operating on jl_value_t* (the julia GUESTOBJECT*).  Used by interface
 * libraries (e.g. casadi.i) that route fuzzy coercion through SWIG_AsVal.
 */

%include <typemaps/swigmacros.swg>
%include <typemaps/fragments.swg>

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

#include <julia.h>
#include <type_traits>
%}

/* Accept any Julia Real (pi, Float32, Rational, BigFloat, ... via Float64);
   reject non-integral sources for integral targets so overload / GenericType
   dispatch can fall through to a floating-point candidate. */
%fragment("SWIG_jl_real_to_double", "header") {
SWIGINTERN bool SWIG_jl_real_to_double(jl_value_t* p, double* d) {
  if (jl_typeis(p, jl_float64_type)) { *d = jl_unbox_float64(p); return true; }
  if (jl_typeis(p, jl_int64_type)) { *d = (double)jl_unbox_int64(p); return true; }
  if (jl_typeis(p, jl_bool_type)) { *d = jl_unbox_bool(p) ? 1.0 : 0.0; return true; }
  jl_value_t* real = jl_get_global(jl_base_module, jl_symbol("Real"));
  if (!real || !jl_isa(p, real)) return false;
  jl_value_t* r = jl_call2(jl_get_function(jl_base_module, "convert"), (jl_value_t*)jl_float64_type, p);
  if (!r || !jl_typeis(r, jl_float64_type)) { jl_exception_clear(); return false; }
  *d = jl_unbox_float64(r);
  return true;
}
}
%define %julia_primtype_asval(T)
%fragment(SWIG_AsVal_frag(T), "header", fragment="SWIG_jl_real_to_double") {
SWIGINTERN int SWIG_AsVal_dec(T)(jl_value_t* p, T* val) {
  if (!p) return SWIG_ERROR;
  double d;
  if (!SWIG_jl_real_to_double(p, &d)) return SWIG_ERROR;
  if (std::is_integral< T >::value &&
      d != static_cast<double>(static_cast<long long>(d))) return SWIG_ERROR;
  if (val) *val = static_cast< T >(d);
  return SWIG_OK;
}
}
%enddef

%define %julia_primtype_from_body(T, BOX)
%fragment(SWIG_From_frag(T), "header") {
SWIGINTERNINLINE jl_value_t* SWIG_From_dec(T)(T value) {
  return BOX;
}
}
%enddef

%julia_primtype_asval(bool)
%julia_primtype_asval(int)
%julia_primtype_asval(long)
%julia_primtype_asval(long long)
%julia_primtype_asval(size_t)
%julia_primtype_asval(double)
%julia_primtype_asval(float)
%julia_primtype_asval(casadi_int)

%julia_primtype_from_body(bool, jl_box_bool(value ? 1 : 0))
%julia_primtype_from_body(int, jl_box_int64((int64_t)value))
%julia_primtype_from_body(long, jl_box_int64((int64_t)value))
%julia_primtype_from_body(long long, jl_box_int64((int64_t)value))
%julia_primtype_from_body(size_t, jl_box_int64((int64_t)value))
%julia_primtype_from_body(double, jl_box_float64(value))
%julia_primtype_from_body(float, jl_box_float64((double)value))
