/* wasm_jsuserdir.swg
 *
 *  Compatibility shims for the python-target %stub_* macros (defined in
 *  Lib/python/pyuserdir.swg).  Casadi.i drives stub emission via two
 *  channels:
 *    (1) Per-typemap `pystub_in=` / `pystub_out=` attributes -- consumed
 *        automatically by wasm_js.cxx::stub_emit_function via
 *        pick_ts_stub() and converted to TypeScript.
 *    (2) Hand-written `%stub_method` / `%stub_func` / `%stub_attr` etc.
 *        invocations with Python-syntax bodies (`def name(...) -> ...`).
 *
 *  Channel (2) is python-only -- its bodies emit `def ... -> ...:` lines
 *  that aren't valid TypeScript.  Under -wasm-js we make them no-ops
 *  so they don't pollute the .d.ts.  Auto-generated stubs from (1)
 *  cover the bulk of public API.
 *
 *  The `%stub_alias_in` / `%stub_alias_out` macros DO carry useful
 *  cross-language widening info (input-position unions, e.g.
 *  "DM = bool | int | DM | Sequence[float]").  We emit them into the
 *  same stubs_alias_in_table / stubs_alias_out_table file slots so
 *  wasm_js.cxx can pick them up in future via the resolveStub
 *  protocol -- but for now they're parked there unread. */

#define %stubcode    %insert("stubs")

#ifdef SWIG_STUBS_ENABLED

/* Widening tables -- same slot names as python.cxx for cross-target
   compatibility.  Bodies are language-neutral `NAME=TYPES` strings. */
%define %stub_alias_in(NAME, TYPES...)
%insert("stubs_preamble") %{_##NAME = TYPES
%}
%insert("stubs_alias_in_table") %{NAME=_##NAME
%}
%enddef

%define %stub_alias_out(NAME, TYPES...)
%insert("stubs_alias_out_table") %{NAME=_##NAME
%}
%enddef

%define %stub_alias_in_key(KEY, TYPES...)
%insert("stubs_alias_in_table") %{KEY=TYPES
%}
%enddef

%define %stub_alias_out_key(KEY, TYPES...)
%insert("stubs_alias_out_table") %{KEY=TYPES
%}
%enddef

/* Hand-written python-syntax stubs: skip under wasm_js.  TypeScript
   counterparts could be added later via a wasm-js-specific macro
   family (`%dts_method` etc.). */
%define %stub_method(NAME, RET, ARGS...) %enddef
%define %stub_method0(NAME, RET) %enddef
%define %stub_overload_method(NAME, RET, ARGS...) %enddef
%define %stub_overload_method0(NAME, RET) %enddef
%define %stub_overload_method_selftyped(NAME, RET, SELFTYPE, ARGS...) %enddef
%define %stub_func(NAME, RET, ARGS...) %enddef
%define %stub_overload_func(NAME, RET, ARGS...) %enddef
%define %stub_attr(NAME, TYPE...) %enddef
%define %stub_const(NAME, TYPE...) %enddef
%define %stub_class_begin(NAME) %enddef

#else

/* -stubs not enabled: every macro is a no-op (matches the
   python.cxx pyuserdir.swg #else branch). */
%define %stub_alias_in(NAME, TYPES...) %enddef
%define %stub_alias_out(NAME, TYPES...) %enddef
%define %stub_alias_in_key(KEY, TYPES...) %enddef
%define %stub_alias_out_key(KEY, TYPES...) %enddef
%define %stub_method(NAME, RET, ARGS...) %enddef
%define %stub_method0(NAME, RET) %enddef
%define %stub_overload_method(NAME, RET, ARGS...) %enddef
%define %stub_overload_method0(NAME, RET) %enddef
%define %stub_overload_method_selftyped(NAME, RET, SELFTYPE, ARGS...) %enddef
%define %stub_func(NAME, RET, ARGS...) %enddef
%define %stub_overload_func(NAME, RET, ARGS...) %enddef
%define %stub_attr(NAME, TYPE...) %enddef
%define %stub_const(NAME, TYPE...) %enddef
%define %stub_class_begin(NAME) %enddef

#endif
