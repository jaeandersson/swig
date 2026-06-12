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
/* Julia-proxy-name -> swig_type_info registry, filled by the generated
   _swig_jl_init_types() (called from the module __init__).  ConvertPtr
   validates the proxy's Julia type against the expected descriptor via
   the SWIG cast chains (derived -> base accepted, with pointer
   adjustment); unknown names with a non-null expected type are rejected. */
#include <map>
#include <string>
SWIGRUNTIME std::map<std::string, swig_type_info*>& swig_jl_proxy_registry() {
  static std::map<std::string, swig_type_info*> reg;
  return reg;
}
SWIGRUNTIME void swig_jl_register_proxy(const char* jlname, swig_type_info* ti) {
  if (ti) swig_jl_proxy_registry()[jlname] = ti;
}
SWIGRUNTIME int swig_jl_convert_ptr(jl_value_t* p, void** out, swig_type_info* type) {
  if (!p || jl_is_nothing(p)) return SWIG_ERROR;
  jl_value_t* t = jl_typeof(p);
  if (!jl_is_mutable_datatype(t)) return SWIG_ERROR;
  jl_datatype_t* dt = (jl_datatype_t*)t;
  if (jl_datatype_nfields(dt) < 1) return SWIG_ERROR;
  jl_value_t* f = jl_get_nth_field(p, 0);
  if (!jl_typeis(f, jl_voidpointer_type)) return SWIG_ERROR;
  void* raw = jl_unbox_voidpointer(f);
  if (type) {
    const char* jlname = jl_symbol_name(dt->name->name);
    std::map<std::string, swig_type_info*>& reg = swig_jl_proxy_registry();
    std::map<std::string, swig_type_info*>::iterator it = reg.find(jlname);
    if (it == reg.end()) return SWIG_ERROR;
    if (it->second != type) {
      swig_cast_info* cast = SWIG_TypeCheckStruct(it->second, type);
      if (!cast) return SWIG_ERROR;
      int newmemory = 0;
      raw = SWIG_TypeCast(cast, raw, &newmemory);
    }
  }
  if (out) *out = raw;
  return SWIG_OK;
}
#define SWIG_ConvertPtr(obj, pptr, type, flags) swig_jl_convert_ptr(obj, pptr, type)
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
