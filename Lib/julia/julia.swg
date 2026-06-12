/* julia.swg -- typemap library for the -julia target.
 *
 * Standard names (ctype / in / out / freearg) with standard placeholders
 * drive the extern-"C" boundary; julia-prefixed names (jltype / jlparam /
 * jlout) drive the generated .jl method surface:
 *   jltype  -- ccall argument/return type (Cdouble, Ptr{Cvoid}, ...)
 *   jlparam -- user-facing ::Type annotation on the Julia method parameter
 *   jlout   -- Julia expression converting the checked ccall result;
 *              `$call` is substituted with the ccall expression
 */

%include <juliaruntime.swg>
%include <juliaprimtypes.swg>

/* ---- numeric primitives ---- */
%typemap(ctype)  double, const double&, float, const float& "double"
%typemap(in)     double, float %{ $1 = ($1_ltype)$input; %}
%typemap(in)     const double& (double tmp), const float& (float tmp) %{ tmp = ($*1_ltype)$input; $1 = &tmp; %}
%typemap(out)    double, const double&, float, const float& %{ $result = (double)$1; %}
%typemap(jltype) double, const double&, float, const float& "Cdouble"
%typemap(jlparam) double, const double&, float, const float& "Real"
%typemap(jlout)  double, const double&, float, const float& "_check($call)"

%typemap(ctype)  int, long, long long, short, size_t, unsigned int, unsigned long, unsigned short, casadi_int, const casadi_int& "long long"
%typemap(in)     int, long, long long, short, size_t, unsigned int, unsigned long, unsigned short, casadi_int %{ $1 = ($1_ltype)$input; %}
%typemap(in)     const casadi_int& (long long tmp) %{ tmp = $input; $1 = ($1_ltype)&tmp; %}
%typemap(out)    int, long, long long, short, size_t, unsigned int, unsigned long, unsigned short, casadi_int, const casadi_int& %{ $result = (long long)$1; %}
%typemap(jltype) int, long, long long, short, size_t, unsigned int, unsigned long, unsigned short, casadi_int, const casadi_int& "Clonglong"
%typemap(jlparam) int, long, long long, short, size_t, unsigned int, unsigned long, unsigned short, casadi_int, const casadi_int& "Integer"
%typemap(jlout)  int, long, long long, short, size_t, unsigned int, unsigned long, unsigned short, casadi_int, const casadi_int& "Int(_check($call))"

%typemap(ctype)  bool, const bool& "unsigned char"
%typemap(in)     bool %{ $1 = $input != 0; %}
%typemap(out)    bool, const bool& %{ $result = $1 ? 1 : 0; %}
%typemap(jltype) bool, const bool& "Cuchar"
%typemap(jlparam) bool, const bool& "Bool"
%typemap(jlout)  bool, const bool& "_check($call) != 0"

%typemap(ctype)  void "void"
%typemap(out)    void "";
%typemap(jltype) void "Cvoid"
%typemap(jlout)  void "($call; _check(nothing))"

/* ---- strings ---- */
%typemap(ctype)  std::string, const std::string& "const char *"
%typemap(ctype, out="char *") std::string, const std::string& "const char *"
%typemap(in)     std::string %{ $1 = std::string($input); %}
%typemap(in)     const std::string& (std::string tmp) %{ tmp = std::string($input); $1 = &tmp; %}
%typemap(out)    std::string, const std::string& %{ $result = swig_jl_strdup($1); %}
%typemap(jltype) std::string, const std::string& "Cstring"
%typemap(jltype, out="Ptr{UInt8}") std::string, const std::string& "Cstring"
%typemap(jlparam) std::string, const std::string& "AbstractString"
%typemap(jlout)  std::string, const std::string& "_takestr($call)"

%typemap(ctype)  char *, const char * "const char *"
%typemap(ctype, out="char *") char *, const char * "const char *"
%typemap(in)     char *, const char * %{ $1 = ($1_ltype)$input; %}
%typemap(out)    char *, const char * %{ $result = swig_jl_strdup($1 ? std::string($1) : std::string()); %}
%typemap(jltype) char *, const char * "Cstring"
%typemap(jltype, out="Ptr{UInt8}") char *, const char * "Cstring"
%typemap(jlparam) char *, const char * "AbstractString"
%typemap(jlout)  char *, const char * "_takestr($call)"

/* ---- generic class types: opaque pointer convention ---- */
%typemap(ctype)  SWIGTYPE, SWIGTYPE&, const SWIGTYPE&, SWIGTYPE* "void *"
%typemap(in)     SWIGTYPE %{ $1 = *($&1_ltype)$input; %}
%typemap(in)     SWIGTYPE& %{ $1 = ($1_ltype)$input; %}
%typemap(in)     const SWIGTYPE& %{ $1 = ($1_ltype)$input; %}
%typemap(in)     SWIGTYPE* %{ $1 = ($1_ltype)$input; %}
%typemap(out)    SWIGTYPE %{ $result = (void*) new $1_ltype($1); %}
%typemap(out)    SWIGTYPE& %{ $result = (void*) new $*1_ltype(*$1); %}
%typemap(out)    const SWIGTYPE& %{ $result = (void*) new $*1_ltype(*$1); %}
%typemap(out)    SWIGTYPE* %{ $result = (void*)$1; %}
%typemap(jltype) SWIGTYPE, SWIGTYPE&, const SWIGTYPE&, SWIGTYPE* "Ptr{Cvoid}"
/* jlparam / jlout for class types are computed by the backend (proxy name). */

/* ---- jl_value_t* route: composite values cross as native Julia values.
 * The wrapper compiles against julia.h but does NOT link libjulia; jl_*
 * symbols resolve from the host process at dlopen.  ccall arguments are
 * caller-rooted; C-side intermediates use JL_GC_PUSH*. */
%fragment("SwigJlRuntime", "header") %{
#include <julia.h>
#include <vector>
static std::vector<double> swig_jl_to_vec_double(jl_value_t* v) {
  if (!jl_is_array(v)) throw std::runtime_error("expected a numeric vector");
  jl_array_t* a = (jl_array_t*)v;
  size_t n = jl_array_len(a);
  std::vector<double> out(n);
  jl_value_t* eltype = (jl_value_t*)jl_array_eltype(v);
  if (eltype == (jl_value_t*)jl_float64_type) {
    double* data = jl_array_data(a, double);
    for (size_t i = 0; i < n; ++i) out[i] = data[i];
  } else if (eltype == (jl_value_t*)jl_int64_type) {
    int64_t* data = jl_array_data(a, int64_t);
    for (size_t i = 0; i < n; ++i) out[i] = (double)data[i];
  } else {
    throw std::runtime_error("expected a Float64/Int64 vector");
  }
  return out;
}
static std::vector<long long> swig_jl_to_vec_int(jl_value_t* v) {
  if (!jl_is_array(v)) throw std::runtime_error("expected an integer vector");
  jl_array_t* a = (jl_array_t*)v;
  size_t n = jl_array_len(a);
  std::vector<long long> out(n);
  if ((jl_value_t*)jl_array_eltype(v) != (jl_value_t*)jl_int64_type)
    throw std::runtime_error("expected an Int64 vector");
  int64_t* data = jl_array_data(a, int64_t);
  for (size_t i = 0; i < n; ++i) out[i] = (long long)data[i];
  return out;
}
static std::vector<std::string> swig_jl_to_vec_string(jl_value_t* v) {
  if (!jl_is_array(v)) throw std::runtime_error("expected a string vector");
  jl_array_t* a = (jl_array_t*)v;
  size_t n = jl_array_len(a);
  std::vector<std::string> out(n);
  for (size_t i = 0; i < n; ++i) {
    jl_value_t* el = jl_array_ptr_ref(a, i);
    if (!jl_is_string(el)) throw std::runtime_error("expected a string vector");
    out[i] = std::string(jl_string_ptr(el), jl_string_len(el));
  }
  return out;
}
static jl_value_t* swig_jl_from_vec_double(const std::vector<double>& v) {
  jl_value_t* atype = jl_apply_array_type((jl_value_t*)jl_float64_type, 1);
  jl_array_t* a = jl_alloc_array_1d(atype, v.size());
  JL_GC_PUSH1(&a);
  double* data = jl_array_data(a, double);
  for (size_t i = 0; i < v.size(); ++i) data[i] = v[i];
  JL_GC_POP();
  return (jl_value_t*)a;
}
static jl_value_t* swig_jl_from_vec_int(const std::vector<long long>& v) {
  jl_value_t* atype = jl_apply_array_type((jl_value_t*)jl_int64_type, 1);
  jl_array_t* a = jl_alloc_array_1d(atype, v.size());
  JL_GC_PUSH1(&a);
  int64_t* data = jl_array_data(a, int64_t);
  for (size_t i = 0; i < v.size(); ++i) data[i] = (int64_t)v[i];
  JL_GC_POP();
  return (jl_value_t*)a;
}
static jl_value_t* swig_jl_from_vec_string(const std::vector<std::string>& v) {
  jl_value_t* atype = jl_apply_array_type((jl_value_t*)jl_string_type, 1);
  jl_array_t* a = jl_alloc_array_1d(atype, v.size());
  JL_GC_PUSH1(&a);
  for (size_t i = 0; i < v.size(); ++i)
    jl_array_ptr_set(a, i, jl_cstr_to_string(v[i].c_str()));
  JL_GC_POP();
  return (jl_value_t*)a;
}
%}

%typemap(ctype, fragment="SwigJlRuntime")  std::vector<double>, const std::vector<double>& "jl_value_t*"
%typemap(in)     std::vector<double> %{ $1 = swig_jl_to_vec_double($input); %}
%typemap(in)     const std::vector<double>& (std::vector<double> tmp) %{ tmp = swig_jl_to_vec_double($input); $1 = &tmp; %}
%typemap(out)    std::vector<double>, const std::vector<double>& %{ $result = swig_jl_from_vec_double($1); %}
%typemap(jltype) std::vector<double>, const std::vector<double>& "Any"
%typemap(jlparam) std::vector<double>, const std::vector<double>& "AbstractVector{<:Real}"
%typemap(jlout)  std::vector<double>, const std::vector<double>& "_check($call)::Vector{Float64}"

%typemap(ctype, fragment="SwigJlRuntime")  std::vector<long long>, const std::vector<long long>&, std::vector<casadi_int>, const std::vector<casadi_int>& "jl_value_t*"
%typemap(in)     std::vector<long long> %{ $1 = swig_jl_to_vec_int($input); %}
%typemap(in)     const std::vector<long long>& (std::vector<long long> tmp) %{ tmp = swig_jl_to_vec_int($input); $1 = &tmp; %}
%typemap(out)    std::vector<long long>, const std::vector<long long>& %{ $result = swig_jl_from_vec_int($1); %}
%typemap(jltype) std::vector<long long>, const std::vector<long long>&, std::vector<casadi_int>, const std::vector<casadi_int>& "Any"
%typemap(jlparam) std::vector<long long>, const std::vector<long long>&, std::vector<casadi_int>, const std::vector<casadi_int>& "AbstractVector{<:Integer}"
%typemap(jlout)  std::vector<long long>, const std::vector<long long>& "_check($call)::Vector{Int64}"

%typemap(ctype, fragment="SwigJlRuntime")  std::vector<std::string>, const std::vector<std::string>& "jl_value_t*"
%typemap(in)     std::vector<std::string> %{ $1 = swig_jl_to_vec_string($input); %}
%typemap(in)     const std::vector<std::string>& (std::vector<std::string> tmp) %{ tmp = swig_jl_to_vec_string($input); $1 = &tmp; %}
%typemap(out)    std::vector<std::string>, const std::vector<std::string>& %{ $result = swig_jl_from_vec_string($1); %}
%typemap(jltype) std::vector<std::string>, const std::vector<std::string>& "Any"
%typemap(jlparam) std::vector<std::string>, const std::vector<std::string>& "AbstractVector{<:AbstractString}"
%typemap(jlout)  std::vector<std::string>, const std::vector<std::string>& "_check($call)::Vector{String}"

/* ---- runtime support for interface libraries (casadi.i-style) ----
 * Pointer carriers: a Julia proxy (mutable struct with a `ptr::Ptr{Cvoid}`
 * field) crosses as jl_value_t*; SWIG_ConvertPtr reads the field.  Type
 * validation is deferred to Julia-side dispatch on the proxy types. */
