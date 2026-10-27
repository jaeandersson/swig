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
  virtual int classHandler(Node *n);
  virtual int destructorHandler(Node *n);
  virtual int insertDirective(Node *n);

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
  String *module_name;
  String *class_name;
  bool class_has_destructor;

  void dispatchFunction(Node *n);
  void addEntry(String *iname, String *wname, int num_returns);
  String *rustIdent(const_String_or_char_ptr name);

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
  module_name(0), class_name(0), class_has_destructor(false) {
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
  Printf(f_rs, "%s", f_rs_classes);
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

void RUST::addEntry(String *iname, String *wname, int num_returns) {
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
    addEntry(iname, wname, num_returns);
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
  addEntry(iname, wname, num_returns);
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
    addEntry(cname, wname, 1);
    Delete(wname);
    Delete(cname);
  } else {
    Swig_warning(WARN_TYPEMAP_CONST_UNDEF, input_file, line_number, "Unsupported constant value.\n");
    return SWIG_NOWRAP;
  }
  return SWIG_OK;
}

/* ------------------------------------------------------------
 * classHandler(): Rust proxy struct
 * ------------------------------------------------------------ */

int RUST::classHandler(Node *n) {
  String *old_class_name = class_name;
  bool old_has_destructor = class_has_destructor;
  class_name = Getattr(n, "sym:name");
  class_has_destructor = false;

  SwigType *t = Copy(Getattr(n, "name"));
  SwigType_add_pointer(t);
  SwigType_remember(t);
  String *mangled = SwigType_manglestr(t);

  Language::classHandler(n);

  String *rname = rustIdent(class_name);
  Printf(f_rs_classes, "pub struct %s {\n", rname);
  Printf(f_rs_classes, "    pub h: rt::Handle,\n");
  Printf(f_rs_classes, "}\n\n");
  Printf(f_rs_classes, "impl %s {\n", rname);
  Printf(f_rs_classes, "    pub fn type_info() -> *mut std::ffi::c_void {\n");
  Printf(f_rs_classes, "        static T: std::sync::OnceLock<usize> = std::sync::OnceLock::new();\n");
  Printf(f_rs_classes, "        *T.get_or_init(|| rt::type_query(\"%s\") as usize) as *mut std::ffi::c_void\n", mangled);
  Printf(f_rs_classes, "    }\n");
  Printf(f_rs_classes, "}\n\n");
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
  class_has_destructor = old_has_destructor;
  return SWIG_OK;
}

int RUST::destructorHandler(Node *n) {
  return Language::destructorHandler(n);
}

extern "C" Language *swig_rust(void) {
  return new RUST();
}
