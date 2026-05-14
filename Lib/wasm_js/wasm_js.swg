/* wasm_js.swg
 *
 * Typemap library for the -wasm-js target. Uses SWIG's STANDARD typemap
 * names (in / out / freearg / ctype) so existing SWIG library files
 * (`Lib/typemaps/std_string.swg`, etc.) can be %include'd without
 * modification by users.
 *
 * Each wrapped function emits a wasm-exported C function with the shape:
 *
 *    EMSCRIPTEN_KEEPALIVE <ret_ctype> swig_foo(<in_ctype>0 obj0, <in_ctype>1 obj1, ...) {
 *      T0 arg1; T1 arg2; ...;     // C++ locals
 *      <in typemap body>:         // populates argN from objN
 *        arg1 = std::string(obj0);   // example for std::string
 *      ResultType result;
 *      result = call(arg1, arg2, ...);
 *      <out typemap body>:        // converts result -> _outv
 *        char* __buf = malloc(...); memcpy(...); _outv = __buf;
 *      <freearg blocks>           // cleanup, if any
 *      return _outv;
 *    }
 *
 * Placeholders SWIG auto-substitutes during Swig_typemap_attach_parms:
 *   $1, $1_type, $1_ltype, $1_name      <- the C++ local (lname=arg<N>)
 *   $argnum                             <- parameter index
 * The wasm_js module post-substitutes:
 *   $input    <- C-API input variable (e.g. "obj0")
 *   $result   <- C-API output variable (e.g. "_outv")
 *   $call     <- inside JS-side typemaps, the wasm call expression
 *
 * The JS-side conversions (jsin/jsarg/jsfree/jsout) live below in
 * wasm_js-specific typemap names; SWIG has no standard equivalent
 * because the JS proxy is unique to this backend.
 */

%insert("header") %{
#include <emscripten.h>
#include <cstdlib>
#include <cstring>
#include <string>
%}

// Pull in the SWIG runtime: swigrun.swg (swig_type_info etc.) +
// swigerrors.swg + wasm_jsrun.swg (SWIG_ConvertPtr / SWIG_NewPointerObj
// as identity casts).  The runtime block is emitted before the wrappers
// in wasm_js.cxx::top().
%include <wasm_jsruntime.swg>

// Argout support: %append_output is invoked from `argout` typemap bodies
// (e.g. casadi.i's `xType &OUTPUT` produces
// `%append_output(casadi::from_ptr($1))`).  For wasm-js we collect
// outputs into a fixed-size local array `_argouts[]` declared by the
// codegen in wasm_js.cxx::cpp_call_body; the wrapper packs them into
// the return.  Single-output case: wrapper directly returns `_argouts[0]`
// as `void*`.  Multi-output: wrapper returns a heap-allocated `void**`.
%define %append_output(X)
_argouts[_argout_idx++] = (void*)(X);
%enddef

// %stub_* macros (python-target's stub-emission primitives) declared as
// no-ops under -wasm-js -- the auto-generator in wasm_js.cxx picks
// up `pystub_in`/`pystub_out` typemap attrs instead.  Loaded
// unconditionally so casadi.i's %stub_alias_in / %stub_method calls
// don't error out.
%include <wasm_jsuserdir.swg>

// Pull in fragment-name machinery and primitive AsVal/From stubs so that
// casadi.i's `%fragment("casadi_bool", "header", fragment=SWIG_AsVal_frag(bool))`
// etc. find what they expect.
%include <wasm_jsprimtypes.swg>

// ============================================================================
// std::string
// ============================================================================

%typemap(ctype) std::string, const std::string&, std::string& "const char*"

// Value: local is std::string, typemap assigns directly.
// `pystub_in="str"` lets the -stubs emitter render std::string params as
// `string` in TypeScript .d.ts (casadi.i gates its own std::string
// %casadi_typemaps registration out under SWIGWASMJS, so we carry the
// stub annotation here).
%typemap(in, tsstub_in="string") std::string %{ $1 = std::string($input); %}

// Reference: parm_prologue declared `std::string* arg<N>;` and SWIG wraps
// the body in a per-parm { } block.  The temp std::string MUST live at
// function scope, not inside that block — otherwise it's destroyed before
// the C++ call and arg<N> dangles.  SSO masks this for short strings;
// heap-allocated long strings dereference freed memory and return junk
// (e.g. allocator free-pattern bytes via name.size()).
//
// Use SWIG's local-variable typemap declaration `(std::string tmp$argnum)`
// so `tmp$argnum` lives at function scope outside the parm block.  Suffix
// with $argnum so multiple string parms don't shadow each other.
// Multi-pattern + `(T m)` locals: GRAMMAR QUIRK — the (T m) only
// attaches to the IMMEDIATELY PRECEDING pattern (per typemap_parm in
// parser.y).  To get locals on BOTH patterns, repeat per pattern.
// Then wasm_js.cxx::parm_prologue mangles `m` -> `m<argnum>` per
// parm position so multi-string-arg methods don't collide.
%typemap(in, tsstub_in="string") const std::string& (std::string m), std::string& (std::string m) %{
  m = std::string($input);
  $1 = &m;
%}

// Value return: $1 is a std::string (declared `std::string result;`).
%typemap(out, tsstub_out="string") std::string %{
  {
    char* __buf = (char*)malloc($1.size() + 1);
    memcpy(__buf, $1.c_str(), $1.size() + 1);
    $result = __buf;
  }
%}

// Reference return: wasm_js.cxx's cpp_call_body declares `result` as
// `std::string*` (via SwigType_ltype), so $1 is `std::string*`.  Deref
// before reading.
%typemap(out, tsstub_out="string") const std::string&, std::string& %{
  {
    char* __buf = (char*)malloc($1->size() + 1);
    memcpy(__buf, $1->c_str(), $1->size() + 1);
    $result = __buf;
  }
%}

// JS side: stringToUTF8 into a malloc'd buffer; free after the call.
%typemap(jsin) std::string, const std::string&, std::string& %{
      const __nps$argnum = M.lengthBytesUTF8($input);
      const __ps$argnum  = M._malloc(__nps$argnum + 1);
      M.stringToUTF8($input, __ps$argnum, __nps$argnum + 1);
%}
%typemap(jsarg)  std::string, const std::string&, std::string& "__ps$argnum"
%typemap(jsfree) std::string, const std::string&, std::string& %{
      M._free(__ps$argnum);
%}
%typemap(jsout) std::string, const std::string&, std::string&
"(()=>{const __p=$call;const __s=M.UTF8ToString(__p);M._free(__p);return __s;})()"

// ============================================================================
// bool — round-trip through int for clean 0/1 across the wasm boundary
// ============================================================================

%typemap(ctype) bool "int"
%typemap(in)    bool %{ $1 = ($input) ? true : false; %}
%typemap(out)   bool %{ $result = ($1) ? 1 : 0; %}
%typemap(jsout) bool "(!!($call))"

// ============================================================================
// %wasm_vec(T, NAME) — JS-side `Array <-> XVector` autoconversion typemaps
// ============================================================================
//
// Users only ever pass / receive plain JS arrays.  The internal
// `NameVector` proxy class is a SWIG implementation detail.  The four
// jsin/jsarg/jsfree/jsout typemaps emitted by this macro handle:
//
//   - INPUT:  jsin builds a temp XVector from a JS array;
//             jsarg passes the temp's pointer;
//             jsfree deletes the temp.
//   - OUTPUT: jsout drains the wasm-returned XVector into a JS array
//             via __vec_to_arr (helper emitted in f_js_pre).
//
// Invoked from casadi.i right after each `%template(NameVector)
// std::vector<T>;` so the JS class name is statically available.
%define %wasm_vec(T, NAME)
%typemap(jsin) std::vector<T>, const std::vector<T>&, std::vector<T>& %{
      let __vp$argnum = $input;
      let __vown$argnum = false;
      if (Array.isArray($input)) {
        __vp$argnum = new NAME();
        __vown$argnum = true;
        for (const __x of $input) __vp$argnum.push_back(__x);
      }
%}
%typemap(jsarg) std::vector<T>, const std::vector<T>&, std::vector<T>& "__unwrap(__vp$argnum)"
%typemap(jsfree) std::vector<T>, const std::vector<T>&, std::vector<T>& %{
      if (__vown$argnum && __vp$argnum.delete) __vp$argnum.delete();
%}
%typemap(jsout) std::vector<T>, const std::vector<T>&, std::vector<T>&
"__vec_to_arr(new NAME(__PRIVATE_CTOR, __from_handle($call)))"
%enddef

// (Note: the previous `%wasm_vector(T)` macro -- an opaque-builder
// convention for vector types -- has been removed.  Vector marshaling
// now goes through casadi.i's `to_ptr<std::vector<M>**>` / `from_ptr`
// templates (which delegate to SWIG_ConvertPtr / SWIG_NewPointerObj),
// plus `%wasm_vec(T, NAME)` above for the JS-side Array <-> XVector
// autoconversion typemaps.)
