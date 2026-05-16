/* wasm_jsrun.swg -- wasm-js runtime glue.  Raw C++ included
   verbatim into the "runtime" slot of the generated wrapper via
   %insert(runtime) "wasm_jsrun.swg".

   Defines the language-specific SWIG runtime entry points expected by
   the SWIG standard typemap library.  Mirrors matlabrun.swg but
   skips the SwigPtr chain / SWIG_TypeCheck walk: wasm_js doesn't
   emit per-class swig_type_info tables -- the JS-side proxy class
   hierarchy enforces type integrity by class identity.

   Wire format:
     A wrapped object crosses the wasm boundary as an EM_VAL handle
     (an i32 index into Embind's value table).  This is the wasm-js
     analog of MATLAB's mxArray* and Python's PyObject*.  On the JS
     side, the SWIG proxy class instance carries a `_ptr` property
     (the underlying C++ pointer); SWIG_ConvertPtr below unwraps the
     val via emscripten::val and reads `_ptr` to recover that pointer.
     SWIG_NewPointerObj builds a JS object `{_ptr: <int>}` and returns
     its EM_VAL handle for the JS side to wrap in the proxy class. */

#include <emscripten/val.h>
#include <emscripten/bind.h>

/* val.h declares EM_VAL (and the underlying `struct _EM_VAL`) inside
   namespace emscripten.  Bring both into global scope so the rest of
   the generated wrapper -- including casadi.i's
   `#define GUESTOBJECT casadi_emval_struct` substitution -- sees them
   as unqualified identifiers, mirroring the way matlab.cxx wrappers
   refer to plain `mxArray` / `mxArray*`. */
using emscripten::EM_VAL;
typedef ::emscripten::_EM_VAL casadi_emval_struct;

/* Thread-local-ish error slot.  wasm-js is single-threaded today, so
   plain statics suffice.  Every generated wrapper emits a `fail:` label
   that SWIG_fail / SWIG_exception_fail goto; the wrapper returns its
   zero-initialised sentinel (`_ret`, 0 / null EM_VAL / null pointer) on
   that path.  The JS side reads `_swig_last_error_*` after each call
   to surface the failure. */
static int  SWIG_lasterror_code = 0;
static char SWIG_lasterror_msg[256] = "";

SWIGINTERN void SWIG_WASMJS_set_error(int code, const char *msg) {
  SWIG_lasterror_code = code;
  if (msg) {
    size_t n = 0;
    while (msg[n] && n < sizeof(SWIG_lasterror_msg) - 1) ++n;
    for (size_t i = 0; i < n; ++i) SWIG_lasterror_msg[i] = msg[i];
    SWIG_lasterror_msg[n] = '\0';
  } else {
    SWIG_lasterror_msg[0] = '\0';
  }
}

/* The wrappers emit a `fail:` label after the regular return path
   (wasm_js.cxx::cpp_call_body).  goto-driven fast exit on conversion
   failure; the JS dispatcher checks _swig_last_error_code() after each
   call and throws. */
#define SWIG_fail                                       goto fail
#define SWIG_exception_fail(code, msg)                  do { SWIG_WASMJS_set_error((code), (msg)); goto fail; } while (0)
#define SWIG_exception(code, msg)                       do { SWIG_WASMJS_set_error((code), (msg)); } while (0)
#define SWIG_Error(code, msg)                           do { SWIG_WASMJS_set_error((code), (msg)); } while (0)

#define SWIG_ConvertPtr(obj, pptr, type, flags)         SWIG_WASMJS_ConvertPtr(obj, (void**)(pptr), type, flags)
#define SWIG_ConvertPtrAndOwn(obj,pptr,type,flags,own)  SWIG_WASMJS_ConvertPtrAndOwn(obj, (void**)(pptr), type, flags, own)
#define SWIG_NewPointerObj(ptr, type, flags)            SWIG_WASMJS_NewPointerObj((void*)(ptr), type, flags)

#define SWIG_ConvertFunctionPtr(obj, pptr, type)        SWIG_ConvertPtr(obj, pptr, type, 0)
#define SWIG_NewFunctionPtrObj(ptr, type)               SWIG_NewPointerObj(ptr, type, 0)

#define SWIG_MODULE_CLIENTDATA_TYPE void*

/* JS-visible accessors.  Reading either clears the slot, so consecutive
   reads are well-defined (subsequent ones see 0 / ""). */
extern "C" EMSCRIPTEN_KEEPALIVE int swig_last_error_code(void) {
  int c = SWIG_lasterror_code;
  SWIG_lasterror_code = 0;
  return c;
}
extern "C" EMSCRIPTEN_KEEPALIVE const char* swig_last_error_msg(void) {
  return SWIG_lasterror_msg;
}

#ifndef SWIGINTERN
#define SWIGINTERN       static
#endif
#ifndef SWIGINTERNINLINE
#define SWIGINTERNINLINE static inline
#endif

/* Phase 3.2: cast-chain + types-array init lives in the wrappers block,
   wired up eagerly via a static-constructor object emitted by
   wasm_js.cxx.  No forward declaration needed here -- the wrappers
   block doesn't reach back into this runtime header. */

/* Val-based ConvertPtr.  $input arrives as an EM_VAL handle for a JS
   value -- typically a SWIG proxy instance carrying a `_ptr` property
   (the underlying C++ pointer) and a `_swig_type` tag on its prototype.
   Borrow ownership (take + release) so the caller's refcount is
   preserved across the peek.

   Phase 3.2: when `type` is non-null, validate type identity by walking
   the cast chain (type->cast) and matching against the val's
   `_swig_type` tag.  This enables dispatcher probes like
   swig_can_Sparsity(MX_inst) to correctly return 0 (no match) instead
   of accepting any proxy by virtue of having a `_ptr`.

   Phase 4.3: when the proxy's from-type differs from the requested
   `type`, ALSO invoke the cast-chain converter to apply C++
   inheritance offset adjustment.  Without this, multi-inheritance
   hierarchies where an intermediate base is non-polymorphic produce
   wrong-offset pointers (e.g. `casadi::Function*` inside a
   `SwigDirector_Callback` lives at +8 because Callback's vptr is at
   offset 0 but Function has no virtuals).  Standard SWIG targets
   (python, matlab) call cast->converter for the same reason; the
   wasm-js target skipped it pre-Phase-4.3 because no director paths
   exercised it.

   When `type` is null (the legacy generic to_ptr<M> fallback), accept
   any proxy carrying a `_ptr` -- preserves the dormant fallback path
   while real callers route through the typed path. */
SWIGRUNTIME int
SWIG_WASMJS_ConvertPtrAndOwn(EM_VAL obj, void **pptr, swig_type_info *type,
                             int SWIGUNUSEDPARM(flags), int *own) {
  if (!obj) return SWIG_ERROR;
  emscripten::val v = emscripten::val::take_ownership(obj);
  bool has_ptr = v.hasOwnProperty("_ptr");
  if (!has_ptr) {
    v.release_ownership();
    return SWIG_ERROR;
  }
  uintptr_t raw = v["_ptr"].as<uintptr_t>();
  void *p = reinterpret_cast<void*>(raw);
  bool type_ok = true;
  if (type) {
    type_ok = false;
    /* `_swig_type` lives on the proxy prototype, not as an own
       property -- read through proto chain and check typeof. */
    emscripten::val tprop = v["_swig_type"];
    if (tprop.typeOf().as<std::string>() == "string") {
      std::string vtag = tprop.as<std::string>();
      if (vtag == type->name) {
        /* Direct match: requested type IS the proxy's type, no
           offset adjustment needed. */
        type_ok = true;
      } else {
        /* Cast required.  Walk `type->cast` looking for an entry whose
           `type->name` matches the proxy's tag.  When found, invoke
           the converter -- the C++ cast helper that applies inheritance
           offsets (e.g. `(Function*)(Callback*)x` for Callback->Function).

           Important: SWIG cast_info arrays are TERMINATED by a sentinel
           `{0, 0, 0, 0}` and entries are visited via POINTER ARITHMETIC
           (++cast), not the `next` linked-list pointer.  The `next`
           field is only populated by SWIG_InitializeModule, which we
           skip in the wasm-js runtime.  Without init, `next == NULL`
           on every entry -- so `cast = cast->next` would terminate
           after the first entry, missing all the casts we need. */
        swig_cast_info *cast = type->cast;
        while (cast && cast->type && cast->type->name) {
          if (vtag == cast->type->name) {
            type_ok = true;
            if (cast->converter) {
              int newmemory = 0;
              p = cast->converter(p, &newmemory);
            }
            break;
          }
          ++cast;
        }
      }
    }
  }
  v.release_ownership();
  if (!type_ok) return SWIG_ERROR;
  if (pptr) *pptr = p;
  if (own)  *own  = 0;
  return SWIG_OK;
}

SWIGRUNTIME int
SWIG_WASMJS_ConvertPtr(EM_VAL obj, void **pptr, swig_type_info *type, int flags) {
  return SWIG_WASMJS_ConvertPtrAndOwn(obj, pptr, type, flags, 0);
}

/* Wrap a raw C++ pointer in a JS-side proxy carrier: a plain JS object
   `{_ptr: <int>}` whose handle we return as an EM_VAL.  The JS-side
   marshal layer (__from_handle in f_js_pre) calls __swig_release_handle
   to recover the object and read `_ptr`. */
SWIGRUNTIME EM_VAL
SWIG_WASMJS_NewPointerObj(void *ptr, swig_type_info *SWIGUNUSEDPARM(type), int SWIGUNUSEDPARM(flags)) {
  if (!ptr) return 0;
  emscripten::val obj = emscripten::val::object();
  obj.set("_ptr", emscripten::val(reinterpret_cast<uintptr_t>(ptr)));
  return obj.release_ownership();
}

/* Embind translator: bridges JS-level objects to EM_VAL handles that
   raw EMSCRIPTEN_KEEPALIVE wrappers can consume.  Without going all-in
   on embind function() registration for every wrapped C++ function,
   we expose ONE translator pair that JS calls to convert a JS value
   <-> EM_VAL handle (the i32 index into Embind's value table).

   Usage from JS:
     const h = Module.__swig_take_handle(jsObj);
     Module._swig_foo(h);          // wasm receives the EM_VAL via i32
     // h is consumed by the wasm side (val::take_ownership decrefs on
     // scope exit), so JS must NOT decref / use h after the call.

   And the reverse for returns:
     const val = Module.__swig_release_handle(h);  // h came from wasm */
/* Embind's `function()` binding system doesn't know about EM_VAL --
   it's the value-table handle type embind uses internally and isn't
   registered as a wire type.  Pass the handle as `uintptr_t` instead;
   same i32 wire format, just a type embind already understands. */
SWIGINTERN uintptr_t swig_wasmjs_take_handle(emscripten::val v) {
  return reinterpret_cast<uintptr_t>(v.release_ownership());
}
SWIGINTERN emscripten::val swig_wasmjs_release_handle(uintptr_t e) {
  return emscripten::val::take_ownership(reinterpret_cast<EM_VAL>(e));
}
EMSCRIPTEN_BINDINGS(swig_wasmjs_runtime) {
  emscripten::function("__swig_take_handle",    &swig_wasmjs_take_handle);
  emscripten::function("__swig_release_handle", &swig_wasmjs_release_handle);
}

/* SWIG_ErrorType + SWIG_DirOutFail: dependencies of `%dirout_fail`, which
   the standard directorin/directorout typemap bodies (see casadi.i's
   `%casadi_input_typemaps` / `%casadi_output_typemaps`) expand to.
   Matches the body matlab/python use, just with our own DirectorTypeMismatchException
   declared in `Lib/wasm_js/director.swg`.  We define these in the runtime
   header (rather than %include-ing `<typemaps/swigtypemaps.swg>`) so the
   wasm-js target stays lean -- the standard SWIGTYPE directorin/out bodies
   in swigtypemaps.swg target generic mxArray/PyObject signatures and we'd
   have to override most of them anyway. */
SWIGRUNTIME const char* SWIG_ErrorType(int code) {
  switch (code) {
    case SWIG_MemoryError:      return "SWIG:MemoryError";
    case SWIG_IOError:          return "SWIG:IOError";
    case SWIG_RuntimeError:     return "SWIG:RuntimeError";
    case SWIG_IndexError:       return "SWIG:IndexError";
    case SWIG_TypeError:        return "SWIG:TypeError";
    case SWIG_DivisionByZero:   return "SWIG:DivisionByZero";
    case SWIG_OverflowError:    return "SWIG:OverflowError";
    case SWIG_SyntaxError:      return "SWIG:SyntaxError";
    case SWIG_ValueError:       return "SWIG:ValueError";
    case SWIG_SystemError:      return "SWIG:SystemError";
    case SWIG_AttributeError:   return "SWIG:AttributeError";
    case SWIG_NullReferenceError: return "SWIG:NullReferenceError";
    default:                    return "SWIG:UnknownError";
  }
}

#define SWIG_DirOutFail(code, msg) \
  Swig::DirectorTypeMismatchException::raise(SWIG_ErrorType(code), msg)

/* Stubs for SwigModule machinery referenced by swigrun.swg when
   SWIG_MODULE_CLIENTDATA_TYPE is defined. */
static swig_module_info swig_wasmjs_module = { 0, 0, 0, 0, 0, 0 };

SWIGRUNTIME swig_module_info *
SWIG_WASMJS_GetModule(void *SWIGUNUSEDPARM(clientdata)) {
  return &swig_wasmjs_module;
}

SWIGRUNTIME void
SWIG_WASMJS_SetModule(void *SWIGUNUSEDPARM(clientdata), swig_module_info *pointer) {
  swig_wasmjs_module = *pointer;
}

#define SWIG_GetModule(clientdata)            SWIG_WASMJS_GetModule(clientdata)
#define SWIG_SetModule(clientdata, pointer)   SWIG_WASMJS_SetModule(clientdata, pointer)
