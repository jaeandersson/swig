/* -----------------------------------------------------------------------------
 * wasm_js.cxx
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
#include <ctype.h>  /* isalnum (cpp_to_ts: drop X:: qualified prefixes) */

static const char *usage = "\
WebAssembly+JS Options (use with -wasm-js):\n\
  -stubs       Emit TypeScript .d.ts declarations alongside .js/.cpp\n";

/* TypeScript stub generation -- mirrors python.cxx's -stubs/.pyi system.
   When `stubs=1`, top() opens a `.d.ts` output file, registers the
   "stubs" file slot, and the per-handler stubEmit*() helpers append
   `declare function` / `class { ... }` signatures.  Off by default;
   user opts in via `swig -wasm-js -stubs`. */
static int stubs = 0;
static File *f_stubs_dts = 0;           /* .d.ts output file */
static String *f_stubs = 0;             /* current receiving buffer */
static String *f_stubs_module = 0;      /* module-level (outside any class) */
static String *f_stubs_class_body = 0;  /* per-class body buffer */

class WASM_JS : public Language {
public:
  WASM_JS()
    : f_out_cpp(0), f_out_js(0), f_out_exports(0),
      f_cpp_runtime(0), f_cpp_header(0), f_cpp_wrappers(0), f_cpp_init(0),
      f_js_pre(0), f_js_classes(0), f_js_module(0),
      class_cname(0), class_jsname(0),
      class_js_body(0), class_cpp_section(0),
      enum_cname(0), enum_js_body(0),
      ctor_overloads(0), ctor_arities_seen(0), ctor_overload_count(0),
      member_names_seen(0), member_overload_counts(0),
      global_names_seen(0), global_overload_counts(0),
      cpp_to_js_class(0),
      cpp_to_js_vector_class(0),
      global_overloads(0),
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

  /* Recursive pre-pass: walk every node in the AST looking for class
     declarations (nodeType "class") and register their C++ name -> JS
     symbol in cpp_to_js_class.  Run before Language::top(n) so that
     js_marshal_return can wrap class-typed returns regardless of
     decl-vs-use order. */
  void prepopulate_class_names(Node *n) {
    if (!n) return;
    String *nt = nodeType(n);
    if (nt && Strcmp(nt, "class") == 0) {
      String *jsname = Getattr(n, "sym:name");
      SwigType *t = Getattr(n, "name");
      if (jsname && t) {
        String *cn = SwigType_str(t, 0);
        Setattr(cpp_to_js_class, cn, jsname);
        /* Track std::vector<...> instantiations separately so call-site
           codegen can auto-convert JS arrays <-> XVector at the boundary. */
        bool is_vector = (Len(cn) > 12 && strncmp(Char(cn), "std::vector<", 12) == 0);
        if (is_vector) {
          if (!cpp_to_js_vector_class) cpp_to_js_vector_class = NewHash();
          Setattr(cpp_to_js_vector_class, cn, jsname);
        }
        if (Len(cn) > 8 && strncmp(Char(cn), "casadi::", 8) == 0) {
          String *bare = NewString(Char(cn) + 8);
          Setattr(cpp_to_js_class, bare, jsname);
          if (is_vector) Setattr(cpp_to_js_vector_class, bare, jsname);
          Delete(bare);
        }
        String *ns = SwigType_namestr(t);
        if (ns && Strcmp(ns, cn) != 0) {
          Setattr(cpp_to_js_class, ns, jsname);
          if (is_vector) Setattr(cpp_to_js_vector_class, ns, jsname);
          if (Len(ns) > 8 && strncmp(Char(ns), "casadi::", 8) == 0) {
            String *bare = NewString(Char(ns) + 8);
            Setattr(cpp_to_js_class, bare, jsname);
            if (is_vector) Setattr(cpp_to_js_vector_class, bare, jsname);
            Delete(bare);
          }
        }
        if (ns) Delete(ns);
        Delete(cn);
      }
    }
    /* Recurse into children + siblings. */
    prepopulate_class_names(firstChild(n));
    prepopulate_class_names(nextSibling(n));
  }

  /* If `rt` is a registered std::vector<...> instantiation, return the
     JS vector class name (e.g. "MXVector"); else 0.  Resolves typedef
     aliases inside template args. */
  String *vector_class_name(SwigType *rt) {
    if (!rt || !cpp_to_js_vector_class) return 0;
    String *cn = SwigType_str(rt, 0);
    const char *cs = Char(cn);
    int n = Len(cn);
    while (n > 0 && (cs[0] == ' ' || cs[0] == '\t')) { cs++; n--; }
    while (n > 0 && (cs[n-1] == ' ' || cs[n-1] == '\t')) n--;
    if (n > 6 && strncmp(cs, "const ", 6) == 0) { cs += 6; n -= 6; }
    for (;;) {
      while (n > 0 && (cs[n-1] == '&' || cs[n-1] == '*' ||
                       cs[n-1] == ' ' || cs[n-1] == '\t')) n--;
      if (n > 6 && strncmp(cs + n - 6, " const", 6) == 0) { n -= 6; continue; }
      break;
    }
    String *bare = NewStringWithSize(cs, n);
    String *hit = (String *)Getattr(cpp_to_js_vector_class, bare);
    if (!hit) {
      SwigType *t = Copy(rt);
      SwigType *resolved = SwigType_typedef_resolve_all(t);
      if (resolved) {
        String *rstr = SwigType_str(resolved, 0);
        const char *rs = Char(rstr);
        int rn = Len(rstr);
        if (rn > 6 && strncmp(rs, "const ", 6) == 0) { rs += 6; rn -= 6; }
        for (;;) {
          while (rn > 0 && (rs[rn-1] == '&' || rs[rn-1] == '*' ||
                            rs[rn-1] == ' ' || rs[rn-1] == '\t')) rn--;
          if (rn > 6 && strncmp(rs + rn - 6, " const", 6) == 0) { rn -= 6; continue; }
          break;
        }
        String *rbare = NewStringWithSize(rs, rn);
        hit = (String *)Getattr(cpp_to_js_vector_class, rbare);
        Delete(rbare); Delete(rstr); Delete(resolved);
      }
      Delete(t);
    }
    Delete(cn); Delete(bare);
    return hit && Len(hit) > 0 ? hit : 0;
  }

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

  /* C++ output accumulators (mirrors matlab.cxx: runtime, header, wrappers,
     init).  Final .cpp is assembled by concatenating them in order at the
     end of top().  "runtime" holds swigrun.swg + wasm-js-specific runtime
     (SWIG_ConvertPtr etc.) and the SwigType type table. */
  String *f_cpp_runtime;
  String *f_cpp_header;
  String *f_cpp_wrappers;
  String *f_cpp_init;
  String *f_js_pre;
  String *f_js_classes;
  String *f_js_module;

  String *class_cname;
  String *class_jsname;
  String *class_js_body;
  String *class_cpp_section;

  String *enum_cname;
  String *enum_js_body;

  /* Per-class ctor overloads: a List of Hash{arity, swig_name,
     dispatch_class, dispatch_idx}.  classHandler builds a JS
     dispatcher that picks per (arity, args[N].constructor.name).
     Replaces the old first-wins-per-arity logic. */
  List   *ctor_overloads;
  Hash   *ctor_arities_seen;
  int    ctor_overload_count;
  Hash   *member_names_seen;       /* per-class: jsname -> "1" (for JS-side dedup) */
  Hash   *member_overload_counts;  /* per-class: jsname -> "N" (next index for C symbol) */
  Hash   *global_names_seen;       /* module-level: jsname -> "1" */
  Hash   *global_overload_counts;  /* module-level: fname -> "N" */
  Hash   *cpp_to_js_class;         /* "casadi::Foo" -> "Foo" (the JS class name).
                                       Populated in classHandler, consulted by
                                       js_marshal_return to wrap class-typed
                                       returns as `new Foo(__PRIVATE_CTOR, ptr)`. */
  Hash   *cpp_to_js_vector_class;  /* C++ vector type -> JS vector class name
                                       (e.g. "std::vector< casadi::MX >" ->
                                       "MXVector").  Subset of cpp_to_js_class
                                       restricted to std::vector<> templates;
                                       lets call-site codegen auto-convert
                                       JS arrays <-> XVector at the boundary
                                       so users see plain arrays, not the
                                       internal vector classes. */
  Hash   *global_overloads;        /* jsname -> List<Hash{arg0_jsclass, body,
                                       arity}>.  Built in globalfunctionHandler,
                                       drained in top() to emit a dispatcher per
                                       jsname (arg0-type based). */
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

  /* Typemap lookups follow SWIG's standard names (ctype/in/out/freearg)
     with the standard $1, $input, $result placeholders.  JS-side hooks use
     wasm-js-specific names (jsin/jsarg/jsfree/jsout) since SWIG has no
     standard for the JS proxy side.

     Convention: each parm has lname set to "arg<N+1>" (matlab.cxx pattern)
     and we treat the C-API input variable as "obj<N>" (substituted into
     $input).  This means an in typemap body  "$1 = std::string($input);"
     emits  "arg1 = std::string(obj0);"  after Swig_typemap_attach_parms +
     our $input Replaceall.  */

  /* Assign canonical lnames/names to each parm. Re-runnable; idempotent. */
  void name_parms(ParmList *p) {
    int i = 1;
    for (Parm *q = p; q; q = nextSibling(q), ++i) {
      String *lname = NewStringf("arg%d", i);
      Setattr(q, "lname", lname);
      if (!Getattr(q, "name")) Setattr(q, "name", lname);
      Delete(lname);
    }
  }

  /* Parm has typemap(in, numinputs=0) — server-side filled, not in wasm sig. */
  bool is_in_numinputs0(Parm *q) {
    return checkAttribute(q, "tmap:in:numinputs", "0");
  }

  /* Obj-name for the input-side (wasm-export signature) variable for a
     parm at survivor-index s. The survivor index advances only for parms
     that ARE in the input signature, so numinputs=0 parms don't take a
     slot.  Returns a fresh string (caller frees). */
  String *obj_name(int s) { return NewStringf("obj%d", s); }

  /* C-side parm declarations: "<ctype> obj0, <ctype> obj1, ...".  Uses
     the standard `ctype` typemap; falls back to the C++ ltype string.
     Skips parms with typemap(in, numinputs=0) — those are server-side
     filled. */
  String *parm_decls(ParmList *p) {
    Swig_typemap_attach_parms("ctype", p, 0);
    Swig_typemap_attach_parms("in", p, 0);   // for numinputs check
    String *out = NewString("");
    int s = 0;  // survivor index (only advances for parms in the C sig)
    for (Parm *q = p; q; q = nextSibling(q)) {
      if (is_in_numinputs0(q)) continue;
      String *tm = Getattr(q, "tmap:ctype");
      String *oname = obj_name(s);
      if (tm) {
        Printf(out, "%s%s %s", s == 0 ? "" : ", ", tm, oname);
      } else {
        SwigType *t = Getattr(q, "type");
        String *ts = SwigType_str(t, 0);
        Printf(out, "%s%s %s", s == 0 ? "" : ", ", ts, oname);
        Delete(ts);
      }
      Delete(oname);
      ++s;
    }
    return out;
  }

  /* Emit "<T> arg<N>; <in-typemap-body>" for parms that have an `in`
     typemap defined.  Parms without an `in` typemap use no local at all
     -- the call site refers to the C-API variable obj<S> directly.

     S is the survivor index (input-only): obj0, obj1 in source order,
     skipping numinputs=0 parms.  Their typemap bodies typically don't
     reference $input (no input to reference), but if they do, the
     reference is to the *next* surviving obj<S>, which would still be
     wrong -- numinputs=0 typemaps with $input references shouldn't
     exist in practice. */
  String *parm_prologue(ParmList *p) {
    Swig_typemap_attach_parms("in", p, 0);
    String *out = NewString("");
    int s = 0;       // 0-based JS-arg index (a0, a1, ...)
    int argnum = 1;  // 1-based SWIG argnum for typemap_locals mangling
    for (Parm *q = p; q; q = nextSibling(q)) {
      bool skip_obj = is_in_numinputs0(q);
      String *tm = Getattr(q, "tmap:in");
      if (tm) {
        SwigType *t = Getattr(q, "type");
        String *lname = Getattr(q, "lname");
        String *ltype = SwigType_lstr(t, 0);
        // Outer-scope local: declared once at wrapper top so the call
        // site (parm_args) can reference it without scope hopping.
        Printf(out, "  %s %s;\n", ltype, lname);
        Delete(ltype);

        // Make a working copy of the body — we may rewrite identifiers
        // in it as we emit `(T m)` locals.  Function scope, not block
        // scope (heap-allocated locals would dangle past block close).
        String *body = Copy(tm);

        // Typemap-declared `(T m)` locals: replicate SWIG core's
        // typemap_locals mangling (rename to `m<argnum>`, substitute
        // in body) -- we bypass `Wrapper *f` so the standard mangling
        // path doesn't fire, and without per-parm mangling, methods
        // with multiple same-type ref args collide.
        Parm *locals = (Parm*)Getattr(q, "tmap:in:locals");
        for (Parm *lp = locals; lp; lp = nextSibling(lp)) {
          SwigType *lt = Getattr(lp, "type");
          String *raw_name = Getattr(lp, "name");
          if (!raw_name || Len(raw_name) == 0) continue;
          String *mangled = NewStringf("%s%d", raw_name, argnum);
          String *lts = SwigType_str(lt, 0);
          Printf(out, "  %s %s;\n", lts, mangled);
          Replace(body, raw_name, mangled, DOH_REPLACE_ID);
          Delete(lts); Delete(mangled);
        }

        // Wrap the typemap body itself in a fresh `{ }` scope.
        Printf(out, "  {\n");
        String *oname = skip_obj ? NewString("__numinputs0_no_input")
                                 : obj_name(s);
        Replaceall(body, "$input", oname);
        Printf(out, "    %s\n", body);
        Printf(out, "  }\n");
        Delete(body); Delete(oname);
      }
      if (!skip_obj) ++s;
      ++argnum;
    }
    return out;
  }

  /* Emit per-parm freearg blocks, $input substituted to obj<S>. */
  String *parm_freeargs(ParmList *p) {
    Swig_typemap_attach_parms("freearg", p, 0);
    Swig_typemap_attach_parms("in", p, 0);   // for numinputs check
    String *out = NewString("");
    int s = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      bool skip_obj = is_in_numinputs0(q);
      String *tm = Getattr(q, "tmap:freearg");
      if (tm) {
        String *body = Copy(tm);
        String *oname = skip_obj ? NewString("__numinputs0_no_input")
                                 : obj_name(s);
        Replaceall(body, "$input", oname);
        Printf(out, "  %s\n", body);
        Delete(body); Delete(oname);
      }
      if (!skip_obj) ++s;
    }
    return out;
  }

  /* C++ call-site arg list. Parms with an `in` typemap pass via the lname
     local (arg<N>) populated by parm_prologue; parms without pass obj<S>
     directly.  Reference-typed parms get a leading `*` since SwigType_lstr
     gave the local pointer type (T* arg1) but the wrapped function takes
     T&.  Attach is idempotent so order vs. parm_prologue doesn't matter. */
  String *parm_args(ParmList *p) {
    Swig_typemap_attach_parms("in", p, 0);
    String *out = NewString("");
    int s = 0;        // survivor index for obj<S>
    bool first = true;
    for (Parm *q = p; q; q = nextSibling(q)) {
      String *tm = Getattr(q, "tmap:in");
      bool skip_obj = is_in_numinputs0(q);
      if (!first) Printv(out, ", ", NIL);
      first = false;
      if (tm) {
        SwigType *t = Getattr(q, "type");
        if (SwigType_isreference(t)) Printv(out, "*", NIL);
        Printv(out, Getattr(q, "lname"), NIL);
      } else {
        String *oname = obj_name(s);
        Printv(out, oname, NIL);
        Delete(oname);
      }
      if (!skip_obj) ++s;
    }
    return out;
  }

  /* JS function-signature parameter list: always plain a0, a1, ...
     (the user-facing JS arg names).  Skips numinputs=0 parms.  Does NOT
     consult `jsarg` — that typemap rewrites the *wasm call expression*,
     not the JS function signature; mixing the two causes the param name
     to be the malloc'd buffer (e.g. `__ps1`) while the body references
     the canonical `a0`, leaving the param unused and `a0` undefined. */
  String *js_arg_names(ParmList *p) {
    Swig_typemap_attach_parms("in", p, 0);
    String *out = NewString("");
    int s = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      if (is_in_numinputs0(q)) continue;
      Printf(out, "%sa%d", s == 0 ? "" : ", ", s);
      ++s;
    }
    return out;
  }

  /* Build the JS-side call-arguments list, auto-unwrapping registered
     class-type parms (`a0` -> `a0._ptr`).  Counterpart to
     js_arg_names() which is for the JS function signature.  Skips
     numinputs=0 parms (server-side filled).  Used by
     memberfunctionHandler / constructorHandler / etc. where
     emit_js_body's richer typemap-aware path is overkill. */
  String *js_call_args(ParmList *p) {
    Swig_typemap_attach_parms("in", p, 0);
    String *out = NewString("");
    int s = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      if (is_in_numinputs0(q)) continue;
      if (s > 0) Printv(out, ", ", NIL);
      if (is_registered_class_type(Getattr(q, "type"))) {
        Printf(out, "__unwrap(a%d)", s);
      } else {
        Printf(out, "a%d", s);
      }
      ++s;
    }
    return out;
  }

  int parm_arity(ParmList *p) {
    int n = 0;
    for (Parm *q = p; q; q = nextSibling(q)) ++n;
    return n;
  }

  /* Emit a JS method body: jsin prologue per parm, wasm call, jsout to
     marshal return, jsfree per parm post-call.  Skips numinputs=0 parms
     entirely (no JS-side input, no wasm-export arg). */
  String *emit_js_body(ParmList *p, SwigType *rt, const String *swig_name, const char *self_prefix) {
    Swig_typemap_attach_parms("jsin",  p, 0);
    Swig_typemap_attach_parms("jsarg", p, 0);
    Swig_typemap_attach_parms("jsfree", p, 0);
    Swig_typemap_attach_parms("in",    p, 0);

    String *out = NewString("");

    /* Prologue: emit per-parm jsin typemaps with $input/$argnum subst. */
    int s = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      if (is_in_numinputs0(q)) continue;
      String *tm = Getattr(q, "tmap:jsin");
      if (tm) {
        String *body  = Copy(tm);
        String *aname = NewStringf("a%d", s);
        String *idxs  = NewStringf("%d", s);
        Replaceall(body, "$input",  aname);
        Replaceall(body, "$argnum", idxs);
        Printv(out, body, NIL);
        Delete(body); Delete(aname); Delete(idxs);
      }
      ++s;
    }

    /* Build call-args list.  Resolution order per parm:
         1. jsarg typemap (e.g. std::string's `__ps$argnum` malloc'd buf).
         2. Class-type auto-unwrap: if the parm's C++ type is a
            registered class (cpp_to_js_class hit), emit `a<S>._ptr`
            so JS callers pass proxy instances directly.
         3. Default: raw `a<S>`. */
    String *call_args = NewString("");
    s = 0;
    bool first = true;
    for (Parm *q = p; q; q = nextSibling(q)) {
      if (is_in_numinputs0(q)) continue;
      if (!first) Printf(call_args, ", ");
      first = false;
      String *tm = Getattr(q, "tmap:jsarg");
      String *aname = NewStringf("a%d", s);
      String *idxs  = NewStringf("%d", s);
      if (tm) {
        String *expr = Copy(tm);
        Replaceall(expr, "$input",  aname);
        Replaceall(expr, "$argnum", idxs);
        Printv(call_args, expr, NIL);
        Delete(expr);
      } else if (is_registered_class_type(Getattr(q, "type"))) {
        /* __unwrap handles null/undefined: passes 0 to the wasm export
           instead of throwing on `null._ptr`.  Lets callers pass null
           for default-arg slots (e.g. an unspecified Dict). */
        Printf(call_args, "__unwrap(%s)", Char(aname));
      } else {
        Printv(call_args, aname, NIL);
      }
      Delete(aname); Delete(idxs);
      ++s;
    }

    String *raw_call;
    if (self_prefix && self_prefix[0]) {
      raw_call = NewStringf("M._%s(%s%s%s)", swig_name, self_prefix,
                            Len(call_args) > 0 ? ", " : "", call_args);
    } else {
      raw_call = NewStringf("M._%s(%s)", swig_name, call_args);
    }
    SwigType *eff_rt = effective_js_return_type(rt, p);
    String *marshalled = js_marshal_return(eff_rt ? eff_rt : rt, Char(raw_call));

    /* Count parms that need post-call cleanup. */
    int n_free = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      if (is_in_numinputs0(q)) continue;
      if (Getattr(q, "tmap:jsfree")) ++n_free;
    }

    if (n_free == 0) {
      Printf(out, "      return %s;\n", marshalled);
    } else {
      Printf(out, "      const __r = %s;\n", marshalled);
      s = 0;
      for (Parm *q = p; q; q = nextSibling(q)) {
        if (is_in_numinputs0(q)) continue;
        String *tm = Getattr(q, "tmap:jsfree");
        if (tm) {
          String *body  = Copy(tm);
          String *aname = NewStringf("a%d", s);
          String *idxs  = NewStringf("%d", s);
          Replaceall(body, "$input",  aname);
          Replaceall(body, "$argnum", idxs);
          Printv(out, body, NIL);
          Delete(body); Delete(aname); Delete(idxs);
        }
        ++s;
      }
      Printf(out, "      return __r;\n");
    }
    Delete(call_args); Delete(raw_call); Delete(marshalled);
    return out;
  }

  /* Is the SwigType a registered class (looking through cv/ref/ptr)?
     Used to auto-unwrap proxy instances at call sites: a parm of type
     `const casadi::MX&` becomes `a0._ptr` in the JS dispatch.  Mirrors
     the resolution logic in js_marshal_return. */
  bool is_registered_class_type(SwigType *rt) {
    if (!rt || !cpp_to_js_class) return false;
    String *cname = SwigType_str(rt, 0);
    const char *cs = Char(cname);
    int cn = Len(cname);
    while (cn > 0 && (cs[0] == ' ' || cs[0] == '\t')) { cs++; cn--; }
    while (cn > 0 && (cs[cn-1] == ' ' || cs[cn-1] == '\t')) cn--;
    if (cn > 6 && strncmp(cs, "const ", 6) == 0) { cs += 6; cn -= 6; }
    for (;;) {
      while (cn > 0 && (cs[cn-1] == '&' || cs[cn-1] == '*' ||
                        cs[cn-1] == ' ' || cs[cn-1] == '\t')) cn--;
      if (cn > 6 && strncmp(cs + cn - 6, " const", 6) == 0) { cn -= 6; continue; }
      break;
    }
    String *bare = NewStringWithSize(cs, cn);
    String *hit = (String *)Getattr(cpp_to_js_class, bare);
    if (!hit && cn > 8 && strncmp(cs, "casadi::", 8) == 0) {
      String *stripped = NewStringWithSize(cs + 8, cn - 8);
      hit = (String *)Getattr(cpp_to_js_class, stripped);
      Delete(stripped);
    }
    if (!hit) {
      SwigType *t = Copy(rt);
      SwigType *resolved = SwigType_typedef_resolve_all(t);
      if (resolved) {
        String *rstr = SwigType_str(resolved, 0);
        const char *rs = Char(rstr);
        int rn = Len(rstr);
        while (rn > 0 && (rs[0] == ' ' || rs[0] == '\t')) { rs++; rn--; }
        while (rn > 0 && (rs[rn-1] == ' ' || rs[rn-1] == '\t')) rn--;
        if (rn > 6 && strncmp(rs, "const ", 6) == 0) { rs += 6; rn -= 6; }
        for (;;) {
          while (rn > 0 && (rs[rn-1] == '&' || rs[rn-1] == '*' ||
                            rs[rn-1] == ' ' || rs[rn-1] == '\t')) rn--;
          if (rn > 6 && strncmp(rs + rn - 6, " const", 6) == 0) { rn -= 6; continue; }
          break;
        }
        String *rbare = NewStringWithSize(rs, rn);
        hit = (String*)Getattr(cpp_to_js_class, rbare);
        if (!hit && rn > 8 && strncmp(rs, "casadi::", 8) == 0) {
          String *stripped = NewStringWithSize(rs + 8, rn - 8);
          hit = (String*)Getattr(cpp_to_js_class, stripped);
          Delete(stripped);
        }
        Delete(rbare); Delete(rstr); Delete(resolved);
      }
      Delete(t);
    }
    Delete(cname); Delete(bare);
    return hit && Len(hit) > 0;
  }

  /* JS-side return marshalling.
       1. If a `jsout` typemap is defined, use it (substituting `$call`).
       2. Else if the return type is a registered class, wrap as
          `new JsName(__PRIVATE_CTOR, $call)` so callers receive a JS
          proxy instance instead of a raw void* pointer.
       3. Else identity (primitive returns: bigint / number / void). */
  String *js_marshal_return(SwigType *rt, const char *expr) {
    if (!rt) return NewString(expr);
    Parm *fake = NewParm(rt, NewString("result"), 0);
    Setattr(fake, "lname", "result");
    Swig_typemap_attach_parms("jsout", fake, 0);
    String *tm = Getattr(fake, "tmap:jsout");
    String *r;
    if (tm) {
      r = Copy(tm);
      Replaceall(r, "$call", expr);
    } else {
      /* Resolve to a bare class name: strip `const`, `&`, `*`.  Then
         look up in cpp_to_js_class.  If found, wrap; else identity. */
      String *cname = SwigType_str(rt, 0);
      const char *cs = Char(cname);
      int cn = Len(cname);
      while (cn > 0 && (cs[0] == ' ' || cs[0] == '\t')) { cs++; cn--; }
      while (cn > 0 && (cs[cn-1] == ' ' || cs[cn-1] == '\t')) cn--;
      if (cn > 6 && strncmp(cs, "const ", 6) == 0) { cs += 6; cn -= 6; }
      for (;;) {
        while (cn > 0 && (cs[cn-1] == '&' || cs[cn-1] == '*' ||
                          cs[cn-1] == ' ' || cs[cn-1] == '\t')) cn--;
        if (cn > 6 && strncmp(cs + cn - 6, " const", 6) == 0) { cn -= 6; continue; }
        break;
      }
      String *bare = NewStringWithSize(cs, cn);
      String *jsname = cpp_to_js_class ? (String*)Getattr(cpp_to_js_class, bare) : 0;
      /* Try typedef-resolved form: turns std::vector< casadi::DM > into
         std::vector< casadi::Matrix<double> >, which is what
         prepopulate_class_names registered. */
      if (!jsname && cpp_to_js_class) {
        SwigType *t = Copy(rt);
        SwigType *resolved = SwigType_typedef_resolve_all(t);
        if (resolved) {
          String *rstr = SwigType_str(resolved, 0);
          const char *rs = Char(rstr);
          int rn = Len(rstr);
          while (rn > 0 && (rs[0] == ' ' || rs[0] == '\t')) { rs++; rn--; }
          while (rn > 0 && (rs[rn-1] == ' ' || rs[rn-1] == '\t')) rn--;
          if (rn > 6 && strncmp(rs, "const ", 6) == 0) { rs += 6; rn -= 6; }
          for (;;) {
            while (rn > 0 && (rs[rn-1] == '&' || rs[rn-1] == '*' ||
                              rs[rn-1] == ' ' || rs[rn-1] == '\t')) rn--;
            if (rn > 6 && strncmp(rs + rn - 6, " const", 6) == 0) { rn -= 6; continue; }
            break;
          }
          String *rbare = NewStringWithSize(rs, rn);
          jsname = (String*)Getattr(cpp_to_js_class, rbare);
          if (!jsname && rn > 8 && strncmp(rs, "casadi::", 8) == 0) {
            String *stripped = NewStringWithSize(rs + 8, rn - 8);
            jsname = (String*)Getattr(cpp_to_js_class, stripped);
            Delete(stripped);
          }
          Delete(rbare); Delete(rstr); Delete(resolved);
        }
        Delete(t);
      }
      /* Also try the namestr form (no spaces around <>) -- SWIG sometimes
         hands us one and the hash holds the other. */
      if (!jsname) {
        SwigType *t = Copy(rt);
        String *namestr = SwigType_namestr(t);
        if (namestr) {
          /* Strip qualifiers from namestr the same way. */
          const char *ns = Char(namestr);
          int nl = Len(namestr);
          while (nl > 0 && (ns[0] == ' ' || ns[0] == '\t')) { ns++; nl--; }
          while (nl > 0 && (ns[nl-1] == ' ' || ns[nl-1] == '\t')) nl--;
          if (nl > 6 && strncmp(ns, "const ", 6) == 0) { ns += 6; nl -= 6; }
          for (;;) {
            while (nl > 0 && (ns[nl-1] == '&' || ns[nl-1] == '*' ||
                              ns[nl-1] == ' ' || ns[nl-1] == '\t')) nl--;
            if (nl > 6 && strncmp(ns + nl - 6, " const", 6) == 0) { nl -= 6; continue; }
            break;
          }
          String *nbare = NewStringWithSize(ns, nl);
          jsname = (String*)Getattr(cpp_to_js_class, nbare);
          if (!jsname && nl > 8 && strncmp(ns, "casadi::", 8) == 0) {
            String *stripped = NewStringWithSize(ns + 8, nl - 8);
            jsname = (String*)Getattr(cpp_to_js_class, stripped);
            Delete(stripped);
          }
          Delete(nbare); Delete(namestr);
        }
        Delete(t);
      }
      /* And try with leading casadi:: stripped from `bare` too. */
      if (!jsname && cn > 8 && strncmp(cs, "casadi::", 8) == 0) {
        String *stripped = NewStringWithSize(cs + 8, cn - 8);
        jsname = (String*)Getattr(cpp_to_js_class, stripped);
        Delete(stripped);
      }
      if (jsname && Len(jsname) > 0) {
        r = NewStringf("new %s(__PRIVATE_CTOR, __from_handle(%s))", Char(jsname), expr);
      } else {
        /* WASM_JS_DEBUG_WRAP=1 logs lookup misses for tuning the
           pre-pass keyset.  Useful when extending casadi.i with new
           class types. */
        if (getenv("WASM_JS_DEBUG_WRAP")) {
          Printf(stderr, "[wasm_js wrap miss] %s\n", cname);
        }
        r = NewString(expr);
      }
      Delete(cname); Delete(bare);
    }
    Delete(fake);
    return r;
  }

  /* C-side return declaration type.  Resolution order:
       1. Explicit `ctype` typemap (covers primitives and casadi types).
       2. Fall back to EM_VAL for class-shaped types (T_USER) so wrappers
          for un-registered classes (e.g. OptiSol / OptiAdvanced, which
          aren't registered via %casadi_typemaps) compile without
          requiring a default ctor on the C++ class.
       3. Otherwise the raw C++ str (primitives that slipped through). */
  String *cpp_return_type(SwigType *rt) {
    if (!rt) return NewString("void");
    String *ts = SwigType_str(rt, 0);
    if (Cmp(ts, "void") == 0) return ts;
    Parm *fake = NewParm(rt, NewString("result"), 0);
    Setattr(fake, "lname", "result");
    Swig_typemap_attach_parms("ctype", fake, 0);
    String *tm = Getattr(fake, "tmap:ctype");
    String *r;
    if (tm) {
      r = Copy(tm); Delete(ts);
    } else {
      /* Resolve typedefs before classifying.  Without resolving,
         typedef-aliased primitives like `std::vector<T>::size_type`
         (which is `unsigned long`) get tagged T_USER and routed
         through the heap-box path -- wrong for primitives. */
      SwigType *resolved = SwigType_typedef_resolve_all(rt);
      SwigType *effective = resolved ? resolved : rt;
      int t = SwigType_type(effective);
      if (t == T_USER) {
        /* Class-shaped types not covered by a ctype typemap fall back to
           the host handle type.  EM_VAL is the wasm-js equivalent of
           PyObject* / mxArray*: an i32 handle into JS's value table. */
        r = NewString("EM_VAL"); Delete(ts);
      } else {
        r = ts;
      }
      if (resolved) Delete(resolved);
    }
    Delete(fake);
    return r;
  }

  // ========================================================================
  //  TypeScript stubs: ports python.cxx's stub system (pickStub /
  //  resolveStub / stubWriteSignature / stubEmitFunction) and renders
  //  Python type expressions (read from `pystub_in` / `pystub_out`
  //  typemap attributes that casadi.i already populates) as TypeScript
  //  type annotations.
  // ========================================================================

  /* Translate a C++ type name (raw `SwigType_namestr` output) to its
     TypeScript equivalent.  Used as the LAST-resort fallback when a
     node has no `pystub_*` attribute -- typical for member-variable
     getters, raw class methods without typemap doc strings, etc.
     Handles primitives, std::string, std::vector<X>, std::map<K,V>;
     strips `casadi::` namespace prefix; unknowns pass through. */
  String *cpp_to_ts(const String *cpp_in) {
    if (!cpp_in || Len(cpp_in) == 0) return NewString("any");
    const char *s = Char((String *)cpp_in);
    int n = Len(cpp_in);
    /* Strip whitespace + leading "const" + trailing `& / * / " const"`. */
    while (n > 0 && (s[0] == ' ' || s[0] == '\t')) { s++; n--; }
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) n--;
    if (n > 6 && strncmp(s, "const ", 6) == 0) { s += 6; n -= 6; }
    for (;;) {
      while (n > 0 && (s[n-1] == '&' || s[n-1] == '*' ||
                       s[n-1] == ' ' || s[n-1] == '\t')) n--;
      if (n > 6 && strncmp(s + n - 6, " const", 6) == 0) { n -= 6; continue; }
      break;
    }
    /* `casadi::Foo` -> `Foo` only at the head -- nested casadi:: in
       template args stays for now (we'll either match via std::vector
       handling below or downgrade to `any`). */
    if (n > 8 && strncmp(s, "casadi::", 8) == 0) { s += 8; n -= 8; }
    String *cpp = NewStringWithSize(s, n);

    /* Primitives.  Note: casadi_int crosses the wasm boundary as i64
       (BigInt in JS); test_mvp.cjs confirms `raw=bigint`. */
    if (Strcmp(cpp, "bool")        == 0) { Delete(cpp); return NewString("boolean"); }
    if (Strcmp(cpp, "int")         == 0) { Delete(cpp); return NewString("number");  }
    if (Strcmp(cpp, "double")      == 0) { Delete(cpp); return NewString("number");  }
    if (Strcmp(cpp, "float")       == 0) { Delete(cpp); return NewString("number");  }
    if (Strcmp(cpp, "long")        == 0) { Delete(cpp); return NewString("number");  }
    if (Strcmp(cpp, "long long")   == 0) { Delete(cpp); return NewString("bigint");  }
    if (Strcmp(cpp, "unsigned long")      == 0) { Delete(cpp); return NewString("number"); }
    if (Strcmp(cpp, "unsigned long long") == 0) { Delete(cpp); return NewString("bigint"); }
    if (Strcmp(cpp, "unsigned int")       == 0) { Delete(cpp); return NewString("number"); }
    if (Strcmp(cpp, "unsigned")           == 0) { Delete(cpp); return NewString("number"); }
    if (Strcmp(cpp, "casadi_int")  == 0) { Delete(cpp); return NewString("bigint");  }
    if (Strcmp(cpp, "size_t")      == 0) { Delete(cpp); return NewString("bigint");  }
    if (Strcmp(cpp, "std::string") == 0) { Delete(cpp); return NewString("string");  }
    if (Strcmp(cpp, "string")      == 0) { Delete(cpp); return NewString("string");  }
    if (Strcmp(cpp, "std::size_t") == 0) { Delete(cpp); return NewString("bigint");  }
    if (Strcmp(cpp, "size_t")      == 0) { Delete(cpp); return NewString("bigint");  }
    if (Strcmp(cpp, "void")        == 0) { Delete(cpp); return NewString("void");    }
    if (Strcmp(cpp, "char")        == 0) { Delete(cpp); return NewString("string");  }
    /* Standard library streams aren't crossed across the wasm boundary -- bind any. */
    if (Strcmp(cpp, "std::istream") == 0 || Strcmp(cpp, "istream") == 0 ||
        Strcmp(cpp, "std::ostream") == 0 || Strcmp(cpp, "ostream") == 0) {
      Delete(cpp); return NewString("any");
    }

    /* std::vector<T>  ->  T[]. */
    const char *cs = Char(cpp);
    int cn = Len(cpp);
    if (cn > 12 && strncmp(cs, "std::vector<", 12) == 0 && cs[cn-1] == '>') {
      String *inner = NewStringWithSize(cs + 12, cn - 13);
      String *inner_ts = cpp_to_ts(inner);
      String *r = NewStringf("%s[]", Char(inner_ts));
      Delete(inner); Delete(inner_ts); Delete(cpp);
      return r;
    }
    /* std::map<K, V>  ->  Record<K, V>. */
    if (cn > 9 && strncmp(cs, "std::map<", 9) == 0 && cs[cn-1] == '>') {
      /* Find top-level comma between K and V. */
      int depth = 0;
      int comma = -1;
      for (int i = 9; i < cn - 1; ++i) {
        if (cs[i] == '<') depth++;
        else if (cs[i] == '>') depth--;
        else if (cs[i] == ',' && depth == 0) { comma = i; break; }
      }
      if (comma > 0) {
        String *k = NewStringWithSize(cs + 9, comma - 9);
        String *v = NewStringWithSize(cs + comma + 1, cn - 1 - comma - 1);
        String *k_ts = cpp_to_ts(k);
        String *v_ts = cpp_to_ts(v);
        String *r = NewStringf("Record<%s, %s>", Char(k_ts), Char(v_ts));
        Delete(k); Delete(v); Delete(k_ts); Delete(v_ts); Delete(cpp);
        return r;
      }
    }
    /* std::pair<A, B>  ->  [A, B]. */
    if (cn > 10 && strncmp(cs, "std::pair<", 10) == 0 && cs[cn-1] == '>') {
      int depth = 0;
      int comma = -1;
      for (int i = 10; i < cn - 1; ++i) {
        if (cs[i] == '<') depth++;
        else if (cs[i] == '>') depth--;
        else if (cs[i] == ',' && depth == 0) { comma = i; break; }
      }
      if (comma > 0) {
        String *a = NewStringWithSize(cs + 10, comma - 10);
        String *b = NewStringWithSize(cs + comma + 1, cn - 1 - comma - 1);
        String *a_ts = cpp_to_ts(a);
        String *b_ts = cpp_to_ts(b);
        String *r = NewStringf("[%s, %s]", Char(a_ts), Char(b_ts));
        Delete(a); Delete(b); Delete(a_ts); Delete(b_ts); Delete(cpp);
        return r;
      }
    }

    /* Pointer / reference suffix: drop -- types cross the wasm
       boundary as opaque references in JS, no `*`/`&` markers. */
    if (cn > 0 && (cs[cn-1] == '*' || cs[cn-1] == '&')) {
      String *base = NewStringWithSize(cs, cn - 1);
      String *r = cpp_to_ts(base);
      Delete(base); Delete(cpp);
      return r;
    }

    /* Anything still carrying `::` or `<>` is a template instantiation
       or qualified name we don't have a clean TS mapping for -- emit
       `any` rather than invalid TS.  The user can refine via
       %typemap(in/out, tsstub_in/tsstub_out=...) on a per-type basis. */
    bool has_template = false, has_qualifier = false;
    for (int i = 0; i < cn; ++i) {
      if (cs[i] == '<' || cs[i] == '>') has_template = true;
      if (cs[i] == ':') has_qualifier = true;
    }
    if (has_template || has_qualifier) {
      Delete(cpp);
      return NewString("any");
    }

    return cpp;  /* class name, hopefully matching the emitted JS proxy */
  }

  /* Pick a TypeScript type string for a parm.  Reads `tmap:in:tsstub_in`
     directly -- casadi.i populates it via the `xTsStub` argument to
     `%casadi_typemaps` / `%casadi_template`, pasted verbatim with no
     translation layer (per user direction: drop py_to_ts).  Falls back
     to rendering the parm's C++ type through cpp_to_ts(). */
  String *pick_ts_stub(Parm *pj, bool is_output) {
    const char *attr = is_output ? "tmap:in:tsstub_out" : "tmap:in:tsstub_in";
    String *raw = Getattr(pj, attr);
    if (raw && Len(raw) > 0) return Copy(raw);
    /* Fallback: render the parm's C++ type through cpp_to_ts.  Handles
       parms whose typemap doesn't carry a `tsstub_*` attribute --
       typical for primitive parms (casadi_int, double, bool) routed
       through the SWIG default `(in)` rather than %casadi_typemaps.
       SwigType_str(t, NIL) produces the normalised C++ source form
       (`const Foo&` etc.) -- cpp_to_ts strips qualifiers/ref/ptr. */
    SwigType *t = Getattr(pj, "type");
    if (t) {
      String *sname = SwigType_str(t, 0);
      String *ts = cpp_to_ts(sname);
      Delete(sname);
      return ts;
    }
    return NewString("any");
  }

  /* Emit a TypeScript signature line for one (overload of a) function-like
     node.  Indent = whitespace prefix (4-space inside classes).
     kind: 0=free fn, 1=member, 2=static member, 3=constructor.
     For TS: `name(arg: T, ...): RetT;` ; constructors are unnamed
     (`constructor(...);`).  Static members are prefixed `static `. */
  void stub_write_signature(String *f, const String *indent, Node *ni, int kind) {
    String *name = Getattr(ni, "sym:name");
    Node *alias = Getattr(ni, "defaultargs");
    Node *source = alias ? alias : ni;

    Printv(f, indent, NIL);
    if (kind == 2) Printv(f, "static ", NIL);
    if (kind == 3) Printv(f, "constructor(", NIL);
    else if (kind == 0) Printv(f, "export function ", name, "(", NIL);
    else Printv(f, name, "(", NIL);

    bool first = true;
    Parm *pj = Getattr(source, "wrap:parms");
    if (!pj) pj = Getattr(source, "parms");
    /* Attach `in` typemap to populate tmap:in:pystub_in / pystub_out
       attributes pick_ts_stub() reads.  Idempotent.  Also "out" for
       reading tmap:out:pystub_out on the return type. */
    if (pj) Swig_typemap_attach_parms("in", pj, 0);
    /* Detect duplicate parm names so we can rename collisions to
       _arg<N>.  Common with overload-typemap parms that share a name
       (`xType &INOUT` -> all named "INOUT" after typemap apply). */
    Hash *seen_names = NewHash();
    for (Parm *q = pj; q; q = nextSibling(q)) {
      String *nm = Getattr(q, "name");
      if (nm && Len(nm) > 0) {
        if (Getattr(seen_names, nm)) Setattr(seen_names, nm, "dup");
        else Setattr(seen_names, nm, "1");
      }
    }
    int auto_idx = 0;
    while (pj) {
      bool is_input = !checkAttribute(pj, "tmap:in:numinputs", "0");
      if (is_input) {
        String *pname = Getattr(pj, "name");
        if (!pname) pname = Getattr(pj, "lname");
        bool is_self = pname && Strcmp(pname, "self") == 0;
        if (!is_self) {
          String *ts = pick_ts_stub(pj, false);
          if (!first) Printv(f, ", ", NIL);
          /* Duplicate-name collision (e.g. multiple `&INOUT` parms): use
             positional _argN. */
          bool dup = pname && Strcmp((String *)Getattr(seen_names, pname), "dup") == 0;
          if (dup) {
            Printf(f, "_arg%d", auto_idx);
          } else if (pname && Len(pname) > 0 && Strcmp(pname, "self") != 0) {
            /* Suffix with `_` to avoid clashes with TypeScript reserved
               words / strict-mode keywords (var, function, new, class,
               default, etc.).  Cheap heuristic vs. a full keyword list:
               only the names that actually appear in the casadi.i
               C++ API need handling; rename them all uniformly with
               `_` suffix when matching. */
            if (Strcmp(pname, "var") == 0 || Strcmp(pname, "function") == 0 ||
                Strcmp(pname, "default") == 0 || Strcmp(pname, "class") == 0 ||
                Strcmp(pname, "new") == 0 || Strcmp(pname, "delete") == 0 ||
                Strcmp(pname, "in") == 0 || Strcmp(pname, "of") == 0 ||
                Strcmp(pname, "return") == 0 || Strcmp(pname, "package") == 0 ||
                Strcmp(pname, "private") == 0 || Strcmp(pname, "public") == 0 ||
                Strcmp(pname, "protected") == 0 || Strcmp(pname, "enum") == 0 ||
                Strcmp(pname, "interface") == 0 || Strcmp(pname, "let") == 0 ||
                Strcmp(pname, "const") == 0 || Strcmp(pname, "yield") == 0 ||
                Strcmp(pname, "import") == 0 || Strcmp(pname, "export") == 0) {
              Printf(f, "%s_", pname);
            } else {
              Printv(f, pname, NIL);
            }
          } else {
            Printf(f, "_arg%d", auto_idx);
          }
          String *pvalue = Getattr(pj, "value");
          if (pvalue && Len(pvalue) > 0) Printv(f, "?", NIL);
          Printv(f, ": ", ts, NIL);
          Delete(ts);
          first = false;
        }
      }
      auto_idx++;
      Parm *pk = Getattr(pj, "tmap:in:next");
      pj = pk ? pk : nextSibling(pj);
    }

    Delete(seen_names);

    if (kind == 3) {
      Printv(f, ");\n", NIL);  /* constructors have no return clause */
    } else {
      Printv(f, "): ", NIL);
      bool is_void = checkAttribute(source, "type", "void");
      if (is_void) {
        Printv(f, "void", NIL);
      } else {
        /* tsstub_out is populated by casadi.i's xTsStub macro arg --
           paste verbatim. */
        String *po = Getattr(source, "tmap:out:tsstub_out");
        if (po && Len(po) > 0) {
          Printv(f, po, NIL);
        } else {
          /* Final fallback: render the SwigType name through cpp_to_ts. */
          SwigType *rt = Getattr(source, "type");
          String *sname = rt ? SwigType_str(rt, 0) : 0;
          if (sname) {
            String *ts = cpp_to_ts(sname);
            Printv(f, ts, NIL);
            Delete(sname); Delete(ts);
          } else {
            Printv(f, "any", NIL);
          }
        }
      }
      Printv(f, ";\n", NIL);
    }
  }

  void stub_emit_function(Node *n, const String *indent, int kind) {
    if (!stubs || !f_stubs) return;
    List *dispatch = Swig_overload_rank(n, true);
    int nfunc = dispatch ? Len(dispatch) : 0;
    List *single = 0;
    if (nfunc == 0) {
      single = NewList();
      Append(single, n);
      dispatch = single;
      nfunc = 1;
    }
    for (int i = 0; i < nfunc; ++i) {
      Node *ni = Getitem(dispatch, i);
      stub_write_signature(f_stubs, indent, ni, kind);
    }
    if (single) Delete(single);
  }

  /* Member-variable annotation: `name: T;` (or readonly if appropriate). */
  void stub_emit_variable(Node *n, const String *indent) {
    if (!stubs || !f_stubs) return;
    String *symname = Getattr(n, "sym:name");
    if (!symname) return;
    String *ts = 0;
    String *po = Getattr(n, "tmap:out:tsstub_out");
    if (po && Len(po) > 0) {
      ts = Copy(po);
    } else {
      SwigType *t = Getattr(n, "type");
      if (t) {
        String *sname = SwigType_str(t, 0);
        ts = cpp_to_ts(sname);
        Delete(sname);
      } else {
        ts = NewString("any");
      }
    }
    Printv(f_stubs, indent, symname, ": ", ts, ";\n", NIL);
    Delete(ts);
  }

  /* Build the full C-wrapper body: locals, in typemaps, call, out typemap,
     freearg, return.  call_expr should use the lnames (arg1, arg2, ...)
     declared by the in typemaps.  For void returns the call is just
     emitted as a statement. */
  /* True if rt is a genuine user-defined type (class/struct) after
     typedef resolution.  A typedef-aliased primitive like
     `std::vector<T>::size_type` (resolves to `unsigned long`) has
     SwigType_type == T_USER pre-resolve, but it's a primitive and
     shouldn't be heap-boxed. */
  bool is_genuinely_user_type(SwigType *rt) {
    if (!rt) return false;
    if (SwigType_type(rt) != T_USER) return false;
    SwigType *resolved = SwigType_typedef_resolve_all(rt);
    SwigType *t = resolved ? resolved : rt;
    bool ok = (SwigType_type(t) == T_USER);
    if (resolved) Delete(resolved);
    return ok;
  }

  /* Count parms carrying an `argout` typemap (e.g. `xType &OUTPUT`).
     These contribute extra outputs that the wrapper packs into its
     return value.  Idempotent attach. */
  int count_argouts(ParmList *p) {
    Swig_typemap_attach_parms("argout", p, 0);
    int n = 0;
    for (Parm *q = p; q; q = nextSibling(q)) {
      String *tm = Getattr(q, "tmap:argout");
      if (tm && Len(tm) > 0) ++n;
    }
    return n;
  }

  /* Emit each parm's argout typemap body, with $1 substituted to the
     parm's lname.  Bodies use the SWIGWASMJS %append_output expansion
     (defined in wasm_js.swg) which writes into _argouts. */
  String *emit_argout_bodies(ParmList *p) {
    Swig_typemap_attach_parms("argout", p, 0);
    String *out = NewString("");
    for (Parm *q = p; q; q = nextSibling(q)) {
      String *tm = Getattr(q, "tmap:argout");
      if (!tm || Len(tm) == 0) continue;
      String *body = Copy(tm);
      String *lname = Getattr(q, "lname");
      if (lname) Replaceall(body, "$1", lname);
      Printf(out, "  %s\n", body);
      Delete(body);
    }
    return out;
  }

  /* When the function has argouts, the JS-visible "return" is no
     longer the C++ rt -- it's the (single) argout's type for n=1.
     Used by handlers building the JS call site so js_marshal_return
     wraps the right proxy class.  For multi-argout or rt-and-argouts
     cases the wrapper packs into a heap void**; JS-side unpacking is
     emitted by the caller. */
  SwigType *effective_js_return_type(SwigType *rt, ParmList *p) {
    int n = count_argouts(p);
    if (n == 0) return rt;
    String *ts = rt ? SwigType_str(rt, 0) : 0;
    bool is_void = !rt || (ts && Cmp(ts, "void") == 0);
    if (ts) Delete(ts);
    if (n == 1 && is_void) {
      for (Parm *q = p; q; q = nextSibling(q)) {
        String *tm = Getattr(q, "tmap:argout");
        if (tm && Len(tm) > 0) return Getattr(q, "type");
      }
    }
    return 0;  /* multi-argout / rt+argout: caller emits unpacker */
  }

  /* The wrapper's actual C return type, accounting for argouts.
       - rt void  + 0 argouts -> "void"
       - rt void  + >=1 argouts -> "void*"  (single ptr or heap array)
       - rt T     + 0 argouts -> cpp_return_type(rt)  (existing)
       - rt T     + >=1 argouts -> "void*"  (packed)
     Callers (memberfunctionHandler etc.) use this to emit the
     EMSCRIPTEN_KEEPALIVE wrapper signature. */
  String *effective_return_ctype(SwigType *rt, ParmList *p) {
    int n_argouts = count_argouts(p);
    if (n_argouts == 0) {
      if (!rt) return NewString("void");
      String *ts = SwigType_str(rt, 0);
      if (Cmp(ts, "void") == 0) return ts;
      Delete(ts);
      return cpp_return_type(rt);
    }
    return NewString("void*");
  }

  String *cpp_call_body(ParmList *p, SwigType *rt, const String *call_expr) {
    String *out = NewString("");

    /* Locals + in conversions. */
    String *prologue = parm_prologue(p);
    Printv(out, prologue, NIL);
    Delete(prologue);

    /* Argout handling: count, declare a fixed-size slot array, emit the
       per-parm `argout` body (uses wasm_js.swg's %append_output macro
       that writes into _argouts via _argout_idx). */
    int n_argouts = count_argouts(p);
    if (n_argouts > 0) {
      Printf(out, "  void* _argouts[%d] = {0};\n", n_argouts);
      Printf(out, "  int _argout_idx = 0;\n");
    }

    /* void return + no argouts: emit call as statement, run freearg, return. */
    String *ts = rt ? SwigType_str(rt, 0) : NewString("void");
    bool is_void_rt = (!rt || Cmp(ts, "void") == 0);
    Delete(ts);
    if (is_void_rt && n_argouts == 0) {
      Printf(out, "  %s;\n", call_expr);
      String *frees = parm_freeargs(p);
      Printv(out, frees, NIL);
      Delete(frees);
      return out;
    }

    /* void return + argouts: call as statement, emit argout bodies,
       pack and return.  Skip the rest of the result-binding logic. */
    if (is_void_rt) {
      Printf(out, "  %s;\n", call_expr);
      String *argouts_body = emit_argout_bodies(p);
      Printv(out, argouts_body, NIL);
      Delete(argouts_body);
      String *frees = parm_freeargs(p);
      Printv(out, frees, NIL);
      Delete(frees);
      /* Pack: single output returned directly; multiple packed into a
         heap void** with caller responsible for freeing each + the
         array (JS-side proxy handles cleanup). */
      if (n_argouts == 1) {
        Printf(out, "  return _argouts[0];\n");
      } else {
        Printf(out, "  void** _packed = (void**)malloc(%d * sizeof(void*));\n", n_argouts);
        Printf(out, "  for (int _i = 0; _i < %d; ++_i) _packed[_i] = _argouts[_i];\n", n_argouts);
        Printf(out, "  return (void*)_packed;\n");
      }
      return out;
    }

    /* Non-void: declare result, run call, run out typemap, freearg, return.
       Use SWIG core's Swig_cresult() to build the assignment line — it
       dispatches on SwigType_type(rt) to insert `& <cast>` for
       T_REFERENCE returns (matching matlab.cxx's `result = (T *) &foo()`
       pattern) and a plain `=` for T_USER value returns.

       For value class returns we wrap with SwigValueWrapper<T> (the
       Fulton transform): cplus_value_type returns that wrapper for any
       class without a public default ctor or that isn't copy-assignable,
       and null for "normal" types (where the original type works).  This
       matches emit_return_variable() in Source/Modules/emit.cxx and is
       what matlab.cxx ends up with via Swig_typemap_lookup_out + the
       standard "out" typemap library. */
    SwigType *vt    = cplus_value_type(rt);
    SwigType *tt    = vt ? vt : rt;
    SwigType *ltype = SwigType_ltype(tt);
    String *result_decl = SwigType_str(ltype, NewString("result"));
    if (SwigType_ispointer(ltype)) {
      Printf(out, "  %s = 0;\n", result_decl);
    } else {
      Printf(out, "  %s;\n", result_decl);
    }
    Delete(result_decl);
    Delete(ltype);
    if (vt) Delete(vt);
    /* Swig_cresult emits `result = (T*) & <call>;` for refs,
       `result = <call>;` for user values, etc. — matlab pattern. */
    String *cres = Swig_cresult(rt, NewString("result"), call_expr);
    Printf(out, "  %s\n", cres);
    Delete(cres);

    /* out typemap: $1 substituted to "result" by attach (lname=result).
       $result substituted manually to "_outv". */
    Parm *fake = NewParm(rt, NewString("result"), 0);
    Setattr(fake, "lname", "result");
    Swig_typemap_attach_parms("out", fake, 0);
    String *tm = Getattr(fake, "tmap:out");
    String *ctype = cpp_return_type(rt);
    Printf(out, "  %s _outv;\n", ctype);
    if (tm) {
      String *body = Copy(tm);
      Replaceall(body, "$result", "_outv");
      Printf(out, "  %s\n", body);
      Delete(body);
    } else if (is_genuinely_user_type(rt)) {
      /* T_USER + no explicit `out` typemap: heap-clone the result so
         JS owns the lifetime, return the pointer.  Matches matlab's
         standard SWIGTYPE-out lowering (`new T(result)`).  Works with
         SwigValueWrapper<T> result via its `operator T&()`.  We can't
         provide this via a `%typemap(out) SWIGTYPE` in wasm_js.swg
         because SWIG resolves typedefs (casadi_int -> long long) before
         matching and SWIGTYPE would shadow registered primitive-typedef
         `(out)` typemaps. */
      SwigType *lt = SwigType_ltype(rt);
      String *lt_str = SwigType_str(lt, 0);
      /* T_USER + no explicit `out` typemap: heap-clone the result and
         wrap as an EM_VAL via SWIG_NewPointerObj (the shared runtime
         in Lib/wasm_js/wasm_jsrun.swg builds the JS-side
         `{_ptr: <int>}` carrier).  Used by SWIGTYPE-default outputs
         that don't go through casadi.i's `%casadi_output_typemaps`.
         Stash the `new` result in a local first: lt_str may contain
         template commas (e.g. std::pair<A, B>), which the C
         preprocessor would otherwise tokenize as multiple macro
         args. */
      Printf(out, "  %s* _heap = new %s(result);\n", lt_str, lt_str);
      Printf(out, "  _outv = SWIG_NewPointerObj(_heap, 0, SWIG_POINTER_OWN);\n");
      Delete(lt_str); Delete(lt);
    } else {
      Printf(out, "  _outv = result;\n");  /* identity default */
    }
    Delete(fake); Delete(ctype);

    /* Argout bodies fire after the call (operate on populated arg
       locals) but before freearg.  Their %append_output expansion
       writes into _argouts[]. */
    if (n_argouts > 0) {
      String *argouts_body = emit_argout_bodies(p);
      Printv(out, argouts_body, NIL);
      Delete(argouts_body);
    }

    String *frees = parm_freeargs(p);
    Printv(out, frees, NIL);
    Delete(frees);

    /* Return packing:
         - 0 argouts: just _outv.
         - argouts present: pack (_outv, _argouts...) into heap void**.
           The wrapper-level effective_return_ctype is void* in this case. */
    if (n_argouts == 0) {
      Printf(out, "  return _outv;\n");
    } else {
      int total = n_argouts + 1;
      Printf(out, "  void** _packed = (void**)malloc(%d * sizeof(void*));\n", total);
      Printf(out, "  _packed[0] = (void*)_outv;\n");
      Printf(out, "  for (int _i = 0; _i < %d; ++_i) _packed[1+_i] = _argouts[_i];\n", n_argouts);
      Printf(out, "  return (void*)_packed;\n");
    }
    return out;
  }
};

// ============================ main / top ====================================

void WASM_JS::main(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (!argv[i]) continue;
    if (strcmp(argv[i], "-help") == 0) {
      fputs(usage, stdout);
    } else if (strcmp(argv[i], "-stubs") == 0) {
      stubs = 1;
      /* Expose a preprocessor symbol so .i files can %ifdef-gate
         %stub_* macros for backward compat with un-patched SWIG.
         Same name as the python target's -stubs uses (set by
         python.cxx) so existing casadi.i guards work identically. */
      Preprocessor_define("SWIG_STUBS_ENABLED", 0);
      Swig_mark_arg(i);
    }
  }
  SWIG_library_directory("wasm_js");
  Preprocessor_define("SWIGWASMJS 1", 0);
  SWIG_config_file("wasm_js.swg");
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

  /* TypeScript stubs: open <outfile-without-cpp>.d.ts and register
     "stubs" file slot for collecting declarations.  Mirrors
     python.cxx's -stubs / .pyi handling (lines 733-760). */
  if (stubs) {
    String *dtsfile = NewString(outfile);
    String *dext = Swig_file_extension(dtsfile);
    if (dext && Len(dext)) Delslice(dtsfile, Len(dtsfile) - Len(dext), DOH_END);
    Append(dtsfile, ".d.ts");
    f_stubs_dts = NewFile(dtsfile, "w", SWIG_output_files());
    if (!f_stubs_dts) { FileErrorDisplay(dtsfile); Exit(EXIT_FAILURE); }
    Delete(dtsfile);
    f_stubs_module = NewString("");
    f_stubs = f_stubs_module;
    f_stubs_class_body = 0;
    Swig_register_filebyname("stubs", f_stubs_module);
    /* Stub-alias tables exist for python compatibility — user .i files
       may target them via %insert("stubs_alias_in_table") etc.  We
       drain them in resolveStub() below, same protocol as python. */
    String *f_alias_in  = NewString("");
    String *f_alias_out = NewString("");
    String *f_preamble  = NewString("");
    Swig_register_filebyname("stubs_alias_in_table",  f_alias_in);
    Swig_register_filebyname("stubs_alias_out_table", f_alias_out);
    Swig_register_filebyname("stubs_preamble",        f_preamble);
    /* Stash for top()-end retrieval. */
    Setattr(n, "wasm_js:f_stubs_alias_in",  f_alias_in);
    Setattr(n, "wasm_js:f_stubs_alias_out", f_alias_out);
    Setattr(n, "wasm_js:f_stubs_preamble",  f_preamble);
  }

  f_cpp_runtime  = NewString("");
  f_cpp_header   = NewString("");
  f_cpp_wrappers = NewString("");
  f_cpp_init     = NewString("");
  f_js_pre       = NewString("");
  f_js_classes   = NewString("");
  f_js_module    = NewString("");

  /* Standard SWIG file-slot wiring (matches matlab.cxx ordering).
     "runtime" receives swigrun.swg + wasm_jsrun.swg via %insert(runtime)
     from Lib/wasm_js/wasm_jsruntime.swg.  "header" gathers user
     %{...%} blocks.  "wrapper" is the main accumulator for per-class /
     per-function generated wrappers — but wasm-js generates wrappers
     itself into f_cpp_wrappers via the handlers below, so the wrapper
     slot is unused in practice.  "init" + "begin" are unused for
     wasm-js (no MEX-style entry point). */
  Swig_register_filebyname("runtime", f_cpp_runtime);
  Swig_register_filebyname("header",  f_cpp_header);
  Swig_register_filebyname("wrapper", f_cpp_wrappers);
  Swig_register_filebyname("init",    f_cpp_init);
  Swig_register_filebyname("begin",   f_cpp_runtime);

  Printf(f_out_exports, "_malloc\n_free\n");

  /* Shared sentinel that lets derived ctors pass a pre-allocated ptr up to
     their base ctor through super(), avoiding double-allocation. */
  Printf(f_js_pre,
    "  const __PRIVATE_CTOR = Symbol('swig-private-ctor');\n\n");

  /* JS <-> wasm handle bridge (the wasm-js analog of mxArray * / PyObject *).
     Class-typed parameters and returns flow as EM_VAL handles -- i32
     indices into Embind's value table.  __unwrap converts a JS proxy
     instance (which carries `_ptr`) to an EM_VAL via the embind
     translator `__swig_take_handle` (registered by wasm_jsrun.swg).
     __from_handle is the reverse for returns: pulls the JS-side carrier
     object back out of an EM_VAL via `__swig_release_handle` and reads
     `_ptr` to feed into the proxy class constructor. */
  Printf(f_js_pre,
    "  const __unwrap      = a => M.__swig_take_handle(a);\n"
    "  const __from_handle = h => M.__swig_release_handle(h)._ptr;\n"
    "  const __unwrap_args = (...args) => args.map(__unwrap);\n\n");

  /* Array <-> vector marshalling.  Users only ever pass / receive plain
     JS arrays; the XVector classes are an internal SWIG implementation
     detail.  __arr_to_vec builds a temp vector from a JS array (or
     accepts an already-built XVector pass-through).  __vec_to_arr is
     the reverse for returns: drains the vector into a JS array and
     deletes it.  Mirrors Python/MATLAB list/cell idioms. */
  Printf(f_js_pre,
    "  function __arr_to_vec(arr, VecClass) {\n"
    "    if (arr && arr.constructor && arr.constructor.name === VecClass.name) return arr;\n"
    "    const v = new VecClass();\n"
    "    if (arr) for (const x of arr) v.push_back(x);\n"
    "    return v;\n"
    "  }\n"
    "  function __vec_to_arr(vec) {\n"
    "    if (!vec) return [];\n"
    "    const n = Number(vec.size());\n"
    "    const out = new Array(n);\n"
    "    for (let i = 0; i < n; ++i) out[i] = vec.at(i);\n"
    "    if (vec.delete) vec.delete();\n"
    "    return out;\n"
    "  }\n\n");

  /* Overload-dispatch type predicates.  Mirrors matlab's pattern: each
     candidate overload AND's `casadi::to_ptr(argv[i], <expected>**)`
     across all class-typed parms (skipping the actual conversion via
     the null `m` arg).  Here in JS we encode the type check directly:
     a class-typed parm accepts the wrapping JS proxy class or null;
     a vector-typed parm accepts a JS array (with elem-class check
     when non-empty) or the corresponding XVector proxy.  Empty arrays
     pass any vector check by design -- matching matlab's `to_ptr`
     behaviour where an empty container matches all element types and
     declaration order breaks the tie. */
  Printf(f_js_pre,
    "  function __can(arg, ClassName) {\n"
    "    if (arg == null) return true;\n"
    "    return arg && typeof arg === 'object' && arg.constructor && arg.constructor.name === ClassName;\n"
    "  }\n"
    "  function __can_vec(arg, ElemClassName, VecClassName) {\n"
    "    if (arg == null) return true;\n"
    "    if (Array.isArray(arg)) { return arg.length === 0 || (arg[0] && arg[0].constructor && arg[0].constructor.name === ElemClassName); }\n"
    "    return arg.constructor && arg.constructor.name === VecClassName;\n"
    "  }\n\n");

  /* JS-side multiple-inheritance shim.  SWIG picks one C++ base for the
     `class D extends B` link, but casadi types like Matrix<SXElem> (=SX)
     inherit several bases in C++ (MatrixCommon + GenericExpression +
     GenericMatrix + Printable).  __mixin copies static methods and
     prototype methods from the auxiliary bases onto D so users see a
     unified interface -- e.g. `M.SX.sym(...)` works because GenSX's
     static methods get mixed into SX. */
  Printf(f_js_pre,
    "  function __mixin(D, ...Bs) {\n"
    "    for (const B of Bs) {\n"
    "      if (!B) continue;\n"
    "      for (const k of Object.getOwnPropertyNames(B)) {\n"
    "        if (k === 'length' || k === 'name' || k === 'prototype') continue;\n"
    "        if (Object.prototype.hasOwnProperty.call(D, k)) continue;\n"
    "        Object.defineProperty(D, k, Object.getOwnPropertyDescriptor(B, k));\n"
    "      }\n"
    "      if (!B.prototype) continue;\n"
    "      for (const k of Object.getOwnPropertyNames(B.prototype)) {\n"
    "        if (k === 'constructor') continue;\n"
    "        if (Object.prototype.hasOwnProperty.call(D.prototype, k)) continue;\n"
    "        Object.defineProperty(D.prototype, k, Object.getOwnPropertyDescriptor(B.prototype, k));\n"
    "      }\n"
    "    }\n"
    "  }\n\n");

  /* Pre-walk the AST to collect all class declarations into
     cpp_to_js_class.  Without this, js_marshal_return misses class
     types that haven't been classHandler'd yet -- e.g. `static sym`
     methods of GenericMatrix<Matrix<SXElem>> emit before
     classHandler for Matrix<SXElem> (= SX) runs.  Pre-population
     fixes the ordering so any method's return type can be wrapped
     regardless of declaration order. */
  if (!cpp_to_js_class) cpp_to_js_class = NewHash();
  prepopulate_class_names(n);

  Language::top(n);

  /* Emit free-function dispatchers from global_overloads.  For each
     jsname with multiple overloads, pick at runtime by first-arg JS
     class name (e.g. MXVector -> the MX overload).  Single-overload
     names emit the body inline -- no dispatch overhead. */
  if (global_overloads) {
    Iterator it = First(global_overloads);
    while (it.key) {
      String *jsn = (String *)it.key;
      List *overloads = (List *)it.item;
      int nover = Len(overloads);
      if (nover == 1) {
        Hash *e = (Hash *)Getitem(overloads, 0);
        Printf(f_js_module, "    %s(%s) {\n%s    },\n",
          jsn, (String *)Getattr(e, "jsargs"), (String *)Getattr(e, "body"));
      } else {
        Printf(f_js_module, "    %s(...args) {\n", jsn);
        /* Emit a chain of `if (args[0]?.constructor?.name === 'X') { body }`
           branches.  Bodies were built with arg names a0/a1/...; remap
           on the fly via destructuring. */
        for (int i = 0; i < nover; ++i) {
          Hash *e = (Hash *)Getitem(overloads, i);
          String *arg0c = (String *)Getattr(e, "arg0_class");
          String *jsargs_e = (String *)Getattr(e, "jsargs");
          String *body  = (String *)Getattr(e, "body");
          if (Len(arg0c) > 0) {
            bool is_vec = false;
            String *elem_class = 0;
            if (cpp_to_js_vector_class) {
              Iterator vit = First(cpp_to_js_vector_class);
              while (vit.key) {
                if (Strcmp((String *)vit.item, arg0c) == 0) { is_vec = true; break; }
                vit = Next(vit);
              }
            }
            if (is_vec) {
              int al = Len(arg0c);
              if (al > 6 && strcmp(Char(arg0c) + al - 6, "Vector") == 0) {
                elem_class = NewStringWithSize(Char(arg0c), al - 6);
              }
            }
            String *cond;
            if (is_vec && elem_class) {
              cond = NewStringf("__can_vec(args[0], '%s', '%s')", Char(elem_class), arg0c);
              Delete(elem_class);
            } else {
              cond = NewStringf("__can(args[0], '%s')", arg0c);
            }
            Printf(f_js_module,
              "      if (%s) {\n"
              "        const [%s] = args;\n"
              "%s"
              "      }\n",
              cond, jsargs_e, body);
            Delete(cond);
          } else {
            /* Fallback overload -- accepts any arg shape.  Place last. */
            Printf(f_js_module,
              "      {\n"
              "        const [%s] = args;\n"
              "%s"
              "      }\n",
              jsargs_e, body);
          }
        }
        Printf(f_js_module,
          "      throw new TypeError(`%s: no overload matches arg types`);\n"
          "    },\n",
          jsn);
      }
      it = Next(it);
    }
  }

  /* Emit the SWIG type table. f_cpp_wrappers is passed as the scope
     context for SwigType_emit_type_table (matlab pattern, line 470). */
  SwigType_emit_type_table(f_cpp_runtime, f_cpp_wrappers);

  // C++ output.  Runtime block (swigrun.swg + wasm-js runtime + type
  // table) is dumped first, BEFORE the extern "C" wrapper section, so
  // SWIG_ConvertPtr / swig_type_info* table are declared in scope of
  // every wrapper.  The user %{...%} header block follows runtime so
  // user code can reference runtime APIs.  Wrappers live inside an
  // extern "C" block so emscripten exports get C linkage.
  Printf(f_out_cpp,
    "// Generated by SWIG -wasm-js (NO Embind).  Do not edit.\n\n"
    "#include <emscripten.h>\n"
    "#include <cstdlib>\n"
    "#include <cstring>\n"
    "#include <string>\n"
    "%s\n"
    "%s\n"
    "extern \"C\" {\n\n%s\n} // extern \"C\"\n",
    f_cpp_runtime, f_cpp_header, f_cpp_wrappers);

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
    "// Generated by SWIG -wasm-js (NO Embind).  Do not edit.\n"
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

  /* TypeScript stubs: dump module body to .d.ts and close.  Do NOT
     emit the stubs_preamble slot: it's populated by python-syntax
     %stub_alias_in expansions (`_DM = bool | int | float | ...`) that
     aren't valid TypeScript.  Auto-generated stubs already translate
     PEP-484 types via py_to_ts; the alias-table widening is a python-
     specific refinement we skip for now. */
  if (stubs && f_stubs_dts) {
    Printv(f_stubs_dts,
      "// Generated by SWIG -wasm-js -stubs. Do not edit.\n\n",
      NIL);
    if (f_stubs_module && Len(f_stubs_module) > 0)
      Printv(f_stubs_dts, f_stubs_module, NIL);
    Delete(f_stubs_dts); f_stubs_dts = 0;
    Delete(f_stubs_module); f_stubs_module = 0;
    String *preamble = (String *)Getattr(n, "wasm_js:f_stubs_preamble");
    if (preamble) Delete(preamble);
    String *ain  = (String *)Getattr(n, "wasm_js:f_stubs_alias_in");
    String *aout = (String *)Getattr(n, "wasm_js:f_stubs_alias_out");
    if (ain)  Delete(ain);
    if (aout) Delete(aout);
  }

  Delete(base);
  Delete(f_cpp_runtime);
  Delete(f_cpp_header);
  Delete(f_cpp_wrappers);
  Delete(f_cpp_init);
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

  /* Register cname -> jsname so js_marshal_return can wrap class-typed
     returns as `new JsName(__PRIVATE_CTOR, ptr)`.  Without this, methods
     returning casadi types would return raw void* pointers, forcing the
     hand-rolled JS layer (the old mvp.js) -- which is what we're
     replacing.

     Register multiple variants so lookup matches different
     SwigType_str spacings.  SWIG sometimes formats template args with
     spaces around `<>` and sometimes without; keep both. */
  if (!cpp_to_js_class) cpp_to_js_class = NewHash();
  Setattr(cpp_to_js_class, cname, class_jsname);
  /* Also register without "casadi::" prefix, for returns whose
     SwigType_str didn't expand the namespace. */
  if (Len(cname) > 8 && strncmp(Char(cname), "casadi::", 8) == 0) {
    String *bare = NewString(Char(cname) + 8);
    Setattr(cpp_to_js_class, bare, class_jsname);
    Delete(bare);
  }
  /* And the SwigType_namestr form (camel-cased, no spaces around <>). */
  String *nstr = SwigType_namestr(t);
  if (nstr && Strcmp(nstr, cname) != 0) {
    Setattr(cpp_to_js_class, nstr, class_jsname);
    if (Len(nstr) > 8 && strncmp(Char(nstr), "casadi::", 8) == 0) {
      String *bare = NewString(Char(nstr) + 8);
      Setattr(cpp_to_js_class, bare, class_jsname);
      Delete(bare);
    }
  }
  if (nstr) Delete(nstr);

  class_cpp_section = NewString("");
  class_js_body     = NewString("");
  ctor_arities_seen = NewHash();
  ctor_overloads    = NewList();
  ctor_overload_count = 0;
  member_names_seen = NewHash();
  member_overload_counts = NewHash();
  class_has_base    = (Getattr(n, "bases") && Len(Getattr(n, "bases")) > 0);

  /* TS stubs: route emission to a per-class body buffer for the
     duration of children visit, then wrap with `export class X { ... }`
     after.  Saves the outer (module-level) buffer for restore. */
  String *saved_stubs = 0;
  if (stubs) {
    saved_stubs = f_stubs;
    f_stubs_class_body = NewString("");
    f_stubs = f_stubs_class_body;
  }

  Language::classHandler(n);

  if (stubs) {
    /* Wrap collected member stubs with `export class Name { ... }`. */
    String *base_clause_ts = NewString("");
    List *bases = Getattr(n, "bases");
    if (bases && Len(bases) > 0) {
      Node *b = Getitem(bases, 0);
      String *bjsname = Getattr(b, "sym:name");
      if (bjsname) Printf(base_clause_ts, " extends %s", bjsname);
    }
    Printv(saved_stubs, "export class ", class_jsname, base_clause_ts, " {\n",
                       f_stubs_class_body,
                       "}\n\n",
                       NIL);
    Delete(base_clause_ts);
    Delete(f_stubs_class_body); f_stubs_class_body = 0;
    f_stubs = saved_stubs;
  }

  /* Emit a single JS constructor that dispatches on (args.length, then
     args[N]?.constructor?.name for the first non-string arg) to the
     right wasm-export ctor.  All overloads of the same arity have a
     C export emitted; the dispatcher's per-arity branch tries each
     in declaration order until one matches the arg shape. */
  if (Len(ctor_overloads) > 0) {
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
    /* Group overloads by arity, emit one `case N:` per arity with
       sequential type-matching tries. */
    Hash *arity_groups = NewHash();
    for (int i = 0; i < Len(ctor_overloads); ++i) {
      Hash *e = (Hash *)Getitem(ctor_overloads, i);
      String *ar = (String *)Getattr(e, "arity");
      List *grp = (List *)Getattr(arity_groups, ar);
      if (!grp) { grp = NewList(); Setattr(arity_groups, ar, grp); }
      Append(grp, e);
    }
    Iterator ait = First(arity_groups);
    while (ait.key) {
      String *arity_key = ait.key;
      List *grp = (List *)ait.item;
      Printf(ctor_js, "        case %s: {\n", arity_key);
      /* Emit class-dispatched cases first (more specific), then a
         single unconditional fallback (if any) last.  Without this
         ordering the fallback's `break;` short-circuits later
         class-dispatch checks. */
      Hash *fallback = 0;
      for (int i = 0; i < Len(grp); ++i) {
        Hash *e = (Hash *)Getitem(grp, i);
        String *idxs = (String *)Getattr(e, "dispatch_idxs");
        String *clss = (String *)Getattr(e, "dispatch_clss");
        String *sw  = (String *)Getattr(e, "swig_name");
        String *call_args = (String *)Getattr(e, "call_args_js");
        if (idxs && Len(idxs) > 0) {
          /* AND together class checks across ALL class-typed parm
             positions (matches matlab's overload-dispatch pattern in
             casadiMATLAB_wrap.cxx: every parm must be convertible to
             the expected type, else move on to the next overload).
             First overload whose every position validates wins. */
          String *cond = NewString("");
          List *idx_list = Split(idxs, ' ', -1);
          List *cls_list = Split(clss, ' ', -1);
          int n_pos = Len(idx_list);
          for (int k = 0; k < n_pos; ++k) {
            String *idx = (String *)Getitem(idx_list, k);
            String *cls = (String *)Getitem(cls_list, k);
            if (k > 0) Printv(cond, " && ", NIL);
            bool is_vec = false;
            String *elem_class = 0;
            if (cpp_to_js_vector_class) {
              Iterator vit = First(cpp_to_js_vector_class);
              while (vit.key) {
                if (Strcmp((String *)vit.item, cls) == 0) { is_vec = true; break; }
                vit = Next(vit);
              }
            }
            if (is_vec) {
              int cl = Len(cls);
              if (cl > 6 && strcmp(Char(cls) + cl - 6, "Vector") == 0) {
                elem_class = NewStringWithSize(Char(cls), cl - 6);
              }
            }
            if (is_vec && elem_class) {
              Printf(cond, "__can_vec(args[%s], '%s', '%s')", idx, elem_class, cls);
              Delete(elem_class);
            } else {
              Printf(cond, "__can(args[%s], '%s')", idx, cls);
            }
          }
          Printf(ctor_js,
            "          if (%s) { __ptr = M._%s(%s); break; }\n",
            cond, sw, call_args ? Char(call_args) : "");
          Delete(cond); Delete(idx_list); Delete(cls_list);
        } else if (!fallback) {
          fallback = e;
        }
      }
      if (fallback) {
        String *sw = (String *)Getattr(fallback, "swig_name");
        String *call_args = (String *)Getattr(fallback, "call_args_js");
        Printf(ctor_js,
          "          __ptr = M._%s(%s); break;\n",
          sw, call_args ? Char(call_args) : "");
      } else {
        Printf(ctor_js,
          "          throw new Error(`%s: no ctor matches args at length %s`);\n",
          class_jsname, arity_key);
      }
      Printf(ctor_js, "        }\n");
      ait = Next(ait);
    }
    Delete(arity_groups);
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
  // For multi-inheritance the additional bases are merged in via
  // __mixin() (emitted just below the class) so users see a unified
  // interface without having to know about template-parent helpers.
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

  // Multi-inheritance shim: pull non-first bases' static + prototype
  // methods into this class.  Effect for casadi: `SX.sym()` works
  // because GenericMatrix<Matrix<SXElem>> (= GenSX) is one of SX's C++
  // bases and `static sym` lives there.  Skips bases[0] (already linked
  // via `extends`) and any base whose JS name we can't resolve.
  if (bases && Len(bases) > 1) {
    String *mixin_args = NewString("");
    bool any = false;
    for (int i = 1; i < Len(bases); ++i) {
      Node *b = Getitem(bases, i);
      String *bjsname = Getattr(b, "sym:name");
      if (!bjsname || Len(bjsname) == 0) continue;
      Printf(mixin_args, ", %s", bjsname);
      any = true;
    }
    if (any) {
      Printf(f_js_classes, "  __mixin(%s%s);\n", class_jsname, mixin_args);
    }
    Delete(mixin_args);
  }

  Delete(cmangle);
  Delete(base_clause);

  /* Export the class as a Proxy so users get Python-style factory call
     syntax: `M.MX(2)` is equivalent to `new M.MX(2)`.  The Proxy's
     `apply` trap forwards to `Reflect.construct(target, args)`; the
     default `construct` trap leaves `new M.MX(...)` working unchanged.
     instanceof / static-method access pass through transparently. */
  Printf(f_js_module,
    "    %s: new Proxy(%s, { apply(t, _self, a) { return Reflect.construct(t, a); } }),\n",
    class_jsname, class_jsname);

  Delete(class_cpp_section); class_cpp_section = 0;
  Delete(class_js_body);     class_js_body     = 0;
  Delete(ctor_arities_seen); ctor_arities_seen = 0;
  if (ctor_overloads) { Delete(ctor_overloads); ctor_overloads = 0; }
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

  /* Skip the implicit copy-ctor (auto-generated by SWIG); recognise it
     by `arity == 1 && parm type mentions our own class name`.  Don't
     skip merely because the parm has a `::` namespace -- that's the
     typical case for legit ctors taking foreign classes (e.g.
     MX(const DM&)). */
  bool skip = false;
  if (arity == 1) {
    SwigType *qt = Getattr(p, "type");
    String *ts = SwigType_str(qt, 0);
    if (Strstr(ts, class_cname)) skip = true;
    Delete(ts);
  }

  if (!skip) {
    String *cmangle = mangle(class_cname);
    String *swig_name = NewStringf("swig_%s_new_%d", cmangle, ctor_overload_count++);
    name_parms(p);
    String *decls = parm_decls(p);
    String *args  = parm_args(p);
    String *prologue = parm_prologue(p);
    String *frees    = parm_freeargs(p);

    Printf(class_cpp_section,
      "EMSCRIPTEN_KEEPALIVE %s* %s(%s) {\n%s  %s* _outv = new %s(%s);\n%s  return _outv;\n}\n",
      class_cname, swig_name, decls,
      prologue,
      class_cname, class_cname, args,
      frees);
    register_export(Char(swig_name));

    /* Track ALL class-typed parm positions for dispatch (not just the
       first).  Each position contributes an OR-clause to the dispatcher
       so empty-array args at one position fall back to a non-empty
       array at another position.  E.g. for
       `Function(string, vec<MX>, vec<MX>, Dict)`, passing
       `(name, [], [r], null)` dispatches via args[2] when args[1] is
       empty.  Stored as parallel space-separated lists of indices and
       JS class names. */
    Hash *entry = NewHash();
    char arity_str[16]; snprintf(arity_str, sizeof(arity_str), "%d", arity);
    Setattr(entry, "arity", NewString(arity_str));
    Setattr(entry, "swig_name", Copy(swig_name));
    int parm_idx = 0;
    String *dispatch_idxs = NewString("");
    String *dispatch_clss = NewString("");
    for (Parm *q = p; q; q = nextSibling(q), ++parm_idx) {
      if (is_in_numinputs0(q)) continue;
      SwigType *t = Getattr(q, "type");
      if (t && is_registered_class_type(t)) {
        SwigType *resolved = SwigType_typedef_resolve_all(t);
        String *sn = SwigType_str(resolved ? resolved : t, 0);
        const char *cs = Char(sn);
        int cn = Len(sn);
        if (cn > 6 && strncmp(cs, "const ", 6) == 0) { cs += 6; cn -= 6; }
        for (;;) {
          while (cn > 0 && (cs[cn-1] == '&' || cs[cn-1] == '*' ||
                            cs[cn-1] == ' ' || cs[cn-1] == '\t')) cn--;
          if (cn > 6 && strncmp(cs + cn - 6, " const", 6) == 0) { cn -= 6; continue; }
          break;
        }
        String *bare = NewStringWithSize(cs, cn);
        String *hit = (String *)Getattr(cpp_to_js_class, bare);
        if (!hit && cn > 8 && strncmp(cs, "casadi::", 8) == 0) {
          String *stripped = NewStringWithSize(cs + 8, cn - 8);
          hit = (String *)Getattr(cpp_to_js_class, stripped);
          Delete(stripped);
        }
        if (hit && Len(hit) > 0) {
          if (Len(dispatch_idxs) > 0) { Printv(dispatch_idxs, " ", NIL); Printv(dispatch_clss, " ", NIL); }
          Printf(dispatch_idxs, "%d", parm_idx);
          Printv(dispatch_clss, hit, NIL);
        }
        Delete(bare); Delete(sn);
        if (resolved) Delete(resolved);
      }
    }
    if (Len(dispatch_idxs) > 0) {
      Setattr(entry, "dispatch_idxs", dispatch_idxs);
      Setattr(entry, "dispatch_clss", dispatch_clss);
    } else {
      Delete(dispatch_idxs); Delete(dispatch_clss);
    }
    /* Build per-arg conversion list so the ctor dispatcher can emit
       proper wasm-call arguments instead of blind `__unwrap_args(...)`.
       For each input parm: vector-typed -> `__unwrap(__arr_to_vec(args[i], V))`,
       class -> `__unwrap(args[i])`, primitive -> `args[i]`. */
    {
      String *call_args_js = NewString("");
      int pi = 0;
      for (Parm *q = p; q; q = nextSibling(q), ++pi) {
        if (is_in_numinputs0(q)) continue;
        if (Len(call_args_js) > 0) Printv(call_args_js, ", ", NIL);
        SwigType *t = Getattr(q, "type");
        if (String *vec_cls = vector_class_name(t)) {
          Printf(call_args_js, "__unwrap(__arr_to_vec(args[%d], %s))", pi, Char(vec_cls));
        } else if (is_registered_class_type(t)) {
          Printf(call_args_js, "__unwrap(args[%d])", pi);
        } else {
          Printf(call_args_js, "args[%d]", pi);
        }
      }
      Setattr(entry, "call_args_js", call_args_js);
      Delete(call_args_js);
    }
    if (!ctor_overloads) ctor_overloads = NewList();
    Append(ctor_overloads, entry);

    Delete(swig_name); Delete(decls); Delete(args); Delete(cmangle);
    Delete(prologue); Delete(frees);
  }
  /* TS stub: one `constructor(...)` line per overload (TS supports
     overloaded constructor signatures natively in .d.ts). */
  stub_emit_function(n, "  ", 3);
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
  name_parms(p);
  String *decls = parm_decls(p);
  String *args  = parm_args(p);
  String *jsargs = js_arg_names(p);
  String *ret_t  = effective_return_ctype(rt, p);
  String *call_e = NewStringf("self->%s(%s)", mname, args);
  String *body   = cpp_call_body(p, rt, call_e);

  Printf(class_cpp_section,
    "EMSCRIPTEN_KEEPALIVE %s %s(%s%s* self%s%s) {\n%s}\n",
    ret_t, swig_name, self_q, class_cname,
    Len(decls) > 0 ? ", " : "", decls,
    body);
  register_export(Char(swig_name));

  SwigType *eff_rt = effective_js_return_type(rt, p);
  /* Use emit_js_body so jsin/jsarg/jsfree typemaps fire -- needed for
     Array <-> XVector autoconversion (the typemaps emitted by
     %wasm_vec in casadi.i).  self_prefix = "this._ptr" makes the
     wasm call site `M._<swig>(this._ptr, ...)`. */
  if (!Getattr(member_names_seen, jsraw)) {
    Setattr(member_names_seen, jsraw, "1");
    String *js_body_str = emit_js_body(p, eff_rt ? eff_rt : rt, swig_name, "this._ptr");
    Printf(class_js_body,
      "    %s(%s) {\n%s    }\n",
      jsraw, jsargs, js_body_str);
    Delete(js_body_str);
    stub_emit_function(n, "  ", 1);
  }

  Delete(swig_name); Delete(cmangle); Delete(self_q); Delete(jsraw);
  Delete(decls); Delete(args); Delete(jsargs);
  Delete(ret_t); Delete(call_e); Delete(body);
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

  stub_emit_variable(n, "  ");

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

  name_parms(p);
  String *decls = parm_decls(p);
  String *args  = parm_args(p);
  String *jsargs = js_arg_names(p);
  String *ret_t  = effective_return_ctype(rt, p);
  String *call_e = NewStringf("%s::%s(%s)", class_cname, mname, args);
  String *body   = cpp_call_body(p, rt, call_e);

  Printf(class_cpp_section,
    "EMSCRIPTEN_KEEPALIVE %s %s(%s) {\n%s}\n",
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
    stub_emit_function(n, "  ", 2);  /* TS: `static name(...): T;` */
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
  // C export symbol must be a valid C identifier. For free functions in
  // a namespace (e.g. "casadi::complement"), mangle :: -> _.
  String *fmangle = mangle(fname);
  String *swig_name = NewStringf("swig_%s_%d", fmangle, idx);

  name_parms(p);
  String *decls = parm_decls(p);
  String *args  = parm_args(p);
  String *jsargs = js_arg_names(p);
  String *ret_t  = effective_return_ctype(rt, p);
  String *call_e = NewStringf("%s(%s)", fname, args);
  String *body   = cpp_call_body(p, rt, call_e);

  Printf(f_cpp_wrappers,
    "EMSCRIPTEN_KEEPALIVE %s %s(%s) {\n%s}\n",
    ret_t, swig_name, decls, body);
  register_export(Char(swig_name));

  /* Record this overload into global_overloads so end-of-top() can
     emit a single dispatcher per jsname.  Dispatcher picks via the
     first arg's JS class name (e.g. `args[0].constructor.name ===
     'MXVector'`); arity is a coarser tiebreaker.  Stub emission still
     happens per-overload because TS .d.ts uses overload chains. */
  if (!global_overloads) global_overloads = NewHash();
  List *overloads = (List *)Getattr(global_overloads, jsname);
  if (!overloads) {
    overloads = NewList();
    Setattr(global_overloads, jsname, overloads);
  }
  String *js_body = emit_js_body(p, rt, swig_name, "");
  /* Resolve arg0's expected JS class name for the dispatcher's
     constructor.name check.  Empty if it's not a registered class
     (primitive arg -- the overload accepts anything that coerces). */
  String *arg0_class = NewString("");
  Parm *first = p;
  while (first && is_in_numinputs0(first)) first = nextSibling(first);
  if (first) {
    SwigType *t = Getattr(first, "type");
    if (t && cpp_to_js_class) {
      String *cn = SwigType_str(t, 0);
      const char *cs = Char(cn);
      int cnn = Len(cn);
      while (cnn > 0 && (cs[0] == ' ' || cs[0] == '\t')) { cs++; cnn--; }
      while (cnn > 0 && (cs[cnn-1] == ' ' || cs[cnn-1] == '\t')) cnn--;
      if (cnn > 6 && strncmp(cs, "const ", 6) == 0) { cs += 6; cnn -= 6; }
      for (;;) {
        while (cnn > 0 && (cs[cnn-1] == '&' || cs[cnn-1] == '*' ||
                           cs[cnn-1] == ' ' || cs[cnn-1] == '\t')) cnn--;
        if (cnn > 6 && strncmp(cs + cnn - 6, " const", 6) == 0) { cnn -= 6; continue; }
        break;
      }
      String *bare = NewStringWithSize(cs, cnn);
      String *hit = (String *)Getattr(cpp_to_js_class, bare);
      if (!hit && cnn > 8 && strncmp(cs, "casadi::", 8) == 0) {
        String *stripped = NewStringWithSize(cs + 8, cnn - 8);
        hit = (String *)Getattr(cpp_to_js_class, stripped);
        Delete(stripped);
      }
      if (hit) Append(arg0_class, hit);
      Delete(cn); Delete(bare);
    }
  }
  Hash *entry = NewHash();
  Setattr(entry, "body", js_body);
  Setattr(entry, "arg0_class", arg0_class);
  char arity_buf[16]; snprintf(arity_buf, sizeof(arity_buf), "%d", parm_arity(p));
  Setattr(entry, "arity", NewString(arity_buf));
  Setattr(entry, "jsargs", Copy(jsargs));
  Append(overloads, entry);

  /* TS stub emission stays per-overload (TS supports overload chains
     in .d.ts directly, unlike runtime JS). */
  stub_emit_function(n, "", 0);

  Delete(js_body); Delete(arg0_class);
  Delete(fmangle);
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

static Language *new_swig_wasm_js() { return new WASM_JS(); }
extern "C" Language *swig_wasm_js(void) { return new_swig_wasm_js(); }
