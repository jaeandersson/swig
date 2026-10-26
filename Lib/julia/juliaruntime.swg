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
%insert(runtime) "swiglabels.swg";
%insert(runtime) "swigerrors.swg";
%insert(runtime) %{
/* Julia-proxy-name -> swig_type_info registry, filled by the generated
   _swig_jl_init_types() (called from the module __init__).  ConvertPtr
   validates the proxy's Julia type against the expected descriptor via
   the SWIG cast chains (derived -> base accepted, with pointer
   adjustment); unknown names with a non-null expected type are rejected. */
#include <map>
#include <string>
#include <stdexcept>
#include <limits>
SWIGRUNTIME std::map<std::string, swig_type_info*>& swig_jl_proxy_registry() {
  static std::map<std::string, swig_type_info*> reg;
  return reg;
}
SWIGRUNTIME std::map<std::string, jl_value_t*>& swig_jl_proxy_types() {
  static std::map<std::string, jl_value_t*> reg;  /* Julia DataTypes: globally rooted */
  return reg;
}
SWIGRUNTIME std::map<swig_type_info*, jl_value_t*>& swig_jl_ti2type() {
  static std::map<swig_type_info*, jl_value_t*> reg;
  return reg;
}
extern "C" SWIGEXPORT void swig_jl_register_proxy_type(const char* jlname, jl_value_t* jltype) {
  swig_jl_proxy_types()[jlname] = jltype;
  std::map<std::string, swig_type_info*>::iterator it = swig_jl_proxy_registry().find(jlname);
  if (it != swig_jl_proxy_registry().end()) swig_jl_ti2type()[it->second] = jltype;
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
/* `this` of a base-class method reached through a derived proxy: apply the cast chain */
extern "C" SWIGEXPORT void* swig_jl_upcast(jl_value_t* p, const char* base) {
  void* r = 0;
  std::map<std::string, swig_type_info*>::iterator it = swig_jl_proxy_registry().find(base);
  if (it == swig_jl_proxy_registry().end() || swig_jl_convert_ptr(p, &r, it->second) != SWIG_OK)
    swig_jl_convert_ptr(p, &r, 0);
  return r;
}
/* Returns are wrapped Julia-side (proxy ctor over void*); from_ptr paths
   that build GUESTOBJECT returns box the pointer for the .jl layer. */
SWIGRUNTIME jl_value_t* swig_jl_new_pointer_obj(void* ptr, swig_type_info* type) {
  if (type) {
    std::map<swig_type_info*, jl_value_t*>::iterator it = swig_jl_ti2type().find(type);
    if (it == swig_jl_ti2type().end()) {
      /* fallback: match by mangled name (duplicate table entries) */
      for (std::map<std::string, swig_type_info*>::iterator r = swig_jl_proxy_registry().begin();
           r != swig_jl_proxy_registry().end(); ++r) {
        if (r->second && type->name && strcmp(r->second->name, type->name) == 0) {
          std::map<std::string, jl_value_t*>::iterator t2 = swig_jl_proxy_types().find(r->first);
          if (t2 != swig_jl_proxy_types().end()) {
            swig_jl_ti2type()[type] = t2->second;
            it = swig_jl_ti2type().find(type);
          }
          break;
        }
      }
    }
    if (it != swig_jl_ti2type().end()) {
      jl_value_t* boxed = jl_box_voidpointer(ptr);
      JL_GC_PUSH1(&boxed);
      jl_value_t* r = jl_call1((jl_value_t*)it->second, boxed);  /* proxy w/ finalizer */
      JL_GC_POP();
      return r;
    }
  }
  return jl_box_voidpointer(ptr);
}
#define SWIG_NewPointerObj(ptr, type, flags) swig_jl_new_pointer_obj(ptr, type)
/* typecheck helpers: x isa Base.<name>; an Integer that fits T (and the long long ctype) */
SWIGRUNTIME int swig_jl_isa_base(jl_value_t* x, const char* name) {
  jl_value_t* t = jl_get_global(jl_base_module, jl_symbol(name));
  return t && jl_isa(x, t);
}
template <typename T> SWIGRUNTIME int swig_jl_check_int(jl_value_t* x) {
  long long v;
  if (jl_typeis(x, jl_int64_type)) {
    v = jl_unbox_int64(x);
  } else if (jl_is_bool(x)) {
    v = jl_unbox_bool(x);
  } else {
    if (!swig_jl_isa_base(x, "Integer")) return 0;
    jl_value_t* r = jl_call2(jl_get_function(jl_base_module, "convert"), (jl_value_t*)jl_int64_type, x);
    if (!r || !jl_typeis(r, jl_int64_type)) { jl_exception_clear(); return 0; }
    v = jl_unbox_int64(r);
  }
  if (std::numeric_limits<T>::is_signed) return v >= (long long)std::numeric_limits<T>::min() && v <= (long long)std::numeric_limits<T>::max();
  return v >= 0 && (unsigned long long)v <= (unsigned long long)std::numeric_limits<T>::max();
}
#define SWIG_exception_fail(code, msg) do { throw std::runtime_error(msg); } while (0)
#define SWIG_exception(code, msg)      do { throw std::runtime_error(msg); } while (0)
#define SWIG_fail                      throw std::runtime_error("SWIG conversion failed")
%}
%insert(runtime) %{
/* pops the JL_GC_PUSH frame of its scope on every exit, C++ exceptions included */
struct SwigJlGcPop { ~SwigJlGcPop() { JL_GC_POP(); } };
/* lets the GC run during the scope, which must not touch Julia values (C++ that may block on threads calling into Julia) */
struct SwigJlGcSafe {
  jl_ptls_t ptls;
  int8_t state;
  SwigJlGcSafe() : ptls(jl_current_task->ptls), state(jl_gc_safe_enter(ptls)) {}
  ~SwigJlGcSafe() { jl_gc_safe_leave(ptls, state); }
};
/* the calling thread's Julia state, adopting a thread Julia does not know; out of line, as the
   const jl_get_pgcstack would otherwise keep returning the null of before the adoption */
#if defined(__GNUC__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
SWIGRUNTIME jl_ptls_t SWIG_JuliaThread(void) {
  jl_gcframe_t **pgcstack = jl_get_pgcstack();
  if (!pgcstack) pgcstack = jl_adopt_thread();
  return container_of(pgcstack, jl_task_t, gcstack)->ptls;
}
/* runs Julia during the scope from any thread, also from inside a SwigJlGcSafe */
struct SwigJlGcUnsafe {
  jl_ptls_t ptls;
  int8_t state;
  SwigJlGcUnsafe() : ptls(SWIG_JuliaThread()), state(jl_gc_unsafe_enter(ptls)) {}
  ~SwigJlGcUnsafe() { jl_gc_unsafe_leave(ptls, state); }
};
/* %append_output target: first output returned bare; further outputs grow a tuple. */
SWIGRUNTIME jl_value_t* SWIG_AppendOutput(jl_value_t* cur, jl_value_t* x) {
  if (!cur) return x;
  size_t n = jl_is_tuple(cur) ? jl_nfields(cur) : 1;
  jl_value_t** vals;
  JL_GC_PUSHARGS(vals, n + 2);
  vals[n] = x;
  vals[n + 1] = cur;
  if (jl_is_tuple(cur)) {
    for (size_t i = 0; i < n; ++i) vals[i] = jl_fieldref(cur, i);
  } else {
    vals[0] = cur;
  }
  jl_value_t* r = jl_call(jl_get_function(jl_core_module, "tuple"), vals, (int)(n + 1));
  JL_GC_POP();
  return r;
}
/* Vector{Any} narrowed to Vector{elt}; unchanged if an element does not convert */
SWIGRUNTIME jl_value_t* swig_jl_narrow(jl_value_t* a, jl_value_t* elt) {
  jl_value_t* r = 0;
  JL_GC_PUSH2(&a, &r);
  r = jl_call2(jl_get_function(jl_base_module, "convert"), jl_apply_array_type(elt, 1), a);
  if (!r) { jl_exception_clear(); r = a; }
  JL_GC_POP();
  return r;
}
SWIGRUNTIME jl_value_t* swig_jl_proxy_type(const char* jlname) {
  std::map<std::string, jl_value_t*>::iterator it = swig_jl_proxy_types().find(jlname);
  return it == swig_jl_proxy_types().end() ? (jl_value_t*)jl_any_type : it->second;
}
/* output arguments of a value-returning function: pushed onto a Julia vector */
SWIGRUNTIME void swig_jl_push_output(jl_value_t* outs, jl_value_t* x) {
  JL_GC_PUSH1(&x);
  jl_value_t* r = jl_call2(jl_get_function(jl_base_module, "push!"), outs, x);
  JL_GC_POP();
  if (!r) throw std::runtime_error("push! failed");
}
%}
%insert(runtime) %{
/* single-module process: no cross-module type sharing */
#define SWIG_GetModule(cd) 0
#define SWIG_SetModule(cd, m) (void)(m)
%}
%insert(init) "swiginit.swg";
