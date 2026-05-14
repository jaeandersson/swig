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

/* Val-based ConvertPtr.  $input arrives as an EM_VAL handle for a JS
   value -- typically a SWIG proxy instance carrying a `_ptr` property
   (the underlying C++ pointer).  Borrow ownership (take + release) so
   the caller's refcount is preserved across the peek. */
SWIGRUNTIME int
SWIG_WASMJS_ConvertPtrAndOwn(EM_VAL obj, void **pptr, swig_type_info *SWIGUNUSEDPARM(type),
                             int SWIGUNUSEDPARM(flags), int *own) {
  if (!obj) return SWIG_ERROR;
  emscripten::val v = emscripten::val::take_ownership(obj);
  bool ok = v.hasOwnProperty("_ptr");
  uintptr_t raw = ok ? v["_ptr"].as<uintptr_t>() : 0;
  v.release_ownership();
  if (!ok) return SWIG_ERROR;
  if (pptr) *pptr = reinterpret_cast<void*>(raw);
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
