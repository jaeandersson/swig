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
