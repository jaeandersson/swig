/* -----------------------------------------------------------------------------
 * This file is part of SWIG, which is licensed as a whole under version 3
 * (or any later version) of the GNU General Public License. Some additional
 * terms also apply to certain portions of SWIG. The full details of the SWIG
 * license and copyrights can be found in the LICENSE and COPYRIGHT files
 * included with the SWIG source code as distributed by the SWIG developers
 * and at https://www.swig.org/legal.html.
 *
 * rust.cxx
 *
 * Rust language module for SWIG.  Values cross the C ABI as guest values
 * (SwigRV*, see Lib/rust/rustrun.swg), the C++ side dispatches overloads as
 * in the MATLAB module, and a .rs file with FFI declarations, raw calls and
 * class proxies is generated next to the C++ wrapper.
 * ----------------------------------------------------------------------------- */

#include "swigmod.h"
#include "cparse.h"
#include <string>
#include <vector>
#include <map>
#include <set>

static const char *usage = (char *) "\
Rust Options (available with -rust)\n\
     (none yet)\n\
\n";

/* Rust keywords; identifiers that collide are emitted as raw identifiers */
static const char *rust_keywords[] = {
  "as", "break", "const", "continue", "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let",
  "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "static", "struct", "trait", "true", "type",
  "unsafe", "use", "where", "while", "async", "await", "dyn", "abstract", "become", "box", "do", "final",
  "macro", "override", "priv", "typeof", "unsized", "virtual", "yield", "try", "gen", 0
};

/* Keywords that cannot be raw identifiers */
static const char *rust_reserved[] = { "self", "Self", "super", "crate", "_", 0 };

class RUST : public Language {
public:
  RUST();
  virtual void main(int argc, char *argv[]);
  virtual int top(Node *n);
  virtual int functionWrapper(Node *n);
  virtual int variableWrapper(Node *n);
  virtual int constantWrapper(Node *n);
  virtual int enumDeclaration(Node *n);
  virtual int enumvalueDeclaration(Node *n);
  virtual int classHandler(Node *n);
  virtual int destructorHandler(Node *n);
  virtual int insertDirective(Node *n);
  virtual int globalfunctionHandler(Node *n);
  virtual int memberfunctionHandler(Node *n);
  virtual int staticmemberfunctionHandler(Node *n);
  virtual int constructorHandler(Node *n);
  virtual int membervariableHandler(Node *n);

private:
  File *f_begin;
  String *f_runtime;
  String *f_header;
  String *f_wrappers;
  String *f_init;
  String *f_initbeforefunc;
  String *f_directors;
  String *f_directors_h;
  String *f_rs_runtime;
  String *f_rs_ffi;
  String *f_rs_raw;
  String *f_rs_classes;
  String *f_rs_code;
  String *f_rs_kinds;
  String *f_rs_acc;
  String *f_rs_sigs;
  String *f_rs_globals;
  String *module_name;
  String *class_name;
  Node *class_node;
  bool class_has_destructor;

  /* What the functions being wrapped are: set by the handlers */
  enum CallKind { CK_GLOBAL, CK_MEMBER, CK_STATIC, CK_CTOR, CK_VAR };
  CallKind cur_kind;
  String *cur_name;

  /* Typed methods per class, in order: (Rust name, code, inheritable) */
  struct Snippet {
    std::string name, code;
    bool inherit;
  };
  std::map<Node *, std::vector<Snippet> > class_snippets;
  /* Trait impls of a class: Clone from the copy constructor, Default from the default one */
  std::map<Node *, std::string> class_traits;
  std::map<std::string, std::string> acc_traits;
  int sig_count;
  std::set<std::string> global_names;
  /* Enum values known at generation time; enum_next < 0 once an initializer is not a literal */
  std::map<std::string, long long> enum_values;
  long long enum_next;
  std::set<std::string> joins;
  std::string fail_type;

  /* One overload as seen from Rust */
  struct Overload {
    Node *n;
    int req, nin;
    std::vector<std::vector<std::string> > acc;
    std::vector<std::string> outs;
  };

  void dispatchFunction(Node *n);
  std::string emitGroup(std::vector<Overload> &ovs, const std::string &base, const std::string &ename, int num_returns, bool member);
  void addEntry(String *iname, String *wname, int num_returns, Node *n);
  String *rustIdent(const_String_or_char_ptr name);
  std::string ident(const std::string &name);
  SwigType *bareType(SwigType *t);
  String *lookupTypemap(const char *method, SwigType *t);
  std::string rsOut(SwigType *t);
  std::vector<std::string> rsAccept(SwigType *t);
  std::string accTrait(const std::vector<std::string> &acc);
  bool overloadInfo(Node *n, Overload &o);
  void emitTyped(Node *n, const std::string &ename, int num_returns);
  std::string emitForm(const std::string &rname, int k, const std::vector<Overload *> &cands, const std::string &ename, int num_returns, bool member);

  const char *get_implicitconv_flag(Node *klass) {
    int conv = 0;
    if (klass && GetFlag(klass, "feature:implicitconv")) {
      conv = 1;
    }
    return conv ? "SWIG_POINTER_IMPLICIT_CONV" : "0";
  }
};

RUST::RUST() :
  f_begin(0), f_runtime(0), f_header(0), f_wrappers(0), f_init(0), f_initbeforefunc(0),
  f_directors(0), f_directors_h(0), f_rs_runtime(0), f_rs_ffi(0), f_rs_raw(0), f_rs_classes(0), f_rs_code(0),
  f_rs_kinds(0), f_rs_acc(0), f_rs_sigs(0), f_rs_globals(0),
  module_name(0), class_name(0), class_node(0), class_has_destructor(false),
  cur_kind(CK_GLOBAL), cur_name(0), sig_count(0) {
}

void RUST::main(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] && strcmp(argv[i], "-help") == 0) {
      fputs(usage, stdout);
    }
  }
  SWIG_library_directory("rust");
  Preprocessor_define("SWIGRUST 1", 0);
  SWIG_config_file("rust.swg");
  allow_overloading();
}

String *RUST::rustIdent(const_String_or_char_ptr name) {
  for (const char **k = rust_reserved; *k; ++k)
    if (Strcmp(name, *k) == 0)
      return NewStringf("%s_", name);
  for (const char **k = rust_keywords; *k; ++k)
    if (Strcmp(name, *k) == 0)
      return NewStringf("r#%s", name);
  return NewString(name);
}

int RUST::top(Node *n) {
  module_name = Copy(Getattr(n, "name"));
  String *outfile = Getattr(n, "outfile");
  f_begin = NewFile(outfile, "w", SWIG_output_files());
  if (!f_begin) {
    FileErrorDisplay(outfile);
    Exit(EXIT_FAILURE);
  }
  f_runtime = NewString("");
  f_header = NewString("");
  f_wrappers = NewString("");
  f_init = NewString("");
  f_initbeforefunc = NewString("");
  f_directors = NewString("");
  f_directors_h = NewString("");
  f_rs_runtime = NewString("");
  f_rs_ffi = NewString("");
  f_rs_raw = NewString("");
  f_rs_classes = NewString("");
  f_rs_code = NewString("");
  f_rs_kinds = NewString("");
  f_rs_acc = NewString("");
  f_rs_sigs = NewString("");
  f_rs_globals = NewString("");

  Swig_register_filebyname("begin", f_begin);
  Swig_register_filebyname("runtime", f_runtime);
  Swig_register_filebyname("header", f_header);
  Swig_register_filebyname("wrapper", f_wrappers);
  Swig_register_filebyname("init", f_init);
  Swig_register_filebyname("initbeforefunc", f_initbeforefunc);
  Swig_register_filebyname("director", f_directors);
  Swig_register_filebyname("director_h", f_directors_h);
  Swig_register_filebyname("rustruntime", f_rs_runtime);
  Swig_register_filebyname("rust", f_rs_code);

  Swig_banner(f_begin);
  Printf(f_runtime, "\n\n#ifndef SWIGRUST\n#define SWIGRUST\n#endif\n\n");

  Language::top(n);

  SwigType_emit_type_table(f_runtime, f_wrappers);

  Dump(f_runtime, f_begin);
  Dump(f_header, f_begin);
  Dump(f_directors_h, f_begin);
  Dump(f_directors, f_begin);
  Dump(f_wrappers, f_begin);
  Dump(f_initbeforefunc, f_begin);
  Printf(f_begin, "extern \"C\" SWIGEXPORT void swig_rust_init() {\n");
  Printf(f_begin, "  static int loaded = 0;\n");
  Printf(f_begin, "  if (loaded) return;\n");
  Printf(f_begin, "  loaded = 1;\n");
  Printf(f_begin, "  SWIG_InitializeModule(0);\n");
  Dump(f_init, f_begin);
  Printf(f_begin, "}\n");

  /* Rust side */
  String *rsfile = NewStringf("%s%s.rs", SWIG_output_directory(), module_name);
  File *f_rs = NewFile(rsfile, "w", SWIG_output_files());
  if (!f_rs) {
    FileErrorDisplay(rsfile);
    Exit(EXIT_FAILURE);
  }
  Printf(f_rs, "// This file was automatically generated by SWIG (https://www.swig.org).\n");
  Printf(f_rs, "// Do not make changes to this file unless you know what you are doing.\n\n");
  Printf(f_rs, "%s\n", f_rs_runtime);
  Printf(f_rs, "#[allow(unused_imports)]\n");
  Printf(f_rs, "use std::collections::HashMap;\n\n");
  Printf(f_rs, "#[allow(dead_code, non_snake_case)]\n");
  Printf(f_rs, "pub mod ffi {\n");
  Printf(f_rs, "    use super::rt::SwigRV;\n");
  Printf(f_rs, "    use std::os::raw::c_int;\n");
  Printf(f_rs, "    extern \"C\" {\n");
  Printf(f_rs, "        pub fn swig_rust_init();\n");
  Printf(f_rs, "%s", f_rs_ffi);
  Printf(f_rs, "    }\n");
  Printf(f_rs, "}\n\n");
  Printf(f_rs, "#[allow(dead_code, non_snake_case)]\n");
  Printf(f_rs, "pub mod raw {\n");
  Printf(f_rs, "    use super::{ffi, rt};\n");
  Printf(f_rs, "%s", f_rs_raw);
  Printf(f_rs, "}\n\n");
  Printf(f_rs, "/// Kinds of argument values, used to resolve overloads at compile time\n");
  Printf(f_rs, "pub mod kind {\n");
  Printf(f_rs, "    pub use super::rt::kind::*;\n");
  Printf(f_rs, "%s", f_rs_kinds);
  Printf(f_rs, "}\n\n");
  Printf(f_rs, "/// Kinds accepted by an argument\n");
  Printf(f_rs, "#[allow(non_camel_case_types)]\n");
  Printf(f_rs, "pub mod acc {\n");
  Printf(f_rs, "    use super::kind;\n");
  Printf(f_rs, "%s", f_rs_acc);
  Printf(f_rs, "}\n\n");
  Printf(f_rs, "/// Results of overloaded calls, by argument kinds\n");
  Printf(f_rs, "#[allow(non_camel_case_types, unused_imports)]\n");
  Printf(f_rs, "pub mod sigs {\n");
  Printf(f_rs, "    use super::*;\n");
  Printf(f_rs, "%s", f_rs_sigs);
  Printf(f_rs, "}\n\n");
  Printf(f_rs, "%s", f_rs_classes);
  Printf(f_rs, "%s", f_rs_globals);
  Printf(f_rs, "%s", f_rs_code);
  Delete(f_rs);
  Delete(rsfile);

  Delete(f_runtime);
  Delete(f_header);
  Delete(f_wrappers);
  Delete(f_init);
  Delete(f_initbeforefunc);
  Delete(f_directors);
  Delete(f_directors_h);
  Delete(f_begin);
  return SWIG_OK;
}

int RUST::insertDirective(Node *n) {
  return Language::insertDirective(n);
}

/* ------------------------------------------------------------
 * addEntry(): exported C trampoline, Rust FFI declaration and raw call
 * ------------------------------------------------------------ */

void RUST::addEntry(String *iname, String *wname, int num_returns, Node *n) {
  String *ename = NewStringf("swigrs_%s", iname);
  Printf(f_wrappers, "extern \"C\" SWIGEXPORT int %s(int resc, SwigRV *resv[], int argc, SwigRV *argv[]) {\n", ename);
  Printf(f_wrappers, "  try {\n");
  Printf(f_wrappers, "    return %s(resc, resv, argc, argv);\n", wname);
  Printf(f_wrappers, "  } catch (const std::exception& e) {\n");
  Printf(f_wrappers, "    SWIG_Error(SWIG_RuntimeError, e.what());\n");
  Printf(f_wrappers, "  } catch (...) {\n");
  Printf(f_wrappers, "    SWIG_Error(SWIG_UnknownError, \"unknown C++ exception\");\n");
  Printf(f_wrappers, "  }\n");
  Printf(f_wrappers, "  return 1;\n");
  Printf(f_wrappers, "}\n\n");

  Printf(f_rs_ffi, "        pub fn %s(resc: c_int, resv: *mut *mut SwigRV, argc: c_int, argv: *mut *mut SwigRV) -> c_int;\n", ename);

  String *rname = rustIdent(iname);
  Printf(f_rs_raw, "    pub fn %s(args: Vec<rt::RV>) -> Result<Vec<rt::RV>, rt::Error> {\n", rname);
  Printf(f_rs_raw, "        rt::call(ffi::%s, args, %d)\n", ename, num_returns);
  Printf(f_rs_raw, "    }\n");
  if (n)
    emitTyped(n, Char(ename), num_returns);
  Delete(rname);
  Delete(ename);
}

/* ------------------------------------------------------------
 * functionWrapper(): as in the MATLAB module, with SwigRV* values
 * ------------------------------------------------------------ */

int RUST::functionWrapper(Node *n) {
  Parm *p;
  String *tm;
  int j;

  String *nodeType = Getattr(n, "nodeType");
  int constructor = (!Cmp(nodeType, "constructor"));
  int destructor = (!Cmp(nodeType, "destructor"));
  String *storage = Getattr(n, "storage");

  bool overloaded = !!Getattr(n, "sym:overloaded");
  bool last_overload = overloaded && !Getattr(n, "sym:nextSibling");
  String *iname = Getattr(n, "sym:name");
  String *wname = Swig_name_wrapper(iname);
  String *overname = Copy(wname);
  SwigType *d = Getattr(n, "type");
  ParmList *l = Getattr(n, "parms");

  if (!overloaded && !addSymbol(iname, n))
    return SWIG_ERROR;

  if (overloaded)
    Append(overname, Getattr(n, "sym:overname"));

  Wrapper *f = NewWrapper();
  Printf(f->def, "SWIGINTERN int %s(int resc, SwigRV *resv[], int argc, SwigRV *argv[]) {", overname);

  emit_parameter_variables(l, f);
  emit_attach_parmmaps(l, f);
  Setattr(n, "wrap:parms", l);

  int num_arguments = emit_num_arguments(l);
  int num_required = emit_num_required(l);
  int varargs = emit_isvarargs(l);
  char source[64];

  if (destructor) {
    Append(f->code, "int is_owned = 0;\n");
  }

  Printf(f->code, "if (!SWIG_check_num_args(\"%s\",argc,%i,%i,%i)) " "{\n SWIG_fail;\n }\n", iname, num_arguments, num_required, varargs);
  if (num_arguments == 0 && num_required == 0) {
    Printf(f->code, "(void)argv;\n");
  } else if (constructor && num_arguments == 1 && num_required == 1) {
    if (Cmp(storage, "explicit") == 0) {
      Node *parent = Swig_methodclass(n);
      if (GetFlag(parent, "feature:implicitconv")) {
	String *desc = NewStringf("SWIGTYPE%s", SwigType_manglestr(Getattr(n, "type")));
	Printf(f->code, "if (SWIG_CheckImplicit(%s)) SWIG_fail;\n", desc);
	Delete(desc);
      }
    }
  }
  // Check if owned here, before possibly disowning in the typemaps
  if (destructor) {
    Append(f->code, "is_owned = argv[0] && argv[0]->kind == SWIG_RV_PTR && argv[0]->own;\n");
  }

  for (j = 0, p = l; j < num_arguments; ++j) {
    while (checkAttribute(p, "tmap:in:numinputs", "0")) {
      p = Getattr(p, "tmap:in:next");
    }

    SwigType *pt = Getattr(p, "type");

    String *tm = Getattr(p, "tmap:in");
    if (tm) {
      sprintf(source, "argv[%d]", j);
      Setattr(p, "emit:input", source);

      Replaceall(tm, "$source", Getattr(p, "emit:input"));
      Replaceall(tm, "$input", Getattr(p, "emit:input"));
      Replaceall(tm, "$target", Getattr(p, "lname"));

      if (Getattr(p, "wrap:disown") || (Getattr(p, "tmap:in:disown"))) {
	Replaceall(tm, "$disown", "SWIG_POINTER_DISOWN");
      } else {
	Replaceall(tm, "$disown", "0");
      }

      if (Getattr(p, "tmap:in:implicitconv")) {
	const char *convflag = "0";
	if (!Getattr(p, "hidden")) {
	  SwigType *ptype = Getattr(p, "type");
	  convflag = get_implicitconv_flag(classLookup(ptype));
	}
	Replaceall(tm, "$implicitconv", convflag);
	Setattr(p, "implicitconv", convflag);
      }

      String *getargs = NewString("");
      if (j >= num_required)
	Printf(getargs, "if (%d<argc) {\n%s\n}", j, tm);
      else
	Printv(getargs, tm, NIL);
      Printv(f->code, getargs, "\n", NIL);
      Delete(getargs);

      p = Getattr(p, "tmap:in:next");
      continue;
    } else {
      Swig_warning(WARN_TYPEMAP_IN_UNDEF, input_file, line_number, "Unable to use type %s as a function argument.\n", SwigType_str(pt, 0));
      break;
    }
  }

  // Check for trailing varargs
  if (varargs) {
    if (p && (tm = Getattr(p, "tmap:in"))) {
      Replaceall(tm, "$input", "varargs");
      Printv(f->code, tm, "\n", NIL);
    }
  }

  // Insert constraint checking code
  for (p = l; p;) {
    if ((tm = Getattr(p, "tmap:check"))) {
      Replaceall(tm, "$target", Getattr(p, "lname"));
      Printv(f->code, tm, "\n", NIL);
      p = Getattr(p, "tmap:check:next");
    } else {
      p = nextSibling(p);
    }
  }

  // Insert cleanup code
  String *cleanup = NewString("");
  for (p = l; p;) {
    if ((tm = Getattr(p, "tmap:freearg"))) {
      if (Getattr(p, "tmap:freearg:implicitconv")) {
	const char *convflag = "0";
	if (!Getattr(p, "hidden")) {
	  SwigType *ptype = Getattr(p, "type");
	  convflag = get_implicitconv_flag(classLookup(ptype));
	}
	if (strcmp(convflag, "0") == 0) {
	  tm = 0;
	}
      }
      if (tm && (Len(tm) != 0)) {
	Replaceall(tm, "$source", Getattr(p, "lname"));
	Printv(cleanup, tm, "\n", NIL);
      }
      p = Getattr(p, "tmap:freearg:next");
    } else {
      p = nextSibling(p);
    }
  }

  // Total number of function outputs, including return value
  int num_returns = 1;

  // Insert argument output code
  String *outarg = NewString("");
  for (p = l; p;) {
    if ((tm = Getattr(p, "tmap:argout"))) {
      Replaceall(tm, "$source", Getattr(p, "lname"));
      Replaceall(tm, "$target", "if (--resc>=0) *resv++");
      Replaceall(tm, "$result", "if (--resc>=0) *resv++");
      Replaceall(tm, "$arg", Getattr(p, "emit:input"));
      Replaceall(tm, "$input", Getattr(p, "emit:input"));
      Printv(outarg, tm, "\n", NIL);
      p = Getattr(p, "tmap:argout:next");
      num_returns++;
    } else {
      p = nextSibling(p);
    }
  }

  Setattr(n, "wrap:name", overname);

  if (destructor) {
    Append(f->code, "if (is_owned) {\n");
  }
  String *actioncode = emit_action(n);
  if (destructor) {
    Append(actioncode, "}\n");
  }

  Wrapper_add_local(f, "_out", "SwigRV * _out");

  // Return the function value
  if ((tm = Swig_typemap_lookup_out("out", n, Swig_cresult_name(), f, actioncode))) {
    // Check if void return
    if (SwigType_issimple(d)) {
      String *typestr = SwigType_str(d, 0);
      if (Strcmp(typestr, "void") == 0)
	num_returns--;
      Delete(typestr);
    }

    Replaceall(tm, "$source", Swig_cresult_name());
    Replaceall(tm, "$target", "_out");
    Replaceall(tm, "$result", "_out");

    if (GetFlag(n, "feature:new"))
      Replaceall(tm, "$owner", "SWIG_POINTER_OWN");
    else
      Replaceall(tm, "$owner", "0");

    Printf(f->code, "%s\n", tm);
    // First output is stored even if resc==0
    Printf(f->code, "if (_out) --resc, *resv++ = _out;\n");
    Delete(tm);
  } else {
    Swig_warning(WARN_TYPEMAP_OUT_UNDEF, input_file, line_number, "Unable to use return type %s in function %s.\n", SwigType_str(d, 0), iname);
  }
  emit_return_variable(n, d, f);

  Printv(f->code, outarg, NIL);
  Printv(f->code, cleanup, NIL);

  if (GetFlag(n, "feature:new")) {
    if ((tm = Swig_typemap_lookup("newfree", n, Swig_cresult_name(), 0))) {
      Replaceall(tm, "$source", Swig_cresult_name());
      Printf(f->code, "%s\n", tm);
    }
  }

  if ((tm = Swig_typemap_lookup("ret", n, Swig_cresult_name(), 0))) {
    Replaceall(tm, "$source", Swig_cresult_name());
    Replaceall(tm, "$result", "_outv");
    Printf(f->code, "%s\n", tm);
    Delete(tm);
  }
  Setattr(n, "rust:num_returns", NewStringf("%d", num_returns));

  Printf(f->code, "return 0;\n");
  Printf(f->code, "fail:\n");

  if (checkAttribute(n, "feature:customdoc", "1")) {
    String *protoType = NewString("");
    Printf(protoType, "    \"    ");
    Swig_prototype(n, protoType, "style_error");
    Printf(protoType, "\\n\"\n");
    Printf(f->code, "if (SWIG_lasterror_code == SWIG_TypeError) {\n");
    Printf(f->code, "std::string type_name = SWIG_Rust_ArgsTypeDesc(argc, argv);\n");
    Printf(f->code, "SWIG_Error_Format(SWIG_RuntimeError, \"No matching function for overload function '%s'.\"\n", Getattr(n, "sym:name"));
    Printf(f->code, "   \"  Prototype:\\n\"\n%s\n     \"  You have: %%s\", type_name.c_str());\n", protoType);
    Printf(f->code, "}\n");
    Delete(protoType);
  }

  Printv(f->code, cleanup, NIL);
  Printf(f->code, "return 1;\n");
  Printf(f->code, "}\n");

  /* Substitute the cleanup code */
  Replaceall(f->code, "$cleanup", cleanup);

  Replaceall(f->code, "$symname", iname);
  Wrapper_print(f, f_wrappers);
  DelWrapper(f);

  if (last_overload) {
    dispatchFunction(n);
  } else if (!overloaded) {
    addEntry(iname, wname, num_returns, n);
  }

  if (destructor)
    class_has_destructor = true;

  Delete(overname);
  Delete(wname);
  Delete(cleanup);
  Delete(outarg);

  return SWIG_OK;
}

/* ------------------------------------------------------------
 * dispatchFunction(): C++-side overload resolution
 * ------------------------------------------------------------ */

void RUST::dispatchFunction(Node *n) {
  Wrapper *f = NewWrapper();

  String *iname = Getattr(n, "sym:name");
  String *wname = Swig_name_wrapper(iname);
  int maxargs;
  bool check_emitted = false;
  String *dispatch = Swig_overload_dispatch(n, "return %s(resc,resv,argc,argv);", &maxargs, &check_emitted);

  bool customdoc = checkAttribute(n, "feature:customdoc", "1");

  String *protoTypes;
  if (customdoc) {
    protoTypes = Swig_prototypes_error(n);
  } else {
    Node *sibl = n;
    while (Getattr(sibl, "sym:previousSibling"))
      sibl = Getattr(sibl, "sym:previousSibling");
    protoTypes = NewString("");
    do {
      String *fulldecl = Swig_name_decl(sibl);
      Printf(protoTypes, "\n\"    %s\\n\"", fulldecl);
      Delete(fulldecl);
    } while ((sibl = Getattr(sibl, "sym:nextSibling")));
  }

  /* Most outputs of any overload */
  int num_returns = 0;
  Node *sibl = n;
  while (Getattr(sibl, "sym:previousSibling"))
    sibl = Getattr(sibl, "sym:previousSibling");
  do {
    String *nr = Getattr(sibl, "rust:num_returns");
    if (nr && atoi(Char(nr)) > num_returns)
      num_returns = atoi(Char(nr));
  } while ((sibl = Getattr(sibl, "sym:nextSibling")));

  Printf(f->def, "SWIGINTERN int %s(int resc, SwigRV *resv[], int argc, SwigRV *argv[]) {", wname);
  Printv(f->code, dispatch, "\n", NIL);

  if (customdoc)
    Printf(f->code, "std::string type_name = SWIG_Rust_ArgsTypeDesc(argc, argv);\n");
  Printf(f->code, "SWIG_Error_Format(SWIG_RuntimeError, \"No matching function for overload function '%s'.\"\n", iname);
  if (customdoc) {
    Printf(f->code, "   \"  Possible prototypes are:\\n\"\n%s\n     \"  You have: %%s\", type_name.c_str());\n", protoTypes);
  } else {
    Printf(f->code, "   \"  Possible C/C++ prototypes are:\\n\"%s);\n", protoTypes);
  }
  Printf(f->code, "return 1;\n");
  Printv(f->code, "}\n", NIL);

  Wrapper_print(f, f_wrappers);
  addEntry(iname, wname, num_returns, n);
  DelWrapper(f);
  Delete(dispatch);
  Delete(wname);
  Delete(protoTypes);
}

/* ------------------------------------------------------------
 * variableWrapper(): getter (and setter) functions
 * ------------------------------------------------------------ */

int RUST::variableWrapper(Node *n) {
  return Language::variableWrapper(n);
}

/* ------------------------------------------------------------
 * Enums: track values so they become Rust constants
 * ------------------------------------------------------------ */

int RUST::enumDeclaration(Node *n) {
  enum_next = 0;
  return Language::enumDeclaration(n);
}

int RUST::enumvalueDeclaration(Node *n) {
  String *ev = Getattr(n, "enumvalue");
  if (ev) {
    char *end;
    long long v = strtoll(Char(ev), &end, 0);
    std::map<std::string, long long>::iterator it = enum_values.find(Char(ev));
    if (*end == 0 && end != Char(ev)) enum_next = v;
    else if (it != enum_values.end()) enum_next = it->second;
    else enum_next = -1;
  }
  if (enum_next >= 0) {
    enum_values[Char(Getattr(n, "sym:name"))] = enum_next;
    enum_values[Char(Getattr(n, "name"))] = enum_next;
    enum_next++;
  }
  return Language::enumvalueDeclaration(n);
}

/* ------------------------------------------------------------
 * constantWrapper(): one getter function per constant
 * ------------------------------------------------------------ */

int RUST::constantWrapper(Node *n) {
  String *name = Getattr(n, "name");
  String *symname = Getattr(n, "sym:name");
  SwigType *type = Getattr(n, "type");
  String *rawval = Getattr(n, "rawval");
  String *value = rawval ? rawval : Getattr(n, "value");
  String *cppvalue = Getattr(n, "cppvalue");
  String *tm;

  if (!addSymbol(symname, n))
    return SWIG_ERROR;

  if (SwigType_type(type) == T_MPOINTER) {
    String *wname = Swig_name_wrapper(symname);
    String *str = SwigType_str(type, wname);
    Printf(f_header, "static %s = %s;\n", str, value);
    Delete(str);
    value = wname;
  }
  if ((tm = Swig_typemap_lookup("constcode", n, name, 0))) {
    Replaceall(tm, "$source", value);
    Replaceall(tm, "$target", name);
    Replaceall(tm, "$value", CPlusPlus && cppvalue ? cppvalue : value);
    Replaceall(tm, "$nsname", symname);
    String *cname = NewStringf("constant_%s", symname);
    String *wname = Swig_name_wrapper(cname);
    Printf(f_wrappers, "SWIGINTERN int %s(int resc, SwigRV *resv[], int argc, SwigRV *argv[]) {\n", wname);
    Printf(f_wrappers, "  (void)resc; (void)argc; (void)argv;\n");
    Printf(f_wrappers, "  *resv = %s\n", tm);
    Printf(f_wrappers, "  return 0;\n");
    Printf(f_wrappers, "}\n");
    addEntry(cname, wname, 1, 0);
    /* Typed: a const when the value is known here, else a getter */
    std::string rs = ident(Char(symname));
    std::map<std::string, long long>::iterator ev = enum_values.find(Char(symname));
    std::string out = rsOut(type);
    if (!global_names.count(rs)) {
      global_names.insert(rs);
      if (ev != enum_values.end()) {
	Printf(f_rs_globals, "\npub const %s: i64 = %lld;\n", rs.c_str(), ev->second);
      } else if (!out.empty()) {
	Printf(f_rs_globals, "\npub fn %s() -> %s {\n    rt::out1(rt::call_or_panic(ffi::swigrs_%s, vec![], 1))\n}\n", rs.c_str(), out.c_str(), Char(cname));
      }
    }
    Delete(wname);
    Delete(cname);
  } else {
    Swig_warning(WARN_TYPEMAP_CONST_UNDEF, input_file, line_number, "Unsupported constant value.\n");
    return SWIG_NOWRAP;
  }
  return SWIG_OK;
}

/* ------------------------------------------------------------
 * Handlers: record what kind of function is being wrapped
 * ------------------------------------------------------------ */

int RUST::globalfunctionHandler(Node *n) {
  /* Static member functions are wrapped through here too */
  if (cur_kind != CK_STATIC) {
    cur_kind = CK_GLOBAL;
    cur_name = Getattr(n, "sym:name");
  }
  return Language::globalfunctionHandler(n);
}

int RUST::memberfunctionHandler(Node *n) {
  cur_kind = CK_MEMBER;
  cur_name = Getattr(n, "sym:name");
  int r = Language::memberfunctionHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

int RUST::staticmemberfunctionHandler(Node *n) {
  cur_kind = CK_STATIC;
  cur_name = Getattr(n, "sym:name");
  int r = Language::staticmemberfunctionHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

int RUST::constructorHandler(Node *n) {
  cur_kind = CK_CTOR;
  cur_name = Getattr(n, "sym:name");
  int r = Language::constructorHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

int RUST::membervariableHandler(Node *n) {
  cur_kind = CK_VAR;
  cur_name = Getattr(n, "sym:name");
  int r = Language::membervariableHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

/* ------------------------------------------------------------
 * Rust types of C++ types
 * ------------------------------------------------------------ */

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\n");
  return s.substr(a, b - a + 1);
}

/* Split at sep outside of <>, () and [] */
static std::vector<std::string> splitTop(const std::string &s, char sep) {
  std::vector<std::string> r;
  int depth = 0;
  std::string cur;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '<' || c == '(' || c == '[') depth++;
    if (c == '>' || c == ')' || c == ']') depth--;
    if (c == sep && depth == 0) {
      r.push_back(trim(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!trim(cur).empty()) r.push_back(trim(cur));
  return r;
}

static bool startsWith(const std::string &s, const char *p) {
  return s.compare(0, strlen(p), p) == 0;
}

/* Kind of a Rust type, e.g. Vec<f64> -> kind::Vec<kind::F64> */
static std::string kindOf(std::string t) {
  t = trim(t);
  while (startsWith(t, "&")) {
    t = trim(t.substr(1));
    if (startsWith(t, "mut ")) t = trim(t.substr(4));
  }
  if (t == "rt::Dict" || startsWith(t, "rt::Dict<")) return "kind::Dict";
  if (t == "rt::Omit") return "kind::Omit";
  if (startsWith(t, "Vec<") && t[t.size() - 1] == '>')
    return "kind::Vec<" + kindOf(t.substr(4, t.size() - 5)) + ">";
  if ((startsWith(t, "HashMap<") || startsWith(t, "BTreeMap<")) && t[t.size() - 1] == '>') {
    std::vector<std::string> a = splitTop(t.substr(t.find('<') + 1, t.size() - t.find('<') - 2), ',');
    return "kind::Map<" + kindOf(a.size() == 2 ? a[1] : "?") + ">";
  }
  if (startsWith(t, "(") && t[t.size() - 1] == ')') {
    std::vector<std::string> a = splitTop(t.substr(1, t.size() - 2), ',');
    if (a.size() == 2) return "kind::Pair<" + kindOf(a[0]) + ", " + kindOf(a[1]) + ">";
  }
  if (t == "f64" || t == "f32") return "kind::F64";
  if (t == "i8" || t == "i16" || t == "i32" || t == "i64" || t == "isize" || t == "u8" || t == "u16" || t == "u32" || t == "u64" || t == "usize")
    return "kind::I64";
  if (t == "bool") return "kind::Bool";
  if (t == "String" || t == "str") return "kind::Str";
  return "kind::" + t;
}

/* Containers nested in a Rust type */
static int nesting(const std::string &t) {
  int n = 0;
  for (size_t i = 0; i < t.size(); ++i) if (t[i] == '<' || t[i] == '(') n++;
  return n;
}

std::string RUST::ident(const std::string &name) {
  String *s = rustIdent(name.c_str());
  std::string r = Char(s);
  Delete(s);
  return r;
}

SwigType *RUST::bareType(SwigType *t) {
  SwigType *s = Copy(t);
  if (SwigType_isreference(s)) SwigType_del_reference(s);
  SwigType *q = SwigType_strip_qualifiers(s);
  Delete(s);
  if (SwigType_ispointer(q)) {
    SwigType_del_pointer(q);
    SwigType *q2 = SwigType_strip_qualifiers(q);
    Delete(q);
    q = q2;
  }
  return q;
}

String *RUST::lookupTypemap(const char *method, SwigType *t) {
  Parm *p = NewParmWithoutFileLineInfo(t, "x");
  String *tm = Swig_typemap_lookup(method, p, "", 0);
  tm = tm ? Copy(tm) : 0;
  Delete(p);
  return tm;
}

/* Template arguments of a template type */
static List *templateArgs(SwigType *r) {
  String *ta = SwigType_templateargs(r);
  String *inner = NewStringWithSize(Char(ta) + 1, Len(ta) - 2);
  List *args = SwigType_parmlist(inner);
  Delete(inner);
  Delete(ta);
  return args;
}

/* Rust type returned for a C++ type, or "" if unknown */
std::string RUST::rsOut(SwigType *t) {
  SwigType *b = bareType(t);
  std::string out;
  String *tm = lookupTypemap("rstype", b);
  if (tm) {
    out = Char(tm);
  } else {
    SwigType *r = SwigType_typedef_resolve_all(b);
    if (SwigType_isenum(r)) {
      out = "i64";
    } else if (SwigType_istemplate(r)) {
      String *prefix = SwigType_templateprefix(r);
      List *args = templateArgs(r);
      if (Strcmp(prefix, "std::vector") == 0 && Len(args) >= 1) {
	std::string e = rsOut(Getitem(args, 0));
	if (!e.empty()) out = "Vec<" + e + ">";
      } else if (Strcmp(prefix, "std::map") == 0 && Len(args) >= 2) {
	std::string k = rsOut(Getitem(args, 0)), v = rsOut(Getitem(args, 1));
	if (k == "String" && !v.empty()) out = "HashMap<String, " + v + ">";
      } else if (Strcmp(prefix, "std::pair") == 0 && Len(args) == 2) {
	std::string a = rsOut(Getitem(args, 0)), c = rsOut(Getitem(args, 1));
	if (!a.empty() && !c.empty()) out = "(" + a + ", " + c + ")";
      }
      Delete(prefix);
      Delete(args);
    }
    if (out.empty()) {
      Node *cls = classLookup(b);
      if (cls && Len(Getattr(cls, "sym:name")) && !GetFlag(cls, "feature:ignore")) out = Char(Getattr(cls, "sym:name"));
    }
    Delete(r);
  }
  Delete(tm);
  Delete(b);
  return out;
}

/* Rust types accepted for a C++ type: {"*"} for anything, {} if unknown */
std::vector<std::string> RUST::rsAccept(SwigType *t) {
  SwigType *b = bareType(t);
  std::vector<std::string> acc;
  String *tm = lookupTypemap("rsaccept", b);
  if (tm) {
    acc = splitTop(Char(tm), '|');
    for (size_t i = 0; i < acc.size(); ++i) if (acc[i] == "Any") acc = std::vector<std::string>(1, "*");
  } else {
    SwigType *r = SwigType_typedef_resolve_all(b);
    if (SwigType_isenum(r)) {
      acc.push_back("i64");
    } else if (SwigType_istemplate(r)) {
      String *prefix = SwigType_templateprefix(r);
      List *args = templateArgs(r);
      if (Strcmp(prefix, "std::vector") == 0 && Len(args) >= 1) {
	std::vector<std::string> e = rsAccept(Getitem(args, 0));
	if (e.size() == 1 && e[0] == "*") acc = e;
	else for (size_t i = 0; i < e.size(); ++i) if (nesting(e[i]) <= 1) acc.push_back("Vec<" + e[i] + ">");
      } else if (Strcmp(prefix, "std::map") == 0 && Len(args) >= 2) {
	std::vector<std::string> v = rsAccept(Getitem(args, 1));
	if (rsOut(Getitem(args, 0)) == "String" && !v.empty()) {
	  if (!(v.size() == 1 && v[0] == "*"))
	    for (size_t i = 0; i < v.size(); ++i) if (nesting(v[i]) == 0) acc.push_back("HashMap<String, " + v[i] + ">");
	  acc.push_back("rt::Dict");
	}
      } else if (Strcmp(prefix, "std::pair") == 0 && Len(args) == 2) {
	std::vector<std::string> a = rsAccept(Getitem(args, 0)), c = rsAccept(Getitem(args, 1));
	if (!a.empty() && !c.empty()) {
	  if (a[0] == "*" || c[0] == "*") acc.push_back("*");
	  else acc.push_back("(" + a[0] + ", " + c[0] + ")");
	}
      }
      Delete(prefix);
      Delete(args);
    }
    if (acc.empty()) {
      Node *cls = classLookup(b);
      if (cls && Len(Getattr(cls, "sym:name")) && !GetFlag(cls, "feature:ignore")) acc.push_back(Char(Getattr(cls, "sym:name")));
    }
    Delete(r);
  }
  Delete(tm);
  Delete(b);
  return acc;
}

/* Marker trait implemented by the kinds in acc */
std::string RUST::accTrait(const std::vector<std::string> &acc) {
  std::set<std::string> kinds;
  for (size_t i = 0; i < acc.size(); ++i) kinds.insert(kindOf(acc[i]));
  std::string key;
  for (std::set<std::string>::iterator it = kinds.begin(); it != kinds.end(); ++it) key += *it + ";";
  std::map<std::string, std::string>::iterator f = acc_traits.find(key);
  if (f != acc_traits.end()) return f->second;
  char buf[32];
  sprintf(buf, "A%d", (int)acc_traits.size());
  std::string name = buf;
  acc_traits[key] = name;
  Printf(f_rs_acc, "    pub trait %s {}\n", name.c_str());
  for (std::set<std::string>::iterator it = kinds.begin(); it != kinds.end(); ++it)
    Printf(f_rs_acc, "    impl %s for %s {}\n", name.c_str(), it->c_str());
  return name;
}

/* ------------------------------------------------------------
 * Typed Rust functions
 * ------------------------------------------------------------ */

/* Inputs, defaults and outputs of an overload; false if a type has no Rust mapping */
bool RUST::overloadInfo(Node *n, Overload &o) {
  o.n = n;
  o.req = 0;
  o.nin = 0;
  ParmList *l = Getattr(n, "wrap:parms");
  bool skip_self = cur_kind == CK_MEMBER || (cur_kind == CK_VAR && !Getattr(n, "staticmembervariableHandler:sym:name") && l && Getattr(l, "self"));
  bool defaults = false;
  for (Parm *p = l; p; p = nextSibling(p)) {
    if (skip_self) {
      skip_self = false;
      continue;
    }
    if (Getattr(p, "tmap:argout")) {
      std::string r = rsOut(Getattr(p, "type"));
      if (r.empty()) {
	fail_type = Char(SwigType_str(Getattr(p, "type"), 0));
	return false;
      }
      o.outs.push_back(r);
    }
    if (checkAttribute(p, "tmap:in:numinputs", "0")) continue;
    std::vector<std::string> a = rsAccept(Getattr(p, "type"));
    if (a.empty()) {
      fail_type = Char(SwigType_str(Getattr(p, "type"), 0));
      return false;
    }
    o.acc.push_back(a);
    if (Getattr(p, "value")) defaults = true;
    if (!defaults) o.req++;
    o.nin++;
  }
  SwigType *rt = Getattr(n, "type");
  std::string ret;
  if (cur_kind == CK_CTOR) {
    ret = Char(class_name);
  } else if (rt && SwigType_type(rt) != T_VOID) {
    ret = rsOut(rt);
    if (ret.empty()) {
      fail_type = Char(SwigType_str(rt, 0));
      return false;
    }
  }
  if (!ret.empty()) o.outs.insert(o.outs.begin(), ret);
  return true;
}

/* Expression converting the outputs vector o to outs */
static std::string takeExpr(const std::vector<std::string> &outs) {
  if (outs.empty()) return "{ let _ = o; }";
  if (outs.size() == 1) return "rt::out1::<" + outs[0] + ">(o)";
  std::string s = "{ let mut it = o.into_iter(); (";
  for (size_t i = 0; i < outs.size(); ++i)
    s += std::string(i ? ", " : "") + "rt::take::<" + outs[i] + ">(it.next().expect(\"missing output\"))";
  return s + ") }";
}

static std::string outType(const std::vector<std::string> &outs) {
  if (outs.empty()) return "()";
  if (outs.size() == 1) return outs[0];
  std::string s = "(";
  for (size_t i = 0; i < outs.size(); ++i) s += std::string(i ? ", " : "") + outs[i];
  return s + ")";
}

static std::string joined(const std::vector<std::string> &v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) s += v[i] + "|";
  return s;
}

/* One Rust function taking k arguments, resolving among cands (in rank order) */
std::string RUST::emitForm(const std::string &rname, int k, const std::vector<Overload *> &cands, const std::string &ename, int num_returns, bool member) {
  std::vector<bool> varying(k, false);
  for (int i = 0; i < k; ++i)
    for (size_t c = 1; c < cands.size(); ++c)
      if (joined(cands[c]->acc[i]) != joined(cands[0]->acc[i])) varying[i] = true;
  bool any_varying = false;
  for (int i = 0; i < k; ++i) any_varying = any_varying || varying[i];
  bool result = false;
  for (size_t c = 0; c < cands.size(); ++c) result = result || GetFlag(cands[c]->n, "feature:rust:result");

  /* Positions from the fewest required arguments on also take None: the C++ default */
  int minreq = k;
  for (size_t c = 0; c < cands.size(); ++c) minreq = std::min(minreq, cands[c]->req);

  std::string generics, params, args;
  if (member) params = "&self";
  if (member) args = "rt::ToRV::to_rv(self)";
  for (int i = 0; i < k; ++i) {
    char a[16], A[16];
    sprintf(a, "a%d", i);
    sprintf(A, "A%d", i);
    if (!params.empty()) params += ", ";
    if (!args.empty()) args += ", ";
    args += std::string(a) + ".to_rv()";
    if (varying[i]) {
      generics += std::string(generics.empty() ? "" : ", ") + A + ": rt::HasKind + rt::ToRV";
      params += std::string(a) + ": " + A;
    } else if (cands[0]->acc[i][0] == "*") {
      params += std::string(a) + ": impl rt::ToRV";
    } else {
      std::vector<std::string> acc = cands[0]->acc[i];
      if (i >= minreq) acc.push_back("rt::Omit");
      params += std::string(a) + ": impl rt::HasKind<K: acc::" + accTrait(acc) + "> + rt::ToRV";
    }
  }

  std::string out, take, where;
  if (any_varying) {
    /* Bit c of a mask: candidate c accepts. The dispatcher takes the first candidate
       accepting every argument: the lowest bit of the AND over the arguments. */
    char buf[64];
    sprintf(buf, "S%d_", sig_count++);
    std::string sig = buf + rname;
    for (size_t r; (r = sig.find("r#")) != std::string::npos;) sig.erase(r, 2);
    std::set<unsigned long long> masks;
    std::vector<std::string> folds;
    for (int i = 0; i < k; ++i) {
      if (!varying[i]) continue;
      std::map<std::string, unsigned long long> km;
      for (size_t c = 0; c < cands.size() && c < 64; ++c)
	for (size_t m = 0; m < cands[c]->acc[i].size(); ++m)
	  if (cands[c]->acc[i][m] != "*") km[kindOf(cands[c]->acc[i][m])] |= 1ULL << c;
      if (i >= minreq)
	for (size_t c = 0; c < cands.size() && c < 64; ++c)
	  if (cands[c]->req <= i) km["kind::Omit"] |= 1ULL << c;
      char pi[16];
      sprintf(pi, "_P%d", i);
      std::string ptrait = sig + pi;
      Printf(f_rs_sigs, "    pub trait %s {\n        type M;\n    }\n", ptrait.c_str());
      for (std::map<std::string, unsigned long long>::iterator it = km.begin(); it != km.end(); ++it) {
	/* An untyped dict dispatches on its contents at run time: no static output if candidates differ */
	if (it->first == "kind::Dict") {
	  std::set<std::string> outs;
	  for (size_t c = 0; c < cands.size() && c < 64; ++c)
	    if (it->second & (1ULL << c)) outs.insert(outType(cands[c]->outs));
	  if (outs.size() > 1) continue;
	}
	Printf(f_rs_sigs, "    impl %s for %s {\n        type M = rt::Mask<%llu>;\n    }\n", ptrait.c_str(), it->first.c_str(), it->second);
	masks.insert(it->second);
      }
      char A[16];
      sprintf(A, "A%d", i);
      folds.push_back("<" + std::string(A) + "::K as sigs::" + ptrait + ">::M");
      where += std::string(where.empty() ? " where " : ", ") + A + "::K: sigs::" + ptrait;
    }
    /* All ANDs of the masks */
    std::set<unsigned long long> closure = masks;
    for (bool grew = true; grew;) {
      grew = false;
      std::vector<unsigned long long> cur(closure.begin(), closure.end());
      for (size_t a = 0; a < cur.size(); ++a)
	for (size_t b = 0; b < masks.size(); ++b) {
	  unsigned long long r = cur[a] & *std::next(masks.begin(), b);
	  if (!closure.count(r)) {
	    closure.insert(r);
	    grew = true;
	  }
	}
    }
    for (std::set<unsigned long long>::iterator a = closure.begin(); a != closure.end(); ++a)
      for (std::set<unsigned long long>::iterator b = masks.begin(); b != masks.end(); ++b) {
	char key[64];
	sprintf(key, "%llu&%llu", *a, *b);
	if (joins.count(key)) continue;
	joins.insert(key);
	Printf(f_rs_sigs, "    impl rt::Join<rt::Mask<%llu>> for rt::Mask<%llu> {\n        type Out = rt::Mask<%llu>;\n    }\n", *b, *a, *a & *b);
      }
    std::string ptrait = sig + "_Pick";
    Printf(f_rs_sigs, "    pub trait %s {\n        type Out;\n        fn take(o: Vec<rt::RV>) -> Self::Out;\n    }\n", ptrait.c_str());
    for (std::set<unsigned long long>::iterator a = closure.begin(); a != closure.end(); ++a) {
      if (!*a) continue;
      size_t c = 0;
      while (!(*a & (1ULL << c))) c++;
      Printf(f_rs_sigs, "    impl %s for rt::Mask<%llu> {\n        type Out = %s;\n        fn take(o: Vec<rt::RV>) -> %s {\n            %s\n        }\n    }\n",
	     ptrait.c_str(), *a, outType(cands[c]->outs).c_str(), outType(cands[c]->outs).c_str(), takeExpr(cands[c]->outs).c_str());
    }
    /* Fold the masks left to right */
    std::string acc = folds[0];
    for (size_t f = 1; f < folds.size(); ++f) {
      where += ", " + acc + ": rt::Join<" + folds[f] + ">";
      acc = "<" + acc + " as rt::Join<" + folds[f] + ">>::Out";
    }
    /* A common output type is spelled out, so callers need no inference through the masks */
    std::string common = outType(cands[0]->outs);
    for (size_t c = 1; c < cands.size() && c < 64; ++c)
      if (outType(cands[c]->outs) != common) common.clear();
    if (common.empty()) {
      where += ", " + acc + ": sigs::" + ptrait;
      out = "<" + acc + " as sigs::" + ptrait + ">::Out";
    } else {
      where += ", " + acc + ": sigs::" + ptrait + "<Out = " + common + ">";
      out = common;
    }
    take = "<" + acc + " as sigs::" + ptrait + ">::take(o)";
  } else {
    out = outType(cands[0]->outs);
    take = takeExpr(cands[0]->outs);
  }

  char nr[16];
  sprintf(nr, "%d", num_returns);
  std::string code = "    pub fn " + rname + (generics.empty() ? "" : "<" + generics + ">") + "(" + params + ") -> ";
  if (result) {
    code += "Result<" + out + ", rt::Error>" + where + " {\n";
    code += "        let o = rt::call(ffi::" + ename + ", vec![" + args + "], " + nr + ")?;\n";
    code += "        Ok(" + take + ")\n";
  } else {
    code += out + where + " {\n";
    code += "        let o = rt::call_or_panic(ffi::" + ename + ", vec![" + args + "], " + nr + ");\n";
    code += "        " + take + "\n";
  }
  code += "    }\n";
  return code;
}

/* Names an overload group: plain = fewest arguments, _with = most, numbered otherwise */
std::string RUST::emitGroup(std::vector<Overload> &ovs, const std::string &base, const std::string &ename, int num_returns, bool member) {
  std::string code;
  if (ovs.empty()) return code;
  /* Plain: fewest arguments; _with: most arguments */
  int kmin = ovs[0].req, kmax = ovs[0].nin;
  for (size_t i = 1; i < ovs.size(); ++i) {
    kmin = std::min(kmin, ovs[i].req);
    kmax = std::max(kmax, ovs[i].nin);
  }
  std::map<int, std::string> forms;
  forms[kmin] = base;
  if (kmax != kmin) forms[kmax] = base + "_with";
  for (size_t i = 0; i < ovs.size(); ++i) {
    bool covered = (ovs[i].req <= kmin && kmin <= ovs[i].nin) || (ovs[i].req <= kmax && kmax <= ovs[i].nin);
    if (!covered && !forms.count(ovs[i].nin)) {
	char buf[16];
	sprintf(buf, "_%d", ovs[i].nin);
	forms[ovs[i].nin] = base + buf;
	String *decl = Swig_name_decl(ovs[i].n);
	Swig_warning(WARN_LANG_OVERLOAD_IGNORED, input_file, line_number, "Rust: %s wrapped as %s%s (no plain or _with form takes %d arguments).\n", decl, base.c_str(), buf, ovs[i].nin);
	Delete(decl);
    }
  }
  if (getenv("RUST_UNREACHABLE"))
    for (size_t i = 0; i < ovs.size(); ++i)
      for (int k = ovs[i].req; k <= ovs[i].nin; ++k)
	if (!forms.count(k)) {
	  String *decl = Swig_name_decl(ovs[i].n);
	  Printf(stderr, "UNREACHABLE %s %d %s %s\n", base.c_str(), k, ovs[i].nin == forms.rbegin()->first ? "PREFIX" : "OTHER", decl);
	  Delete(decl);
	}
  for (std::map<int, std::string>::iterator f = forms.begin(); f != forms.end(); ++f) {
    std::vector<Overload *> cands;
    for (size_t i = 0; i < ovs.size(); ++i)
	if (ovs[i].req <= f->first && f->first <= ovs[i].nin) cands.push_back(&ovs[i]);
    code += emitForm(ident(f->second), f->first, cands, ename, num_returns, member);
  }
  return code;
}

void RUST::emitTyped(Node *n, const std::string &ename, int num_returns) {
  /* Destructors run from Drop */
  if (Cmp(Getattr(n, "nodeType"), "destructor") == 0) return;
  /* Overloads in the order the C++ dispatcher tries them */
  std::vector<Node *> nodes;
  if (Getattr(n, "sym:overloaded")) {
    List *ranked = Swig_overload_rank(n, false);
    for (int i = 0; ranked && i < Len(ranked); ++i) nodes.push_back((Node *)Getitem(ranked, i));
  } else {
    nodes.push_back(n);
  }
  std::vector<Overload> ovs;
  std::string skipped;
  for (size_t i = 0; i < nodes.size(); ++i) {
    Overload o;
    if (overloadInfo(nodes[i], o)) {
      ovs.push_back(o);
    } else {
      String *decl = Swig_name_decl(nodes[i]);
      skipped += "    // no Rust mapping for " + fail_type + ": " + std::string(Char(decl)) + "\n";
      Delete(decl);
    }
  }

  /* Rust name */
  std::string base = cur_name ? Char(cur_name) : "";
  bool member = cur_kind == CK_MEMBER;
  if (cur_kind == CK_CTOR) {
    /* A renamed constructor is a named one, e.g. from_ccs */
    base = Cmp(cur_name, class_name) == 0 ? "new" : Char(cur_name);
  } else if (cur_kind == CK_VAR) {
    std::string iname = Char(Getattr(n, "sym:name"));
    bool setter = iname.size() > 4 && iname.compare(iname.size() - 4, 4, "_set") == 0;
    base = setter ? "set_" + base : base;
    member = !ovs.empty() && ovs[0].nin < (int)(Len(Getattr(n, "wrap:parms")) ? ParmList_len(Getattr(n, "wrap:parms")) : 0);
  }

  /* Constructors: copy -> Clone, default -> Default */
  if (cur_kind == CK_CTOR && class_node) {
    std::string rcls = ident(Char(class_name));
    std::vector<Overload> rest;
    for (size_t i = 0; i < ovs.size(); ++i) {
      ParmList *l = Getattr(ovs[i].n, "wrap:parms");
      SwigType *b = ovs[i].nin == 1 && l ? bareType(Getattr(l, "type")) : 0;
      bool copy = b && classLookup(b) == class_node;
      Delete(b);
      char nr[16];
      sprintf(nr, "%d", num_returns);
      if (copy) {
	if (base == "new")
	  class_traits[class_node] += "impl Clone for " + rcls + " {\n    fn clone(&self) -> Self {\n        rt::out1(rt::call_or_panic(ffi::" + ename + ", vec![rt::ToRV::to_rv(self)], " + nr + "))\n    }\n}\n\n";
      } else if (ovs[i].req == 0 && base == "new") {
	if (class_traits[class_node].find("impl Default") == std::string::npos)
	  class_traits[class_node] += "impl Default for " + rcls + " {\n    fn default() -> Self {\n        rt::out1(rt::call_or_panic(ffi::" + ename + ", vec![], " + nr + "))\n    }\n}\n\n";
      } else {
	rest.push_back(ovs[i]);
      }
    }
    ovs = rest;
  }

  /* List twins: a std::vector<T> parameter where a sibling takes T are named <base>_list */
  std::vector<Overload> plain, twins;
  for (size_t i = 0; i < ovs.size(); ++i) {
    bool twin = false;
    for (size_t p = 0; !twin && p < ovs[i].acc.size(); ++p) {
      const std::vector<std::string> &a = ovs[i].acc[p];
      std::set<std::string> elems;
      bool all_vec = !a.empty();
      for (size_t m = 0; all_vec && m < a.size(); ++m) {
	all_vec = a[m].compare(0, 4, "Vec<") == 0;
	if (all_vec) elems.insert(a[m].substr(4, a[m].size() - 5));
      }
      /* Lists of numbers are values, not repetitions (offsets vs an increment) */
      std::string first = all_vec ? a[0].substr(4, a[0].size() - 5) : "";
      if (!all_vec || first == "f64" || first == "i64" || first == "bool" || first == "String") continue;
      for (size_t j = 0; !twin && j < ovs.size(); ++j) {
	if (j == i || ovs[j].acc.size() <= p) continue;
	std::set<std::string> sib(ovs[j].acc[p].begin(), ovs[j].acc[p].end());
	bool same = sib.count(first) > 0;
	for (std::set<std::string>::iterator e = elems.begin(); same && e != elems.end(); ++e) same = sib.count(*e) || e->compare(0, 4, "Vec<") == 0;
	for (size_t q = 0; same && q < p; ++q) same = ovs[j].acc[q] == ovs[i].acc[q];
	twin = same;
      }
    }
    (twin ? twins : plain).push_back(ovs[i]);
  }

  std::string code = skipped;
  code += emitGroup(plain, base, ename, num_returns, member);
  code += emitGroup(twins, base + "_list", ename, num_returns, member);

  if (class_node && cur_kind != CK_GLOBAL) {
    Snippet s;
    s.name = base;
    s.code = code;
    s.inherit = cur_kind != CK_CTOR;
    class_snippets[class_node].push_back(s);
  } else {
    if (global_names.count(base)) {
      Printf(f_rs_globals, "// duplicate Rust name %s\n", base.c_str());
    } else {
      global_names.insert(base);
      String *c = NewString(("\n" + code).c_str());
      Replaceall(c, "\n    ", "\n");
      Printf(f_rs_globals, "%s", c);
      Delete(c);
    }
  }
}

/* ------------------------------------------------------------
 * classHandler(): Rust proxy struct
 * ------------------------------------------------------------ */

int RUST::classHandler(Node *n) {
  String *old_class_name = class_name;
  Node *old_class_node = class_node;
  bool old_has_destructor = class_has_destructor;
  class_name = Getattr(n, "sym:name");
  class_node = n;
  class_has_destructor = false;
  Setattr(n, "rust:proxy", "1");

  SwigType *t = Copy(Getattr(n, "name"));
  SwigType_add_pointer(t);
  SwigType_remember(t);
  String *mangled = SwigType_manglestr(t);

  Language::classHandler(n);

  String *rname = rustIdent(class_name);
  Printf(f_rs_kinds, "    pub struct %s;\n", rname);
  Printf(f_rs_classes, "pub struct %s {\n", rname);
  Printf(f_rs_classes, "    pub h: rt::Handle,\n");
  Printf(f_rs_classes, "}\n\n");
  Printf(f_rs_classes, "impl rt::HasKind for %s {\n", rname);
  Printf(f_rs_classes, "    type K = kind::%s;\n", rname);
  Printf(f_rs_classes, "}\n\n");
  Printf(f_rs_classes, "#[allow(non_snake_case, clippy::all)]\n");
  Printf(f_rs_classes, "impl %s {\n", rname);
  Printf(f_rs_classes, "    pub fn type_info() -> *mut std::ffi::c_void {\n");
  Printf(f_rs_classes, "        static T: std::sync::OnceLock<usize> = std::sync::OnceLock::new();\n");
  Printf(f_rs_classes, "        *T.get_or_init(|| rt::type_query(\"%s\") as usize) as *mut std::ffi::c_void\n", mangled);
  Printf(f_rs_classes, "    }\n");

  /* Own methods, then inherited ones not shadowed */
  std::set<std::string> names;
  std::vector<Snippet> &own = class_snippets[n];
  for (size_t i = 0; i < own.size(); ++i) {
    if (names.count(own[i].name)) continue;
    names.insert(own[i].name);
    Printf(f_rs_classes, "%s", own[i].code.c_str());
  }
  /* Breadth-first over bases ("allbases" misses %template grandparents) */
  std::vector<Node *> queue(1, n);
  std::set<Node *> seen(queue.begin(), queue.end());
  for (size_t q = 0; q < queue.size(); ++q) {
    List *bases = Getattr(queue[q], "bases");
    for (int b = 0; bases && b < Len(bases); ++b) {
      Node *base = Getitem(bases, b);
      if (!seen.insert(base).second) continue;
      queue.push_back(base);
      std::vector<Snippet> &inh = class_snippets[base];
      for (size_t i = 0; i < inh.size(); ++i) {
	if (!inh[i].inherit || names.count(inh[i].name)) continue;
	names.insert(inh[i].name);
	Printf(f_rs_classes, "%s", inh[i].code.c_str());
      }
    }
  }
  Printf(f_rs_classes, "}\n\n");

  Printf(f_rs_classes, "%s", class_traits[n].c_str());
  Printf(f_rs_classes, "impl rt::ToRV for %s {\n", rname);
  Printf(f_rs_classes, "    fn to_rv(&self) -> rt::RV {\n");
  Printf(f_rs_classes, "        self.h.to_rv()\n");
  Printf(f_rs_classes, "    }\n");
  Printf(f_rs_classes, "}\n\n");
  Printf(f_rs_classes, "impl rt::FromRV for %s {\n", rname);
  Printf(f_rs_classes, "    fn from_rv(v: rt::RVRef) -> Option<Self> {\n");
  if (class_has_destructor)
    Printf(f_rs_classes, "        rt::Handle::from_rv(v, Self::type_info(), Some(ffi::swigrs_delete_%s)).map(|h| %s { h })\n", class_name, rname);
  else
    Printf(f_rs_classes, "        rt::Handle::from_rv(v, Self::type_info(), None).map(|h| %s { h })\n", rname);
  Printf(f_rs_classes, "    }\n");
  Printf(f_rs_classes, "}\n\n");

  Delete(rname);
  Delete(mangled);
  Delete(t);
  class_name = old_class_name;
  class_node = old_class_node;
  class_has_destructor = old_has_destructor;
  return SWIG_OK;
}

int RUST::destructorHandler(Node *n) {
  return Language::destructorHandler(n);
}

extern "C" Language *swig_rust(void) {
  return new RUST();
}
