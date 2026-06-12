/* -----------------------------------------------------------------------------
 * julia.cxx
 *
 * Julia language module for SWIG.  Emits a flat extern-"C" C++ wrapper
 * (consumed via ccall) plus a Julia module source file (<module>.jl) with
 * proxy types, finalizers and typed methods.  Overloads map onto Julia's
 * multiple dispatch; no arity/probe dispatchers are generated.
 *
 * Error protocol: every wrapper clears a thread_local error slot on entry
 * and fills it from catch(...); the Julia side checks the slot after each
 * ccall (raising target-side keeps C++ unwinding sane -- never longjmp/
 * jl_throw across C++ frames).
 * ----------------------------------------------------------------------------- */

#include "swigmod.h"

class JULIA : public Language {
  File   *f_begin;
  String *f_runtime;
  String *f_header;
  String *f_wrappers;
  String *f_init;
  String *f_jl;          /* generated Julia module source */
  String *f_jl_body;
  String *f_jl_exports;  /* names to export from the module */

  String *module_name;
  String *class_jlname;  /* sym:name of the class being processed, 0 outside */
  String *class_cname;   /* fully qualified C++ name */
  bool    class_has_ctor;
  int     n_skipped;

public:
  JULIA() : f_begin(0), f_runtime(0), f_header(0), f_wrappers(0), f_init(0),
            f_jl(0), f_jl_body(0), f_jl_exports(0), module_name(0),
            class_jlname(0), class_cname(0), class_has_ctor(false),
            n_skipped(0) {}

  virtual void main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SWIG_library_directory("julia");
    Preprocessor_define("SWIGJULIA 1", 0);
    SWIG_config_file("julia.swg");
    allow_overloading();
  }

  /* ---- type mapping (hardcoded primitives for the skeleton; the typemap
     subsystem lands when casadi.i consumption starts) ---- */

  /* Returns the C type used at the extern-"C" boundary, or 0 if the type
     is not yet supported.  `resolved` should be SwigType_typedef_resolve_all'd. */
  const char *boundary_ctype(SwigType *t, bool is_return) {
    SwigType *r = SwigType_typedef_resolve_all(t);
    const char *res = 0;
    if (SwigType_isreference(r) || SwigType_ispointer(r)) {
      SwigType *base = Copy(r);
      if (SwigType_isreference(base)) SwigType_del_reference(base);
      else SwigType_del_pointer(base);
      String *bstr = SwigType_str(base, 0);
      bool is_str = Strstr(bstr, "std::string") != 0;
      bool is_char = Strcmp(SwigType_base(base), "char") == 0;
      Delete(bstr);
      if (is_char) res = is_return ? "char *" : "const char *";
      else if (is_str) res = is_return ? "char *" : "const char *";
      else if (is_class_type(base)) res = "void *";
      Delete(base);
      Delete(r);
      return res;
    }
    String *s = SwigType_str(r, 0);
    if (Strcmp(s, "double") == 0 || Strcmp(s, "float") == 0) res = "double";
    else if (Strcmp(s, "bool") == 0) res = "unsigned char";
    else if (Strcmp(s, "int") == 0 || Strcmp(s, "long") == 0
          || Strcmp(s, "long long") == 0 || Strcmp(s, "short") == 0
          || Strcmp(s, "size_t") == 0 || Strcmp(s, "unsigned int") == 0
          || Strcmp(s, "unsigned long") == 0 || Strcmp(s, "casadi_int") == 0)
      res = "long long";
    else if (Strstr(s, "std::string")) res = is_return ? "char *" : "const char *";
    else if (Strcmp(s, "void") == 0 && is_return) res = "void";
    else if (is_class_type(r)) res = "void *";
    Delete(s);
    Delete(r);
    return res;
  }

  bool is_class_type(SwigType *t) {
    SwigType *r = SwigType_typedef_resolve_all(t);
    Node *cls = classLookup(r);
    Delete(r);
    return cls != 0;
  }

  /* Julia-side ccall arg type for a boundary ctype. */
  const char *jl_ctype(const char *bt) {
    if (!bt) return 0;
    if (Strcmp(bt, "double") == 0) return "Cdouble";
    if (Strcmp(bt, "long long") == 0) return "Clonglong";
    if (Strcmp(bt, "unsigned char") == 0) return "Cuchar";
    if (Strcmp(bt, "const char *") == 0) return "Cstring";
    if (Strcmp(bt, "char *") == 0) return "Ptr{UInt8}";
    if (Strcmp(bt, "void *") == 0) return "Ptr{Cvoid}";
    if (Strcmp(bt, "void") == 0) return "Cvoid";
    return 0;
  }

  /* Julia-side user-facing parameter type annotation. */
  String *jl_param_type(SwigType *t) {
    SwigType *r = SwigType_typedef_resolve_all(t);
    String *res = 0;
    Node *cls = 0;
    SwigType *base = Copy(r);
    if (SwigType_isreference(base)) SwigType_del_reference(base);
    else if (SwigType_ispointer(base)) SwigType_del_pointer(base);
    cls = classLookup(base);
    if (cls) {
      res = Copy(Getattr(cls, "sym:name"));
    } else {
      String *s = SwigType_str(r, 0);
      if (Strcmp(s, "double") == 0 || Strcmp(s, "float") == 0) res = NewString("Real");
      else if (Strcmp(s, "bool") == 0) res = NewString("Bool");
      else if (Strstr(s, "std::string") || Strstr(s, "char")) res = NewString("AbstractString");
      else res = NewString("Integer");
      Delete(s);
    }
    Delete(base);
    Delete(r);
    return res;
  }

  /* ---- top ---- */

  virtual int top(Node *n) {
    module_name = Copy(Getattr(n, "name"));
    String *outfile = Getattr(n, "outfile");
    f_begin = NewFile(outfile, "w", SWIG_output_files());
    if (!f_begin) { FileErrorDisplay(outfile); Exit(EXIT_FAILURE); }
    f_runtime  = NewString("");
    f_header   = NewString("");
    f_wrappers = NewString("");
    f_init     = NewString("");
    f_jl_body    = NewString("");
    f_jl_exports = NewString("");

    Swig_register_filebyname("begin",   f_begin);
    Swig_register_filebyname("runtime", f_runtime);
    Swig_register_filebyname("header",  f_header);
    Swig_register_filebyname("wrapper", f_wrappers);
    Swig_register_filebyname("init",    f_init);

    Swig_banner(f_begin);
    Printf(f_runtime,
      "#include <string>\n#include <cstring>\n#include <cstdlib>\n"
      "#include <exception>\n\n"
      "static thread_local int  swig_jl_err_code = 0;\n"
      "static thread_local std::string swig_jl_err_msg;\n"
      "#define SWIG_JL_ENTER() do { swig_jl_err_code = 0; } while (0)\n"
      "#define SWIG_JL_CATCH(ret) \\\n"
      "  catch (const std::exception& e) { swig_jl_err_code = 1; swig_jl_err_msg = e.what(); return ret; } \\\n"
      "  catch (...) { swig_jl_err_code = 1; swig_jl_err_msg = \"unknown C++ exception\"; return ret; }\n"
      "extern \"C\" {\n"
      "int swig_jl_last_error_code() { return swig_jl_err_code; }\n"
      "const char* swig_jl_last_error_msg() { return swig_jl_err_msg.c_str(); }\n"
      "char* swig_jl_strdup(const std::string& s) { char* p = (char*)malloc(s.size()+1); memcpy(p, s.c_str(), s.size()+1); return p; }\n"
      "void swig_jl_str_free(char* p) { free(p); }\n"
      "}\n\n");

    Language::top(n);

    /* assemble C++ wrapper */
    Dump(f_runtime, f_begin);
    Dump(f_header, f_begin);
    Dump(f_wrappers, f_begin);
    Dump(f_init, f_begin);

    /* assemble Julia module next to the C++ output */
    String *jlfile = NewStringf("%s%s.jl", SWIG_output_directory(), module_name);
    File *jf = NewFile(jlfile, "w", SWIG_output_files());
    if (!jf) { FileErrorDisplay(jlfile); Exit(EXIT_FAILURE); }
    Printf(jf, "# Generated by SWIG -julia. Do not edit.\n");
    Printf(jf, "module %s\n\n", module_name);
    Printf(jf, "const _lib = joinpath(@__DIR__, \"lib%s_wrap.so\")\n\n", module_name);
    Printf(jf,
      "struct SwigError <: Exception\n    msg::String\nend\n"
      "Base.showerror(io::IO, e::SwigError) = print(io, \"%s error: \", e.msg)\n\n"
      "function _check(v)\n"
      "    if ccall((:swig_jl_last_error_code, _lib), Cint, ()) != 0\n"
      "        throw(SwigError(unsafe_string(ccall((:swig_jl_last_error_msg, _lib), Cstring, ()))))\n"
      "    end\n    v\nend\n\n"
      "function _takestr(p::Ptr{UInt8})\n"
      "    _check(p)\n    s = unsafe_string(p)\n"
      "    ccall((:swig_jl_str_free, _lib), Cvoid, (Ptr{UInt8},), p)\n    s\nend\n\n",
      module_name);
    Dump(f_jl_body, jf);
    if (Len(f_jl_exports) > 0) Printf(jf, "export %s\n", f_jl_exports);
    Printf(jf, "\nend # module %s\n", module_name);
    Delete(jf);

    if (n_skipped > 0)
      Printf(stderr, "swig -julia: skipped %d declaration(s) with unsupported types (marked in output)\n", n_skipped);

    Delete(f_begin);
    return SWIG_OK;
  }

  /* ---- shared emission for free functions / methods / ctors ---- */

  void add_export(const String *name) {
    if (Strstr(f_jl_exports, name)) return;
    if (Len(f_jl_exports) > 0) Printv(f_jl_exports, ", ", NIL);
    Printv(f_jl_exports, name, NIL);
  }

  /* kind: 0 free fn, 1 member method, 2 constructor, 3 static method */
  int emit_callable(Node *n, int kind) {
    String *symname = Getattr(n, "sym:name");
    String *overname = Getattr(n, "sym:overname");  /* "__SWIG_0" or 0 */
    SwigType *returntype = (kind == 2) ? 0 : Getattr(n, "type");
    ParmList *parms = Getattr(n, "parms");

    /* boundary signature check */
    const char *ret_bt = (kind == 2) ? "void *" : boundary_ctype(returntype, true);
    if (!ret_bt) { skip(n, "return type"); return SWIG_NOWRAP; }
    for (Parm *p = parms; p; p = nextSibling(p)) {
      if (!boundary_ctype(Getattr(p, "type"), false)) { skip(n, "parameter type"); return SWIG_NOWRAP; }
    }

    String *wname = NewStringf("_swig_%s_%s%s",
        class_jlname ? Char(class_jlname) : "g", Char(symname),
        overname ? Char(overname) : "");

    /* ---- C++ side ---- */
    String *cargs = NewString("");   /* extern C parameter list */
    String *cconv = NewString("");   /* arg conversion statements */
    String *ccall_args = NewString(""); /* args for the C++ call */
    int idx = 0;
    if (kind == 1) Printf(cargs, "void *self");
    for (Parm *p = parms; p; p = nextSibling(p), ++idx) {
      SwigType *pt = Getattr(p, "type");
      const char *bt = boundary_ctype(pt, false);
      if (Len(cargs) > 0) Printf(cargs, ", ");
      Printf(cargs, "%s a%d", bt, idx);
      if (Len(ccall_args) > 0) Printf(ccall_args, ", ");
      SwigType *r = SwigType_typedef_resolve_all(pt);
      if (Strcmp(bt, "void *") == 0) {
        SwigType *base = Copy(r);
        if (SwigType_isreference(base)) SwigType_del_reference(base);
        else if (SwigType_ispointer(base)) SwigType_del_pointer(base);
        String *bstr = SwigType_str(base, 0);
        if (SwigType_ispointer(r)) Printf(ccall_args, "static_cast<%s*>(a%d)", bstr, idx);
        else Printf(ccall_args, "*static_cast<%s*>(a%d)", bstr, idx);
        Delete(bstr); Delete(base);
      } else if (Strcmp(bt, "const char *") == 0 && Strstr(SwigType_str(r, 0), "std::string")) {
        Printf(cconv, "  std::string arg%d(a%d);\n", idx, idx);
        Printf(ccall_args, "arg%d", idx);
      } else if (Strcmp(bt, "unsigned char") == 0) {
        Printf(ccall_args, "a%d != 0", idx);
      } else {
        Printf(ccall_args, "a%d", idx);
      }
      Delete(r);
    }

    String *fail_ret = NewString("");
    if (Strcmp(ret_bt, "void") != 0)
      Printf(fail_ret, "%s", Strcmp(ret_bt, "double") == 0 ? "0.0" :
             (Strcmp(ret_bt, "void *") == 0 || Strstr(ret_bt, "char *")) ? "0" : "0");

    String *callexpr = NewString("");
    if (kind == 2) {
      Printf(callexpr, "new %s(%s)", class_cname, ccall_args);
    } else if (kind == 1) {
      Printf(callexpr, "static_cast<%s*>(self)->%s(%s)", class_cname, Getattr(n, "name"), ccall_args);
    } else if (kind == 3) {
      Printf(callexpr, "%s::%s(%s)", class_cname, Getattr(n, "name"), ccall_args);
    } else {
      Printf(callexpr, "%s(%s)", Getattr(n, "name"), ccall_args);
    }

    Printf(f_wrappers, "extern \"C\" %s %s(%s) {\n  SWIG_JL_ENTER();\n  try {\n%s",
           ret_bt, wname, Len(cargs) ? Char(cargs) : "void", cconv);
    if (kind == 2) {
      Printf(f_wrappers, "    return %s;\n", callexpr);
    } else if (Strcmp(ret_bt, "void") == 0) {
      Printf(f_wrappers, "    %s;\n    return;\n", callexpr);
    } else if (Strcmp(ret_bt, "void *") == 0) {
      SwigType *rr = SwigType_typedef_resolve_all(returntype);
      if (SwigType_ispointer(rr) || SwigType_isreference(rr)) {
        SwigType *base = Copy(rr);
        if (SwigType_isreference(base)) SwigType_del_reference(base); else SwigType_del_pointer(base);
        String *bstr = SwigType_str(base, 0);
        Printf(f_wrappers, "    return (void*)new %s(%s%s);\n", bstr,
               SwigType_isreference(rr) ? "" : "*", callexpr);
        Delete(bstr); Delete(base);
      } else {
        String *bstr = SwigType_str(rr, 0);
        Printf(f_wrappers, "    return (void*)new %s(%s);\n", bstr, callexpr);
        Delete(bstr);
      }
      Delete(rr);
    } else if (Strstr(ret_bt, "char *")) {
      Printf(f_wrappers, "    return swig_jl_strdup(%s);\n", callexpr);
    } else if (Strcmp(ret_bt, "unsigned char") == 0) {
      Printf(f_wrappers, "    return (%s) ? 1 : 0;\n", callexpr);
    } else {
      Printf(f_wrappers, "    return (%s)(%s);\n", ret_bt, callexpr);
    }
    Printf(f_wrappers, "  } SWIG_JL_CATCH(%s)\n}\n\n", Len(fail_ret) ? Char(fail_ret) : "");

    /* ---- Julia side ---- */
    String *jargs = NewString("");      /* typed parameter list */
    String *jccall_types = NewString(""); /* ccall type tuple */
    String *jccall_args = NewString("");
    String *preserve = NewString("");
    if (kind == 1) {
      Printf(jccall_types, "Ptr{Cvoid}, ");
      Printf(jccall_args, "self.ptr, ");
      Printf(preserve, "self ");
    }
    idx = 0;
    for (Parm *p = parms; p; p = nextSibling(p), ++idx) {
      SwigType *pt = Getattr(p, "type");
      const char *bt = boundary_ctype(pt, false);
      String *jt = jl_param_type(pt);
      String *pname = Getattr(p, "name");
      String *an = (pname && Len(pname) > 0 && !Strstr(pname, "arg")) ?
          NewStringf("%s", pname) : NewStringf("a%d", idx);
      if (Len(jargs) > 0) Printf(jargs, ", ");
      Printf(jargs, "%s::%s", an, jt);
      Printf(jccall_types, "%s, ", jl_ctype(bt));
      if (Strcmp(bt, "void *") == 0) {
        Printf(jccall_args, "%s.ptr, ", an);
        Printf(preserve, "%s ", an);
      } else {
        Printf(jccall_args, "%s, ", an);
      }
      Delete(jt); Delete(an);
    }
    /* trailing comma handling: ccall type tuples need a trailing comma for 1-tuples; always emitting one is valid */

    const char *jret = jl_ctype(ret_bt);
    String *callcore = NewStringf(
        "ccall((:%s, _lib), %s, (%s), %s)",
        wname, (kind == 2) ? "Ptr{Cvoid}" : jret, jccall_types, jccall_args);

    String *jlfn = NewString("");
    String *fname = 0;
    if (kind == 2) {
      fname = Copy(class_jlname);
      Printf(jlfn, "%s(%s) = %s(_check(%s))\n", fname, jargs, class_jlname, callcore);
    } else {
      fname = Copy(symname);
      String *self_sig = (kind == 1) ? NewStringf("self::%s%s", class_jlname, Len(jargs) ? ", " : "")
                       : (kind == 3) ? NewStringf("::Type{%s}%s", class_jlname, Len(jargs) ? ", " : "")
                       : NewString("");
      String *body = 0;
      SwigType *rr = returntype ? SwigType_typedef_resolve_all(returntype) : 0;
      if (Strcmp(ret_bt, "void *") == 0) {
        Node *cls = 0;
        SwigType *base = Copy(rr);
        if (SwigType_isreference(base)) SwigType_del_reference(base);
        else if (SwigType_ispointer(base)) SwigType_del_pointer(base);
        cls = classLookup(base);
        body = NewStringf("%s(_check(%s))", cls ? Getattr(cls, "sym:name") : "Ptr", callcore);
        Delete(base);
      } else if (Strstr(ret_bt, "char *")) {
        body = NewStringf("_takestr(%s)", callcore);
      } else if (Strcmp(ret_bt, "unsigned char") == 0) {
        body = NewStringf("_check(%s) != 0", callcore);
      } else if (Strcmp(ret_bt, "void") == 0) {
        body = NewStringf("(%s; _check(nothing))", callcore);
      } else if (Strcmp(ret_bt, "long long") == 0) {
        body = NewStringf("Int(_check(%s))", callcore);
      } else {
        body = NewStringf("_check(%s)", callcore);
      }
      if (Len(preserve) > 0)
        Printf(jlfn, "%s(%s%s) = GC.@preserve %sbegin %s end\n", fname, self_sig, jargs, preserve, body);
      else
        Printf(jlfn, "%s(%s%s) = %s\n", fname, self_sig, jargs, body);
      Delete(self_sig); Delete(body);
      if (rr) Delete(rr);
    }
    Printv(f_jl_body, jlfn, NIL);
    if (kind == 0 || kind == 3) add_export(fname);
    Delete(jlfn); Delete(fname);
    Delete(callcore); Delete(jargs); Delete(jccall_types); Delete(jccall_args);
    Delete(preserve); Delete(wname); Delete(cargs); Delete(cconv);
    Delete(ccall_args); Delete(callexpr); Delete(fail_ret);
    return SWIG_OK;
  }

  void skip(Node *n, const char *why) {
    ++n_skipped;
    Printf(f_jl_body, "# skipped %s (%s not yet supported)\n",
           Getattr(n, "sym:name"), why);
  }

  /* ---- handlers ---- */

  virtual int globalfunctionHandler(Node *n) {
    emit_callable(n, 0);
    return SWIG_OK;
  }

  virtual int classHandler(Node *n) {
    class_jlname = Getattr(n, "sym:name");
    class_cname  = Getattr(n, "name");
    class_has_ctor = false;

    Printf(f_jl_body,
      "mutable struct %s\n    ptr::Ptr{Cvoid}\n"
      "    function %s(p::Ptr{Cvoid})\n"
      "        x = new(p)\n"
      "        finalizer(o -> (ccall((:_swig_%s_delete, _lib), Cvoid, (Ptr{Cvoid},), o.ptr); o.ptr = C_NULL), x)\n"
      "        x\n    end\nend\n",
      class_jlname, class_jlname, class_jlname);
    add_export(class_jlname);

    Printf(f_wrappers,
      "extern \"C\" void _swig_%s_delete(void *p) {\n"
      "  SWIG_JL_ENTER();\n  try { delete static_cast<%s*>(p); } SWIG_JL_CATCH()\n}\n\n",
      class_jlname, class_cname);

    Language::classHandler(n);

    class_jlname = 0;
    class_cname = 0;
    return SWIG_OK;
  }

  virtual int constructorHandler(Node *n) {
    emit_callable(n, 2);
    return SWIG_OK;
  }

  virtual int memberfunctionHandler(Node *n) {
    emit_callable(n, 1);
    return SWIG_OK;
  }

  virtual int staticmemberfunctionHandler(Node *n) {
    emit_callable(n, 3);
    return SWIG_OK;
  }

  virtual int destructorHandler(Node *) { return SWIG_OK; } /* emitted in classHandler */
};

static Language *new_swig_julia() { return new JULIA(); }
extern "C" Language *swig_julia(void) { return new_swig_julia(); }
