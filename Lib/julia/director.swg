/* director.swg -- Julia director runtime.
 *
 * Inserted verbatim into the generated wrapper's runtime so SwigDirector_<C>
 * classes compile.  A SwigDirector_<C> holds a jl_value_t* to the Julia
 * object the user subclassed with; each overridden virtual marshals C++ args
 * to Julia (directorin), invokes the module generic `<C>_<method>(self,
 * args...)`, and marshals the Julia return back (directorout).
 *
 * The wrapper compiles against julia.h but does NOT link libjulia; jl_*
 * symbols resolve from the host at dlopen.  The director only borrows a
 * Ref{Any} holding the Julia self object: the Julia proxy owns it (field
 * _swig_refs) and deletes the director when it is finalized. */

#ifndef SWIG_DIRECTOR_JULIA_HEADER_
#define SWIG_DIRECTOR_JULIA_HEADER_

#include <julia.h>
#include <stdexcept>
#include <string>
#include <limits>
#include <stdint.h>

/* Recover the Swig::Director base from a director-managed object pointer. */
#define SWIG_DIRECTOR_CAST(ARG) dynamic_cast<Swig::Director *>(ARG)

/* The Julia module that owns the generated director dispatch functions.
   Set from Julia __init__ via swig_jl_set_module(@__MODULE__). */
static jl_module_t *swig_jl_director_module = 0;
extern "C" SWIGEXPORT void swig_jl_set_module(jl_value_t *m) {
  swig_jl_director_module = (jl_module_t *)m;
}

namespace Swig {
  /* Look up a director dispatch function `name` in the registered module. */
  inline jl_value_t *swig_director_fn(const char *name) {
    jl_module_t *m = swig_jl_director_module ? swig_jl_director_module : jl_main_module;
    return jl_get_function(m, name);
  }

  /* Look up a module-scoped global (the no-override sentinel). */
  inline jl_value_t *swig_director_global(const char *name) {
    jl_module_t *m = swig_jl_director_module ? swig_jl_director_module : jl_main_module;
    return jl_get_global(m, jl_symbol(name));
  }

  /* Render the pending Julia exception (from a failed upcall) as a string, so
     the user's own error text surfaces in the C++-side error.  Uses Julia's
     `sprint(showerror, e)`; falls back to the exception type name if rendering
     itself fails.  Clears no state -- caller decides.  Returns "" if no
     exception is pending. */
  inline std::string swig_director_exception_msg() {
    jl_value_t *e = jl_exception_occurred();
    if (!e) return std::string();
    std::string msg;
    JL_GC_PUSH1(&e);
    jl_value_t *sprintf_fn = jl_get_function(jl_base_module, "sprint");
    jl_value_t *showerror_fn = jl_get_function(jl_base_module, "showerror");
    if (sprintf_fn && showerror_fn) {
      jl_value_t *s = jl_call2(sprintf_fn, (jl_value_t *)showerror_fn, e);
      if (s && jl_is_string(s)) msg = jl_string_ptr(s);
    }
    if (msg.empty()) msg = jl_typeof_str(e);  /* fallback: exception type name */
    JL_GC_POP();
    return msg;
  }

  /* A primitive returned by a Julia override, converted with Base.convert:
     lossless values of any Julia number type are taken, anything else throws. */
  inline jl_value_t *swig_director_convert(jl_datatype_t *t, jl_value_t *x) {
    jl_value_t *r = jl_call2(jl_get_function(jl_base_module, "convert"), (jl_value_t *)t, x);
    if (!r) throw std::runtime_error(swig_director_exception_msg());
    return r;
  }
  template <typename T> T swig_director_as(jl_value_t *x) {
    if (!std::numeric_limits<T>::is_signed) {
      uint64_t u = jl_unbox_uint64(swig_director_convert(jl_uint64_type, x));
      if (u > (uint64_t)std::numeric_limits<T>::max()) throw std::runtime_error("InexactError: value out of range of the C++ return type");
      return (T)u;
    }
    int64_t i = jl_unbox_int64(swig_director_convert(jl_int64_type, x));
    if (i < (int64_t)std::numeric_limits<T>::min() || i > (int64_t)std::numeric_limits<T>::max())
      throw std::runtime_error("InexactError: value out of range of the C++ return type");
    return (T)i;
  }
  template <> inline bool swig_director_as<bool>(jl_value_t *x) {
    return jl_unbox_bool(swig_director_convert(jl_bool_type, x)) != 0;
  }
  template <> inline double swig_director_as<double>(jl_value_t *x) {
    return jl_unbox_float64(swig_director_convert(jl_float64_type, x));
  }
  template <> inline float swig_director_as<float>(jl_value_t *x) {
    return (float)swig_director_as<double>(x);
  }

  /* Base for every SwigDirector_<C>: borrows a Ref{Any} holding the Julia self
     object; the proxy owns the Ref, so self keeps its identity and stays alive. */
  class Director {
  protected:
    jl_value_t *swig_holder;
  public:
    Director(jl_value_t *holder) : swig_holder(holder) { }
    virtual ~Director() { }
    jl_value_t *swig_get_self() const { return swig_holder ? jl_get_nth_field(swig_holder, 0) : 0; }
    void swig_disown() { }  /* ownership stays with the Julia proxy finalizer */
  };

  /* Thrown when a pure-virtual director method has no Julia override. */
  class DirectorPureVirtualException : public std::runtime_error {
  public:
    DirectorPureVirtualException(const char *m)
      : std::runtime_error(std::string("Attempt to invoke pure virtual method ") + m) {}
  };
}

#endif
