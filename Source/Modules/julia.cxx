/* -----------------------------------------------------------------------------
 * julia.cxx
 *
 * Julia language module for SWIG.  Emits a flat extern-"C" C++ wrapper
 * (consumed via ccall) plus a Julia module source file (<module>.jl) with
 * proxy types, finalizers and typed methods.  Overloads map onto Julia's
 * multiple dispatch; no arity/probe dispatchers are generated.
 *
 * Conversions are typemap-driven (Lib/julia/julia.swg): standard names
 * (ctype/in/out/freearg) shape the extern-"C" boundary; julia-prefixed
 * names (jltype/jlparam/jlout) shape the generated .jl methods.
 *
 * Error protocol: every wrapper clears a thread_local error slot on entry
 * and fills it from catch(...); the Julia side checks the slot after each
 * ccall (raising target-side keeps C++ unwinding sane -- never longjmp/
 * jl_throw across C++ frames).
 * ----------------------------------------------------------------------------- */

#include "swigmod.h"
#include <cctype>

class JULIA : public Language {
  File   *f_begin;
  String *f_runtime;
  String *f_header;
  String *f_wrappers;
  String *f_init;
  String *f_jl_types;
  String *f_jl_body;
  String *f_jl_exports;

  String *module_name;
  String *f_type_init;    /* body of _swig_jl_init_types() */
  Hash   *class_statics;  /* class jlname -> Hash of static fnames */
  Hash   *class_members;  /* class jlname -> List of method-line templates (@SELF@) */
  Hash   *class_bases;    /* class jlname -> List of base jlnames */
  String *class_jlname;   /* sym:name of class being processed, 0 outside */
  String *bare_symname;   /* unqualified member name captured pre-transform */
  bool    in_ctor, in_static;
  int     n_skipped;

public:
  JULIA() : f_begin(0), f_runtime(0), f_header(0), f_wrappers(0), f_init(0),
            f_jl_types(0), f_jl_body(0), f_jl_exports(0), module_name(0),
            f_type_init(0), class_statics(0), class_members(0), class_bases(0), class_jlname(0),
            bare_symname(0), in_ctor(false), in_static(false), n_skipped(0) {}

  virtual void main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SWIG_library_directory("julia");
    Preprocessor_define("SWIGJULIA 1", 0);
    SWIG_config_file("julia.swg");
    allow_overloading();
  }

  /* proxy name for a (possibly ref/ptr) class type, or 0 if not wrapped */
  String *proxy_name(SwigType *t) {
    SwigType *r = SwigType_typedef_resolve_all(t);
    SwigType *base = Copy(r);
    if (SwigType_isreference(base)) SwigType_del_reference(base);
    else if (SwigType_ispointer(base)) SwigType_del_pointer(base);
    if (SwigType_isqualifier(base)) SwigType_del_qualifier(base);
    Node *cls = classLookup(base);
    String *res = cls ? Copy(Getattr(cls, "sym:name")) : 0;
    Delete(base);
    Delete(r);
    return res;
  }

  virtual int top(Node *n) {
    module_name = Copy(Getattr(n, "name"));
    String *outfile = Getattr(n, "outfile");
    f_begin = NewFile(outfile, "w", SWIG_output_files());
    if (!f_begin) { FileErrorDisplay(outfile); Exit(EXIT_FAILURE); }
    f_runtime  = NewString("");
    f_header   = NewString("");
    f_wrappers = NewString("");
    f_init     = NewString("");
    f_type_init  = NewString("");
    f_jl_types   = NewString("");
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
      "static char* swig_jl_strdup(const std::string& s) { char* p = (char*)malloc(s.size()+1); memcpy(p, s.c_str(), s.size()+1); return p; }\n"
      "extern \"C\" {\n"
      "int swig_jl_last_error_code() { return swig_jl_err_code; }\n"
      "const char* swig_jl_last_error_msg() { return swig_jl_err_msg.c_str(); }\n"
      "void swig_jl_str_free(char* p) { free(p); }\n"
      "}\n\n");

    Language::top(n);

    SwigType_emit_type_table(f_runtime, f_wrappers);
    Printf(f_init, "extern \"C\" void _swig_jl_init_types() {\n%s}\n", f_type_init);

    Dump(f_runtime, f_begin);
    Dump(f_header, f_begin);
    Dump(f_wrappers, f_begin);
    Dump(f_init, f_begin);

    String *jlfile = NewStringf("%s%s.jl", SWIG_output_directory(), module_name);
    File *jf = NewFile(jlfile, "w", SWIG_output_files());
    if (!jf) { FileErrorDisplay(jlfile); Exit(EXIT_FAILURE); }
    Printf(jf, "# Generated by SWIG -julia. Do not edit.\n");
    Printf(jf, "module %s\n\n", module_name);
    Printf(jf, "const _lib = joinpath(@__DIR__, \"lib%s_wrap.so\")\n\n", module_name);
    Printf(jf, "function __init__()\n    ccall((:_swig_jl_init_types, _lib), Cvoid, ())\nend\n\n");
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
    Dump(f_jl_types, jf);
    Printf(jf, "\n");
    Dump(f_jl_body, jf);
    /* Julia structs do not inherit: forward statics from base proxies so
       sym(SX, ...) reaches the GenSX implementation. */
    if (class_bases) {
      Printf(jf, "\n# base-class static forwarders\n");
      Iterator ci = First(class_bases);
      while (ci.key) {
        String *derived = (String *)ci.key;
        /* transitive base walk */
        List *queue = Copy((List *)ci.item);
        Hash *seen = NewHash();
        for (int qi = 0; qi < Len(queue); ++qi) {
          String *b = (String *)Getitem(queue, qi);
          if (Getattr(seen, b)) continue;
          Setattr(seen, b, "1");
          List *ml = class_members ? (List *)Getattr(class_members, b) : 0;
          if (ml) {
            for (int mi = 0; mi < Len(ml); ++mi) {
              String *line = Copy((String *)Getitem(ml, mi));
              Replaceall(line, "@SELF@", derived);
              Printv(jf, line, NIL);
              Delete(line);
            }
          }
          Hash *set = (Hash *)Getattr(class_statics, b);
          if (set) {
            Iterator si = First(set);
            while (si.key) {
              Hash *own = class_statics ? (Hash *)Getattr(class_statics, derived) : 0;
              if (!own || !Getattr(own, si.key))
                Printf(jf, "%s(::Type{%s}, args...) = %s(%s, args...)\n",
                       si.key, derived, si.key, b);
              si = Next(si);
            }
          }
          List *bb = (List *)Getattr(class_bases, b);
          if (bb) for (int k = 0; k < Len(bb); ++k) Append(queue, Getitem(bb, k));
        }
        Delete(queue); Delete(seen);
        ci = Next(ci);
      }
    }
    if (Len(f_jl_exports) > 0) Printf(jf, "export %s\n", f_jl_exports);
    Printf(jf, "\nend # module %s\n", module_name);
    Delete(jf);

    if (n_skipped > 0)
      Printf(stderr, "swig -julia: skipped %d declaration(s) with unsupported types\n", n_skipped);

    Delete(f_begin);
    return SWIG_OK;
  }

  void add_export(const String *name) {
    String *probe = NewStringf("|%s|", name);
    String *hay = NewStringf("|%s|", f_jl_exports);
    Replaceall(hay, ", ", "|");
    bool present = Strstr(hay, probe) != 0;
    Delete(probe); Delete(hay);
    if (present) return;
    if (Len(f_jl_exports) > 0) Printv(f_jl_exports, ", ", NIL);
    Printv(f_jl_exports, name, NIL);
  }

  void skip(Node *n, const char *why) {
    ++n_skipped;
    Printf(f_jl_body, "# skipped %s (%s)\n", Getattr(n, "sym:name"), why);
  }

  /* ---- the single emission point: every callable lands here ---- */

  virtual int functionWrapper(Node *n) {
    String *symname = Getattr(n, "sym:name");
    String *overname = Getattr(n, "sym:overname");
    SwigType *returntype = Getattr(n, "type");
    ParmList *parms = Getattr(n, "parms");

    Wrapper *w = NewWrapper();
    String *wname = NewStringf("_swig_%s_%s%s",
        class_jlname ? Char(class_jlname) : "g", Char(symname),
        overname ? Char(overname) : "");
    Setattr(n, "wrap:name", wname);

    /* attach typemaps; sets lname (arg1, ...) for $1 substitution */
    emit_parameter_variables(parms, w);
    emit_attach_parmmaps(parms, w);
    Swig_typemap_attach_parms("ctype", parms, w);
    Swig_typemap_attach_parms("jltype", parms, w);
    Swig_typemap_attach_parms("jlparam", parms, w);

    /* return-type typemaps via a fake parm (lname needed for $1 subst) */
    Parm *retp = NewParm(returntype, NewString("result"), n);
    Setattr(retp, "lname", Swig_cresult_name());
    Swig_typemap_attach_parms("ctype", retp, 0);
    Swig_typemap_attach_parms("jltype", retp, 0);
    Swig_typemap_attach_parms("jlout", retp, 0);

    String *ret_ct = Getattr(retp, "tmap:ctype:out");
    if (!ret_ct) ret_ct = Getattr(retp, "tmap:ctype");
    String *ret_jt = Getattr(retp, "tmap:jltype:out");
    if (!ret_jt) ret_jt = Getattr(retp, "tmap:jltype");
    String *ret_jlout = Getattr(retp, "tmap:jlout");
    String *ret_proxy = proxy_name(returntype);
    bool ret_is_class = ret_proxy != 0;
    if (!ret_ct || !ret_jt || (!ret_jlout && !ret_is_class)) {
      skip(n, "return type");
      DelWrapper(w); Delete(wname);
      return SWIG_NOWRAP;
    }

    /* ---- C side: signature + in-typemap marshaling ---- */
    String *csig = NewString("");
    String *jargs = NewString("");        /* Julia typed params */
    List *jarg_names = 0, *jarg_defaults = 0;
    String *jccall_types = NewString("");
    String *jccall_args = NewString("");
    String *preserve = NewString("");
    int idx = 0;
    bool ok = true;
    Parm *p = parms;
    while (p) {
      if (checkAttribute(p, "tmap:in:numinputs", "0")) {
        p = Getattr(p, "tmap:in:next");
        continue;
      }
      String *ct = Getattr(p, "tmap:ctype");
      String *in = Getattr(p, "tmap:in");
      String *jt = Getattr(p, "tmap:jltype");
      String *jp = Getattr(p, "tmap:jlparam");
      String *pproxy = proxy_name(Getattr(p, "type"));
      bool jl_any = jt && Strcmp(jt, "Any") == 0;
      if (!ct || !in || !jt || (!jp && !pproxy && !jl_any)) { ok = false; break; }

      String *inputvar = NewStringf("jarg%d", idx);
      if (Len(csig) > 0) Printf(csig, ", ");
      Printf(csig, "%s %s", ct, inputvar);
      String *inb = Copy(in);
      Replaceall(inb, "$input", inputvar);
      Printv(w->code, inb, "\n", NIL);
      Delete(inb);

      String *pname = Getattr(p, "name");
      String *an = (pname && Len(pname) > 0) ? NewStringf("%s", pname) : NewStringf("a%d", idx);
      Replaceall(an, "::", "_");
      {  /* typemap-applied parms can share names (INOUT, ...): dedupe */
        String *probe = NewStringf("%s::", an);
        bool dup = false;
        if (jarg_names)
          for (int i = 0; i < Len(jarg_names); ++i)
            if (Strncmp((String *)Getitem(jarg_names, i), probe, Len(probe)) == 0) { dup = true; break; }
        if (dup) { Delete(an); an = NewStringf("a%d", idx); }
        Delete(probe);
      }
      if (!jarg_names) { jarg_names = NewList(); jarg_defaults = NewList(); }
      Append(jarg_names, NewStringf("%s::%s", an, jp ? jp : (pproxy ? pproxy : (String*)NewString("Any"))));
      {  /* compactdefaultargs: literal defaults, applied as a trailing run */
        String *dv = Getattr(p, "value");
        String *jdv = NewString("");
        if (dv && Len(dv) > 0) {
          if (Strcmp(dv, "true") == 0 || Strcmp(dv, "false") == 0) Printv(jdv, dv, NIL);
          else {
            bool numeric = Len(dv) > 0;
            for (const char *c = Char(dv); *c; ++c)
              if (!(isdigit(*c) || *c == '.' || *c == '-' || *c == '+' || *c == 'e')) { numeric = false; break; }
            if (numeric) Printv(jdv, dv, NIL);
          }
        }
        Append(jarg_defaults, jdv);
      }
      Printf(jccall_types, "%s, ", jt);
      if (!jp && pproxy && !jl_any) {  /* opaque-ptr class param: pass .ptr */
        Printf(jccall_args, "%s.ptr, ", an);
        Printf(preserve, "%s ", an);
      } else {
        Printf(jccall_args, "%s, ", an);
      }
      Delete(an); Delete(inputvar);
      if (pproxy) Delete(pproxy);
      ++idx;
      p = Getattr(p, "tmap:in:next") ? Getattr(p, "tmap:in:next") : nextSibling(p);
    }
    if (!ok) {
      skip(n, "parameter type");
      DelWrapper(w); Delete(wname);
      Delete(csig); Delete(jargs); Delete(jccall_types); Delete(jccall_args); Delete(preserve);
      return SWIG_NOWRAP;
    }

    /* assemble the Julia parameter list; defaults only as a trailing run */
    if (jarg_names) {
      int first_def = Len(jarg_names);
      for (int i = Len(jarg_names) - 1; i >= 0; --i) {
        if (Len((String *)Getitem(jarg_defaults, i)) == 0) break;
        first_def = i;
      }
      for (int i = 0; i < Len(jarg_names); ++i) {
        if (i > 0) Printf(jargs, ", ");
        Printv(jargs, (String *)Getitem(jarg_names, i), NIL);
        if (i >= first_def) Printf(jargs, "=%s", (String *)Getitem(jarg_defaults, i));
      }
      Delete(jarg_names); Delete(jarg_defaults);
    }

    /* action: members/ctors/statics arrive pre-transformed by the Language
       base; plain globals need the default action built here. */
    if (!Getattr(n, "wrap:action")) {
      String *call = Swig_cfunction_call(Getattr(n, "name"), parms);
      Setattr(n, "wrap:action", Swig_cresult(returntype, Swig_cresult_name(), call));
    }
    bool is_void = Strcmp(ret_ct, "void") == 0;
    if (!is_void) emit_return_variable(n, returntype, w);
    String *actioncode = emit_action(n);

    String *outtm = 0;
    if (!is_void) {
      outtm = Swig_typemap_lookup_out("out", n, Swig_cresult_name(), w, actioncode);
      if (!outtm) {
        skip(n, "return conversion");
        DelWrapper(w); Delete(wname);
        return SWIG_NOWRAP;
      }
      Replaceall(outtm, "$result", "_outv");
    }

    Printf(f_wrappers, "extern \"C\" %s %s(%s) {\n  SWIG_JL_ENTER();\n  try {\n",
           ret_ct, wname, Len(csig) ? Char(csig) : "void");
    Printv(f_wrappers, w->locals, NIL);
    if (!is_void) Printf(f_wrappers, "  %s _outv;\n", ret_ct);
    Printv(f_wrappers, w->code, NIL);
    if (is_void) {
      Printv(f_wrappers, actioncode, NIL);
      Printf(f_wrappers, "  return;\n");
    } else {
      Printv(f_wrappers, outtm, "\n", NIL);
      Printf(f_wrappers, "  return _outv;\n");
    }
    Printf(f_wrappers, "  } SWIG_JL_CATCH(%s)\n}\n\n",
           is_void ? "" : (Strstr(ret_ct, "*") ? "0" : (Strcmp(ret_ct, "double") == 0 ? "0.0" : "0")));

    /* ---- Julia side ---- */
    String *callcore = NewStringf("ccall((:%s, _lib), %s, (%s), %s)",
                                  wname, ret_jt, jccall_types, jccall_args);
    String *body = 0;
    if (in_ctor) {
      body = NewStringf("%s(_check(%s))", class_jlname, callcore);
    } else if (ret_is_class) {
      body = NewStringf("%s(_check(%s))", ret_proxy, callcore);
    } else {
      body = Copy(ret_jlout);
      Replaceall(body, "$call", callcore);
    }

    String *fname = in_ctor ? Copy(class_jlname)
                  : bare_symname ? Copy(bare_symname) : Copy(symname);
    String *sig = NewString("");
    if (in_static) Printf(sig, "::Type{%s}%s", class_jlname, Len(jargs) ? ", " : "");
    Printv(sig, jargs, NIL);
    String *jline = NewString("");
    if (Len(preserve) > 0)
      Printf(jline, "%s(%s) = GC.@preserve %sbegin %s end\n", fname, sig, preserve, body);
    else
      Printf(jline, "%s(%s) = %s\n", fname, sig, body);
    Printv(f_jl_body, jline, NIL);
    if (class_jlname && !in_static && !in_ctor) {
      /* member method: register a @SELF@ template for derived-class flattening */
      String *tmpl = Copy(jline);
      String *selfpat = NewStringf("self::%s", class_jlname);
      if (Strstr(tmpl, selfpat)) {
        Replaceall(tmpl, selfpat, "self::@SELF@");
        if (!class_members) class_members = NewHash();
        List *lst = (List *)Getattr(class_members, class_jlname);
        if (!lst) { lst = NewList(); Setattr(class_members, class_jlname, lst); }
        Append(lst, tmpl);
      } else Delete(tmpl);
      Delete(selfpat);
    }
    Delete(jline);
    if (!class_jlname || in_static || in_ctor) add_export(fname);
    if (in_static && class_jlname) {
      if (!class_statics) class_statics = NewHash();
      Hash *set = (Hash *)Getattr(class_statics, class_jlname);
      if (!set) { set = NewHash(); Setattr(class_statics, class_jlname, set); }
      Setattr(set, fname, "1");
    }

    Delete(fname); Delete(sig); Delete(body); Delete(callcore);
    Delete(csig); Delete(jargs); Delete(jccall_types); Delete(jccall_args);
    Delete(preserve); Delete(wname);
    if (ret_proxy) Delete(ret_proxy);
    DelWrapper(w);
    return SWIG_OK;
  }

  /* ---- context plumbing around the Language base transforms ---- */

  virtual int classHandler(Node *n) {
    class_jlname = Getattr(n, "sym:name");
    String *cname = Getattr(n, "name");

    Printf(f_jl_types,
      "mutable struct %s\n    ptr::Ptr{Cvoid}\n"
      "    function %s(p::Ptr{Cvoid})\n"
      "        x = new(p)\n"
      "        finalizer(o -> (ccall((:_swig_%s_delete, _lib), Cvoid, (Ptr{Cvoid},), o.ptr); o.ptr = C_NULL), x)\n"
      "        x\n    end\nend\n",
      class_jlname, class_jlname, class_jlname);
    add_export(class_jlname);

    String *cxxname = SwigType_namestr(cname);
    {
      SwigType *ct = Copy(Getattr(n, "name"));
      SwigType_add_pointer(ct);
      SwigType_remember(ct);
      String *mangled = SwigType_manglestr(ct);
      Printf(f_type_init, "  swig_jl_register_proxy(\"%s\", SWIGTYPE%s);\n",
             class_jlname, mangled);
      Delete(mangled); Delete(ct);
    }
    Printf(f_wrappers,
      "extern \"C\" void _swig_%s_delete(void *p) {\n"
      "  SWIG_JL_ENTER();\n  try { delete static_cast<%s*>(p); } SWIG_JL_CATCH()\n}\n\n",
      class_jlname, cxxname);
    Delete(cxxname);

    {
      List *bases = Getattr(n, "bases");
      if (bases && Len(bases) > 0) {
        if (!class_bases) class_bases = NewHash();
        List *bl = NewList();
        for (int bi = 0; bi < Len(bases); ++bi) {
          String *bn = Getattr(Getitem(bases, bi), "sym:name");
          if (bn) Append(bl, Copy(bn));
        }
        Setattr(class_bases, class_jlname, bl);
      }
    }
    Language::classHandler(n);
    class_jlname = 0;
    return SWIG_OK;
  }

  virtual int constructorHandler(Node *n) {
    in_ctor = true;
    int r = Language::constructorHandler(n);
    in_ctor = false;
    return r;
  }

  virtual int memberfunctionHandler(Node *n) {
    bare_symname = Copy(Getattr(n, "sym:name"));
    int r = Language::memberfunctionHandler(n);
    Delete(bare_symname); bare_symname = 0;
    return r;
  }

  virtual int staticmemberfunctionHandler(Node *n) {
    in_static = true;
    bare_symname = Copy(Getattr(n, "sym:name"));
    int r = Language::staticmemberfunctionHandler(n);
    Delete(bare_symname); bare_symname = 0;
    in_static = false;
    return r;
  }

  virtual int constantWrapper(Node *n) {
    if (class_jlname) return SWIG_OK;  /* class-scope consts: later */
    String *symname = Getattr(n, "sym:name");
    String *value = Getattr(n, "value");
    SwigType *t = Getattr(n, "type");
    String *ts = SwigType_str(t, 0);
    if (Strcmp(ts, "int") == 0 || Strcmp(ts, "long") == 0 || Strstr(ts, "long long")) {
      /* value may be a C++ qualified name (casadi::OP_ADD); emit a C getter */
      Printf(f_wrappers, "extern \"C\" long long _swig_const_%s() { return (long long)(%s); }\n",
             symname, value);
      Printf(f_jl_body, "const %s = Int(ccall((:_swig_const_%s, _lib), Clonglong, ()))\n",
             symname, symname);
    } else if (Strcmp(ts, "double") == 0) {
      Printf(f_wrappers, "extern \"C\" double _swig_const_%s() { return (double)(%s); }\n",
             symname, value);
      Printf(f_jl_body, "const %s = ccall((:_swig_const_%s, _lib), Cdouble, ())\n",
             symname, symname);
    }
    return SWIG_OK;  /* silently skip strings/other for now */
  }

  virtual int destructorHandler(Node *) { return SWIG_OK; } /* emitted in classHandler */
  virtual int membervariableHandler(Node *) { return SWIG_OK; }    /* later */
  virtual int staticmembervariableHandler(Node *) { return SWIG_OK; }
  virtual int globalvariableHandler(Node *) { return SWIG_OK; }
};

static Language *new_swig_julia() { return new JULIA(); }
extern "C" Language *swig_julia(void) { return new_swig_julia(); }
