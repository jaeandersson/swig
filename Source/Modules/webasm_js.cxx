/* -----------------------------------------------------------------------------
 * webasm_js.cxx
 *
 * SWIG language module that generates a TWO-FILE WebAssembly+JavaScript
 * binding for a C++ API, without depending on Emscripten Embind:
 *
 *   <outfile>           --  extern "C" EMSCRIPTEN_KEEPALIVE wrappers (C++)
 *   <outfile>.js        --  hand-style JavaScript wrapper exposing classes,
 *                           free functions, enums; calls Module._swig_*
 *                           directly via raw wasm exports
 *   <outfile>.exports   --  newline-separated list of "_swig_*" symbols,
 *                           usable as @-file for em++ -sEXPORTED_FUNCTIONS
 *
 * Structure cribbed from matlab.cxx (multi-file output, per-class
 * accumulators, handler dispatch flow), then rewritten for the
 * extern-C + JS-class target.  Built on the same handlers verified in
 * embind.cxx.  Status: experimental.
 * ----------------------------------------------------------------------------- */

#include "swigmod.h"

static const char *usage = "\
WebAssembly+JS Options (use with -webasm-js):\n\
  (none yet)\n";

class WASM_JS : public Language {
public:
  WASM_JS()
    : f_out_cpp(0), f_out_js(0), f_out_exports(0),
      f_cpp_header(0), f_cpp_wrappers(0),
      f_js_pre(0), f_js_classes(0), f_js_module(0),
      class_cname(0), class_jsname(0),
      class_js_body(0), class_cpp_section(0),
      enum_cname(0), enum_js_body(0),
      ctor_arities_seen(0), ctor_overload_count(0),
      member_names_seen(0), member_overload_counts(0),
      global_names_seen(0), global_overload_counts(0),
      class_has_base(false) {}

  /* Allocate a unique-per-(scope, name) C identifier suffix. */
  int next_index(Hash *h, const String *key) {
    if (!h) return 0;
    String *v = (String*)Getattr(h, key);
    int n = v ? atoi(Char(v)) : 0;
    char buf[32]; snprintf(buf, sizeof(buf), "%d", n + 1);
    Setattr(h, key, buf);
    return n;
  }

  virtual void main(int argc, char *argv[]);
  virtual int top(Node *n);

  virtual int classHandler(Node *n);
  virtual int memberfunctionHandler(Node *n);
  virtual int membervariableHandler(Node *n);
  virtual int constructorHandler(Node *n);
  virtual int destructorHandler(Node *n);
  virtual int staticmemberfunctionHandler(Node *n);
  virtual int globalfunctionHandler(Node *n);
  virtual int enumDeclaration(Node *n);
  virtual int enumvalueDeclaration(Node *n);

protected:
  File   *f_out_cpp;
  File   *f_out_js;
  File   *f_out_exports;

  String *f_cpp_header;
  String *f_cpp_wrappers;
  String *f_js_pre;
  String *f_js_classes;
  String *f_js_module;

  String *class_cname;
  String *class_jsname;
  String *class_js_body;
  String *class_cpp_section;

  String *enum_cname;
  String *enum_js_body;

  Hash   *ctor_arities_seen;
  int    ctor_overload_count;
  Hash   *member_names_seen;       /* per-class: jsname -> "1" (for JS-side dedup) */
  Hash   *member_overload_counts;  /* per-class: jsname -> "N" (next index for C symbol) */
  Hash   *global_names_seen;       /* module-level: jsname -> "1" */
  Hash   *global_overload_counts;  /* module-level: fname -> "N" */
  bool   class_has_base;

  String *mangle(const String *s) {
    String *r = NewString(s);
    Replaceall(r, "::", "_");
    Replaceall(r, "<", "_");
    Replaceall(r, ">", "_");
    Replaceall(r, ",", "_");
    Replaceall(r, " ", "");
    Replaceall(r, "&", "");
    Replaceall(r, "*", "");
    return r;
  }

  void register_export(const char *swig_name) {
    Printf(f_out_exports, "_%s\n", swig_name);
  }

  /* True if a parameter type is std::string-like (passed as const char* on C side). */
  bool parm_is_string(Parm *q) {
    SwigType *t = Getattr(q, "type");
    if (!t) return false;
    String *ts = SwigType_str(t, 0);
    bool yes = Strstr(ts, "std::string") != 0;
    Delete(ts);
    return yes;
  }

  String *parm_decls(ParmList *p) {
    String *out = NewString("");
    int i = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      SwigType *t = Getattr(q, "type");
      String *ts = SwigType_str(t, 0);
      if (Strstr(ts, "std::string")) {
        Printf(out, "%sconst char* a%d", i == 0 ? "" : ", ", i);
      } else {
        Printf(out, "%s%s a%d", i == 0 ? "" : ", ", ts, i);
      }
      Delete(ts);
      ++i;
    }
    return out;
  }

  /* C++ call-site args. Strings get wrapped in std::string(...). */
  String *parm_args(ParmList *p) {
    String *out = NewString("");
    int i = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      if (parm_is_string(q)) Printf(out, "%sstd::string(a%d)", i == 0 ? "" : ",", i);
      else                   Printf(out, "%s a%d", i == 0 ? "" : ",", i);
      ++i;
    }
    return out;
  }

  String *js_arg_names(ParmList *p) {
    String *out = NewString("");
    int i = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      Printf(out, "%s a%d", i == 0 ? "" : ",", i);
      ++i;
    }
    return out;
  }

  int parm_arity(ParmList *p) {
    int n = 0;
    for (Parm *q = p; q; q = nextSibling(q)) ++n;
    return n;
  }

  /* Emit a JS method body (multi-line). Handles:
       - allocating UTF-8 buffers for std::string args
       - calling the wasm export
       - marshalling string returns / bool returns
       - freeing allocated buffers
     `self_prefix` is "this._ptr" for instance methods, "" for static/global. */
  String *emit_js_body(ParmList *p, SwigType *rt, const String *swig_name, const char *self_prefix) {
    String *out = NewString("");
    int n_str = 0, i = 0;
    for (Parm *q = p; q; q = nextSibling(q), ++i) {
      if (parm_is_string(q)) {
        Printf(out,
          "      const __nps%d = M.lengthBytesUTF8(a%d);\n"
          "      const __ps%d  = M._malloc(__nps%d + 1);\n"
          "      M.stringToUTF8(a%d, __ps%d, __nps%d + 1);\n",
          i, i, i, i, i, i, i);
        ++n_str;
      }
    }
    String *call_args = NewString("");
    i = 0;
    for (Parm *q = p; q; q = nextSibling(q), ++i) {
      if (i > 0) Printf(call_args, ", ");
      if (parm_is_string(q)) Printf(call_args, "__ps%d", i);
      else Printf(call_args, "a%d", i);
    }
    String *raw_call;
    if (self_prefix && self_prefix[0]) {
      raw_call = NewStringf("M._%s(%s%s%s)", swig_name, self_prefix,
                            Len(call_args) > 0 ? ", " : "", call_args);
    } else {
      raw_call = NewStringf("M._%s(%s)", swig_name, call_args);
    }
    String *marshalled = js_marshal_return(rt, Char(raw_call));
    if (n_str == 0) {
      Printf(out, "      return %s;\n", marshalled);
    } else {
      Printf(out, "      const __r = %s;\n", marshalled);
      i = 0;
      for (Parm *q = p; q; q = nextSibling(q), ++i) {
        if (parm_is_string(q)) Printf(out, "      M._free(__ps%d);\n", i);
      }
      Printf(out, "      return __r;\n");
    }
    Delete(call_args); Delete(raw_call); Delete(marshalled);
    return out;
  }

  // Wrap a JS expression so the JS side sees the right native type.
  String *js_marshal_return(SwigType *rt, const char *expr) {
    if (!rt) return NewString(expr);
    String *ts = SwigType_str(rt, 0);
    String *r;
    if (Strstr(ts, "std::string")) {
      r = NewStringf("(()=>{const __p=%s;const __s=M.UTF8ToString(__p);M._free(__p);return __s;})()", expr);
    } else if (Cmp(ts, "bool") == 0) {
      r = NewStringf("(!!(%s))", expr);   // int 0/1 -> JS boolean
    } else {
      r = NewString(expr);
    }
    Delete(ts);
    return r;
  }

  String *cpp_return_type(SwigType *rt) {
    if (!rt) return NewString("void");
    String *ts = SwigType_str(rt, 0);
    if (Strstr(ts, "std::string")) {
      Delete(ts);
      return NewString("char*");
    }
    /* bool through C linkage in wasm: cast to int for clean 0/1. */
    if (Cmp(ts, "bool") == 0) {
      Delete(ts);
      return NewString("int");
    }
    return ts;
  }

  // Emit "{ ... }" body that performs the call and returns appropriately.
  String *cpp_call_body(SwigType *rt, const String *call_expr) {
    String *out = NewString("");
    if (!rt) {
      Printf(out, "%s;", call_expr);
      return out;
    }
    String *ts = SwigType_str(rt, 0);
    if (Cmp(ts, "void") == 0) {
      Printf(out, "%s;", call_expr);
    } else if (Strstr(ts, "std::string")) {
      Printf(out,
        "auto __s = %s; "
        "char* __buf = (char*)malloc(__s.size()+1); "
        "memcpy(__buf, __s.c_str(), __s.size()+1); "
        "return __buf;",
        call_expr);
    } else {
      Printf(out, "return %s;", call_expr);
    }
    Delete(ts);
    return out;
  }
};

// ============================ main / top ====================================

void WASM_JS::main(int argc, char *argv[]) {
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "-help") == 0) fputs(usage, stdout);
  }
  Preprocessor_define("SWIGWASMJS 1", 0);
  allow_overloading();
}

int WASM_JS::top(Node *n) {
  String *module_name = Getattr(n, "name");
  String *outfile = Getattr(n, "outfile");

  f_out_cpp = NewFile(outfile, "w", SWIG_output_files());
  if (!f_out_cpp) { FileErrorDisplay(outfile); Exit(EXIT_FAILURE); }

  String *jsfile = NewString(outfile);
  String *ext = Swig_file_extension(jsfile);
  if (ext && Len(ext)) Delslice(jsfile, Len(jsfile) - Len(ext), DOH_END);
  Append(jsfile, ".js");
  f_out_js = NewFile(jsfile, "w", SWIG_output_files());
  if (!f_out_js) { FileErrorDisplay(jsfile); Exit(EXIT_FAILURE); }

  String *exportfile = NewStringf("%s.exports", outfile);
  f_out_exports = NewFile(exportfile, "w", SWIG_output_files());
  if (!f_out_exports) { FileErrorDisplay(exportfile); Exit(EXIT_FAILURE); }

  f_cpp_header   = NewString("");
  f_cpp_wrappers = NewString("");
  f_js_pre       = NewString("");
  f_js_classes   = NewString("");
  f_js_module    = NewString("");

  String *sink = NewString("");
  Swig_register_filebyname("header",  f_cpp_header);
  Swig_register_filebyname("runtime", sink);
  Swig_register_filebyname("wrapper", sink);
  Swig_register_filebyname("init",    sink);
  Swig_register_filebyname("begin",   sink);

  Printf(f_out_exports, "_malloc\n_free\n");

  /* Shared sentinel that lets derived ctors pass a pre-allocated ptr up to
     their base ctor through super(), avoiding double-allocation. */
  Printf(f_js_pre,
    "  const __PRIVATE_CTOR = Symbol('swig-private-ctor');\n\n");

  Language::top(n);

  // C++ output
  Printf(f_out_cpp,
    "// Generated by SWIG -webasm-js (NO Embind).  Do not edit.\n\n"
    "#include <emscripten.h>\n"
    "#include <cstdlib>\n"
    "#include <cstring>\n"
    "#include <string>\n"
    "%s\n"
    "extern \"C\" {\n\n%s\n} // extern \"C\"\n",
    f_cpp_header, f_cpp_wrappers);

  // JS output -- strip directory + extension to get sibling name for require()
  String *base = NewString(outfile);
  String *bext = Swig_file_extension(base);
  if (bext && Len(bext)) Delslice(base, Len(base) - Len(bext), DOH_END);
  /* Strip leading directory components */
  const char *bc = Char(base);
  const char *slash = strrchr(bc, '/');
  if (slash) {
    String *bn = NewString(slash + 1);
    Delete(base);
    base = bn;
  }

  Printf(f_out_js,
    "// Generated by SWIG -webasm-js (NO Embind).  Do not edit.\n"
    "//\n"
    "// Compile <name>.cpp with em++:\n"
    "//   em++ -O3 -fwasm-exceptions <name>.cpp libfoo.a \\\n"
    "//     -s MODULARIZE=1 -s EXPORT_NAME=createWasm \\\n"
    "//     -s EXPORTED_RUNTIME_METHODS='[\"UTF8ToString\"]' \\\n"
    "//     -s EXPORTED_FUNCTIONS=@<name>.exports \\\n"
    "//     -o <name>_wasm.js\n\n"
    "const createWasm = require('./%s_wasm.js');\n\n"
    "module.exports = async function create%s() {\n"
    "  const M = await createWasm();\n\n"
    "%s"
    "%s"
    "  return {\n"
    "%s"
    "  };\n"
    "};\n",
    base, module_name, f_js_pre, f_js_classes, f_js_module);

  Delete(base);
  Delete(f_cpp_header);
  Delete(f_cpp_wrappers);
  Delete(f_js_pre);
  Delete(f_js_classes);
  Delete(f_js_module);
  Delete(f_out_cpp);
  Delete(f_out_js);
  Delete(f_out_exports);
  return SWIG_OK;
}

// =========================== Class machinery ================================

int WASM_JS::classHandler(Node *n) {
  class_jsname = Getattr(n, "sym:name");
  SwigType *t  = Getattr(n, "name");
  String *cname = SwigType_str(t, 0);
  class_cname = cname;

  class_cpp_section = NewString("");
  class_js_body     = NewString("");
  ctor_arities_seen = NewHash();
  ctor_overload_count = 0;
  member_names_seen = NewHash();
  member_overload_counts = NewHash();
  class_has_base    = (Getattr(n, "bases") && Len(Getattr(n, "bases")) > 0);

  Language::classHandler(n);

  /* Emit a single JS constructor that dispatches on arguments.length to
     the right wasm-export ctor. */
  if (Len(ctor_arities_seen) > 0) {
    String *cmangle = mangle(cname);
    String *ctor_js = NewString("");
    Printf(ctor_js, "    constructor(...args) {\n");
    if (class_has_base) {
      Printf(ctor_js,
        "      if (args[0] === __PRIVATE_CTOR) { super(__PRIVATE_CTOR, args[1]); return; }\n");
    } else {
      Printf(ctor_js,
        "      if (args[0] === __PRIVATE_CTOR) { this._ptr = args[1]; return; }\n");
    }
    Printf(ctor_js, "      let __ptr;\n      switch (args.length) {\n");
    Iterator it = First(ctor_arities_seen);
    while (it.key) {
      Printf(ctor_js,
        "        case %s: __ptr = M._%s(...args); break;\n",
        it.key, it.item);
      it = Next(it);
    }
    Printf(ctor_js,
      "        default: throw new Error(`%s: no ctor for ${args.length} args`);\n"
      "      }\n", class_jsname);
    if (class_has_base) {
      Printf(ctor_js, "      super(__PRIVATE_CTOR, __ptr);\n");
    } else {
      Printf(ctor_js, "      this._ptr = __ptr;\n");
    }
    Printf(ctor_js,
      "      __fr_%s.register(this, __ptr, this);\n"
      "    }\n", cmangle);

    /* Prepend the constructor before the rest of the body. */
    String *new_body = NewStringf("%s%s", ctor_js, class_js_body);
    Delete(class_js_body);
    class_js_body = new_body;
    Delete(ctor_js); Delete(cmangle);
  }

  Printf(f_cpp_wrappers,
    "// --- class %s (%s) ---\n%s\n",
    class_jsname, cname, class_cpp_section);

  // Inheritance: take the first public base if any -- `class D extends B`.
  String *base_clause = NewString("");
  List *bases = Getattr(n, "bases");
  if (bases && Len(bases) > 0) {
    Node *b = Getitem(bases, 0);
    String *bjsname = Getattr(b, "sym:name");
    if (bjsname) Printf(base_clause, " extends %s", bjsname);
  }

  // Per-class FinalizationRegistry: GC of the JS wrapper triggers
  // _swig_<C>_delete on the underlying WASM pointer. Manual .delete()
  // is still available below as an opt-in early-release.
  String *cmangle = mangle(cname);
  Printf(f_js_classes,
    "  const __fr_%s = new FinalizationRegistry(p => M._swig_%s_delete(p));\n"
    "  class %s%s {\n%s  }\n",
    cmangle, cmangle, class_jsname, base_clause, class_js_body);
  Delete(cmangle);
  Delete(base_clause);

  Printf(f_js_module, "    %s,\n", class_jsname);

  Delete(class_cpp_section); class_cpp_section = 0;
  Delete(class_js_body);     class_js_body     = 0;
  Delete(ctor_arities_seen); ctor_arities_seen = 0;
  Delete(member_names_seen); member_names_seen = 0;
  Delete(member_overload_counts); member_overload_counts = 0;
  Delete(cname);
  class_cname = 0;
  class_jsname = 0;
  return SWIG_OK;
}

int WASM_JS::constructorHandler(Node *n) {
  if (!class_cname) return Language::constructorHandler(n);
  ParmList *p = Getattr(n, "parms");
  int arity = parm_arity(p);

  bool skip = false;
  if (arity == 1) {
    SwigType *qt = Getattr(p, "type");
    String *ts = SwigType_str(qt, 0);
    if (Strstr(ts, class_cname)) skip = true;
    else if (Strstr(ts, "::"))   skip = true;
    Delete(ts);
  }

  char keybuf[32]; snprintf(keybuf, sizeof(keybuf), "%d", arity);
  if (!skip && !Getattr(ctor_arities_seen, keybuf)) {
    Setattr(ctor_arities_seen, keybuf, "1");

    String *cmangle = mangle(class_cname);
    String *swig_name = NewStringf("swig_%s_new_%d", cmangle, ctor_overload_count++);
    String *decls = parm_decls(p);
    String *args  = parm_args(p);
    String *jsargs = js_arg_names(p);

    Printf(class_cpp_section,
      "EMSCRIPTEN_KEEPALIVE %s* %s(%s) { return new %s(%s); }\n",
      class_cname, swig_name, decls, class_cname, args);
    register_export(Char(swig_name));

    /* Record this ctor's swig_name keyed by arity for dispatcher emission
       in classHandler close. */
    Setattr(ctor_arities_seen, keybuf, swig_name);

    Delete(swig_name); Delete(decls); Delete(args); Delete(jsargs); Delete(cmangle);
  }
  return Language::constructorHandler(n);
}

int WASM_JS::destructorHandler(Node *n) {
  if (class_cname) {
    String *cmangle = mangle(class_cname);
    String *swig_name = NewStringf("swig_%s_delete", cmangle);
    Printf(class_cpp_section,
      "EMSCRIPTEN_KEEPALIVE void %s(%s* self) { delete self; }\n",
      swig_name, class_cname);
    register_export(Char(swig_name));
    // Optional manual release; unregister from FinalizationRegistry to
    // avoid double-free when GC eventually runs.
    Printf(class_js_body,
      "    delete() {\n"
      "      if (this._ptr === 0) return;\n"
      "      __fr_%s.unregister(this);\n"
      "      M._%s(this._ptr);\n"
      "      this._ptr = 0;\n"
      "    }\n",
      cmangle, swig_name);
    Delete(swig_name); Delete(cmangle);
  }
  return Language::destructorHandler(n);
}

int WASM_JS::memberfunctionHandler(Node *n) {
  if (!class_cname) return Language::memberfunctionHandler(n);

  String *jsraw_raw = Getattr(n, "sym:name");
  String *mname = Getattr(n, "name");
  if (Strstr(mname, "operator")) return Language::memberfunctionHandler(n);

  /* Strip __SWIG_N suffix so all overloads share the same JS-facing name. */
  String *jsraw = NewString(jsraw_raw);
  const char *suf = Strstr(jsraw, "__SWIG_");
  if (suf) Delslice(jsraw, suf - Char(jsraw), DOH_END);

  ParmList *p = Getattr(n, "parms");
  SwigType *rt = Getattr(n, "type");
  String *cmangle = mangle(class_cname);
  /* Per-(class, mname) overload index; unique across all overloads. */
  int idx = next_index(member_overload_counts, mname);
  String *swig_name = NewStringf("swig_%s_%s_%d", cmangle, mname, idx);

  String *self_q = NewString("");
  String *q = Getattr(n, "qualifier");
  if (q && Strstr(q, "const")) Printf(self_q, "const ");
  String *decls = parm_decls(p);
  String *args  = parm_args(p);
  String *jsargs = js_arg_names(p);
  String *ret_t  = cpp_return_type(rt);
  String *call_e = NewStringf("self->%s(%s)", mname, args);
  String *body   = cpp_call_body(rt, call_e);

  Printf(class_cpp_section,
    "EMSCRIPTEN_KEEPALIVE %s %s(%s%s* self%s%s) { %s }\n",
    ret_t, swig_name, self_q, class_cname,
    Len(decls) > 0 ? ", " : "", decls,
    body);
  register_export(Char(swig_name));

  String *raw_call = NewStringf("M._%s(this._ptr%s%s)",
                                 swig_name,
                                 Len(jsargs) > 0 ? "," : "",
                                 jsargs);
  String *marshalled = js_marshal_return(rt, Char(raw_call));
  /* Only emit the JS method on first sighting of a jsname; later overloads
     get their C export but no JS clone (which would clobber).  Proper
     arity-based dispatch is TODO. */
  if (!Getattr(member_names_seen, jsraw)) {
    Setattr(member_names_seen, jsraw, "1");
    Printf(class_js_body,
      "    %s(%s) { return %s; }\n",
      jsraw, jsargs, marshalled);
  }

  Delete(swig_name); Delete(cmangle); Delete(self_q); Delete(jsraw);
  Delete(decls); Delete(args); Delete(jsargs);
  Delete(ret_t); Delete(call_e); Delete(body);
  Delete(raw_call); Delete(marshalled);
  return Language::memberfunctionHandler(n);
}

int WASM_JS::membervariableHandler(Node *n) {
  if (!class_cname) return Language::membervariableHandler(n);
  String *jsname = Getattr(n, "sym:name");
  String *fname  = Getattr(n, "name");
  String *cmangle = mangle(class_cname);
  String *get_n = NewStringf("swig_%s_%s_get", cmangle, fname);
  String *set_n = NewStringf("swig_%s_%s_set", cmangle, fname);
  SwigType *ft  = Getattr(n, "type");
  String *fts   = SwigType_str(ft, 0);

  bool is_string = (Strstr(fts, "std::string") != 0);

  if (is_string) {
    /* getter returns malloc'd char*; setter takes const char* and assigns */
    Printf(class_cpp_section,
      "EMSCRIPTEN_KEEPALIVE char* %s(const %s* self) {"
      " const std::string& __s = self->%s;"
      " char* __buf = (char*)malloc(__s.size()+1);"
      " memcpy(__buf, __s.c_str(), __s.size()+1);"
      " return __buf; }\n"
      "EMSCRIPTEN_KEEPALIVE void %s(%s* self, const char* v) { self->%s = v; }\n",
      get_n, class_cname, fname,
      set_n, class_cname, fname);
    register_export(Char(get_n));
    register_export(Char(set_n));
    Printf(class_js_body,
      "    get %s()  { const __p=M._%s(this._ptr); const __s=M.UTF8ToString(__p); M._free(__p); return __s; }\n"
      "    set %s(v) {\n"
      "      const __n = M.lengthBytesUTF8(v); const __p = M._malloc(__n+1);\n"
      "      M.stringToUTF8(v, __p, __n+1); M._%s(this._ptr, __p); M._free(__p);\n"
      "    }\n",
      jsname, get_n, jsname, set_n);
  } else {
    Printf(class_cpp_section,
      "EMSCRIPTEN_KEEPALIVE %s %s(const %s* self) { return self->%s; }\n"
      "EMSCRIPTEN_KEEPALIVE void %s(%s* self, %s v) { self->%s = v; }\n",
      fts, get_n, class_cname, fname,
      set_n, class_cname, fts, fname);
    register_export(Char(get_n));
    register_export(Char(set_n));
    Printf(class_js_body,
      "    get %s()  { return M._%s(this._ptr); }\n"
      "    set %s(v) { M._%s(this._ptr, v); }\n",
      jsname, get_n, jsname, set_n);
  }

  Delete(get_n); Delete(set_n); Delete(cmangle); Delete(fts);
  return Language::membervariableHandler(n);
}

int WASM_JS::staticmemberfunctionHandler(Node *n) {
  if (!class_cname) return Language::staticmemberfunctionHandler(n);

  String *jsraw  = Getattr(n, "sym:name");
  String *mname  = Getattr(n, "name");
  if (Strstr(mname, "operator")) return Language::staticmemberfunctionHandler(n);
  String *jsname = NewString(jsraw);
  const char *suf = Strstr(jsname, "__SWIG_");
  if (suf) Delslice(jsname, suf - Char(jsname), DOH_END);
  ParmList *p = Getattr(n, "parms");
  SwigType *rt = Getattr(n, "type");
  String *cmangle = mangle(class_cname);
  String *static_key = NewStringf("static_%s", mname);
  int idx = next_index(member_overload_counts, static_key);
  Delete(static_key);
  String *swig_name = NewStringf("swig_%s_static_%s_%d", cmangle, mname, idx);

  String *decls = parm_decls(p);
  String *args  = parm_args(p);
  String *jsargs = js_arg_names(p);
  String *ret_t  = cpp_return_type(rt);
  String *call_e = NewStringf("%s::%s(%s)", class_cname, mname, args);
  String *body   = cpp_call_body(rt, call_e);

  Printf(class_cpp_section,
    "EMSCRIPTEN_KEEPALIVE %s %s(%s) { %s }\n",
    ret_t, swig_name, decls, body);
  register_export(Char(swig_name));

  /* First-wins per (class, jsname) -- same convention as member methods. */
  if (!Getattr(member_names_seen, jsname)) {
    Setattr(member_names_seen, jsname, "1");
    String *js_body = emit_js_body(p, rt, swig_name, "");
    Printf(class_js_body,
      "    static %s(%s) {\n%s    }\n",
      jsname, jsargs, js_body);
    Delete(js_body);
  }

  Delete(swig_name); Delete(jsname); Delete(cmangle); Delete(decls); Delete(args);
  Delete(jsargs); Delete(ret_t); Delete(call_e); Delete(body);
  return Language::staticmemberfunctionHandler(n);
}

int WASM_JS::globalfunctionHandler(Node *n) {
  if (class_cname) return Language::globalfunctionHandler(n);

  String *jsraw = Getattr(n, "sym:name");
  String *fname  = Getattr(n, "name");
  if (Strstr(fname, "operator")) return Language::globalfunctionHandler(n);
  /* Strip __SWIG_N suffix from JS name */
  String *jsname = NewString(jsraw);
  const char *suf = Strstr(jsname, "__SWIG_");
  if (suf) Delslice(jsname, suf - Char(jsname), DOH_END);

  ParmList *p = Getattr(n, "parms");
  SwigType *rt = Getattr(n, "type");
  if (!global_overload_counts) global_overload_counts = NewHash();
  int idx = next_index(global_overload_counts, fname);
  String *swig_name = NewStringf("swig_%s_%d", fname, idx);

  String *decls = parm_decls(p);
  String *args  = parm_args(p);
  String *jsargs = js_arg_names(p);
  String *ret_t  = cpp_return_type(rt);
  String *call_e = NewStringf("%s(%s)", fname, args);
  String *body   = cpp_call_body(rt, call_e);

  Printf(f_cpp_wrappers,
    "EMSCRIPTEN_KEEPALIVE %s %s(%s) { %s }\n",
    ret_t, swig_name, decls, body);
  register_export(Char(swig_name));

  if (!global_names_seen) global_names_seen = NewHash();
  if (!Getattr(global_names_seen, jsname)) {
    Setattr(global_names_seen, jsname, "1");
    String *js_body = emit_js_body(p, rt, swig_name, "");
    Printf(f_js_module,
      "    %s(%s) {\n%s    },\n",
      jsname, jsargs, js_body);
    Delete(js_body);
  }

  Delete(swig_name); Delete(jsname); Delete(decls); Delete(args); Delete(jsargs);
  Delete(ret_t); Delete(call_e); Delete(body);
  return Language::globalfunctionHandler(n);
}

int WASM_JS::enumDeclaration(Node *n) {
  String *jsname = Getattr(n, "sym:name");
  String *cname  = Getattr(n, "name");
  if (!cname || Len(cname) == 0) return Language::enumDeclaration(n);

  enum_cname = cname;
  enum_js_body = NewString("");

  Language::enumDeclaration(n);

  Printf(f_js_module, "    %s: {\n%s    },\n", jsname, enum_js_body);

  Delete(enum_js_body);
  enum_js_body = 0;
  enum_cname = 0;
  return SWIG_OK;
}

int WASM_JS::enumvalueDeclaration(Node *n) {
  if (!enum_cname) return Language::enumvalueDeclaration(n);
  String *jsname = Getattr(n, "sym:name");
  String *vname  = Getattr(n, "name");
  String *emangle = mangle(enum_cname);
  String *swig_name = NewStringf("swig_enum_%s_%s", emangle, vname);
  Printf(f_cpp_wrappers,
    "EMSCRIPTEN_KEEPALIVE int %s() { return (int)(%s::%s); }\n",
    swig_name, enum_cname, vname);
  register_export(Char(swig_name));
  Printf(enum_js_body, "      %s: M._%s(),\n", jsname, swig_name);
  Delete(swig_name); Delete(emangle);
  return Language::enumvalueDeclaration(n);
}

static Language *new_swig_webasm_js() { return new WASM_JS(); }
extern "C" Language *swig_webasm_js(void) { return new_swig_webasm_js(); }
