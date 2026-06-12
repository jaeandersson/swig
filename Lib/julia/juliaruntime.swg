/* juliaruntime.swg -- SWIG runtime for the -julia target.
   swigrun.swg supplies swig_type_info & the type system; ConvertPtr reads
   the first (ptr) field of a Julia mutable-struct proxy.  Type validation
   is deferred to Julia-side dispatch on the proxy types. */
%insert(runtime) %{
#include <cstdlib>
#include <cstring>
#include <julia.h>
%}
%insert(runtime) "swigrun.swg";
%insert(runtime) "swigerrors.swg";
%insert(runtime) %{
SWIGRUNTIME int swig_jl_convert_ptr(jl_value_t* p, void** out) {
  if (!p || jl_is_nothing(p)) return SWIG_ERROR;
  jl_value_t* t = jl_typeof(p);
  if (!jl_is_mutable_datatype(t)) return SWIG_ERROR;
  jl_datatype_t* dt = (jl_datatype_t*)t;
  if (jl_datatype_nfields(dt) < 1) return SWIG_ERROR;
  jl_value_t* f = jl_get_nth_field(p, 0);
  if (!jl_typeis(f, jl_voidpointer_type)) return SWIG_ERROR;
  if (out) *out = jl_unbox_voidpointer(f);
  return SWIG_OK;
}
#define SWIG_ConvertPtr(obj, pptr, type, flags) swig_jl_convert_ptr(obj, pptr)
/* Returns are wrapped Julia-side (proxy ctor over void*); from_ptr paths
   that build GUESTOBJECT returns box the pointer for the .jl layer. */
SWIGRUNTIME jl_value_t* swig_jl_new_pointer_obj(void* ptr) {
  return jl_box_voidpointer(ptr);
}
#define SWIG_NewPointerObj(ptr, type, flags) swig_jl_new_pointer_obj(ptr)
#define SWIG_exception_fail(code, msg) do { throw std::runtime_error(msg); } while (0)
#define SWIG_exception(code, msg)      do { throw std::runtime_error(msg); } while (0)
#define SWIG_fail                      throw std::runtime_error("SWIG conversion failed")
%}
