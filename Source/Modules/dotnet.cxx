/* -----------------------------------------------------------------------------
 * This file is part of SWIG, which is licensed as a whole under version 3
 * (or any later version) of the GNU General Public License. Some additional
 * terms also apply to certain portions of SWIG. The full details of the SWIG
 * license and copyrights can be found in the LICENSE and COPYRIGHT files
 * included with the SWIG source code as distributed by the SWIG developers
 * and at https://www.swig.org/legal.html.
 *
 * dotnet.cxx
 *
 * .NET (C#) language module for SWIG with guest values.
 *
 * C++ side (as the -rust module): every argument and result crosses a plain
 * C ABI as a SwigRV* (Lib/dotnet/dotnetrun.swg), built and read by the C#
 * runtime (Lib/dotnet/csrun.cs) through an extern "C" API.  Each overload
 * gets an exported entry, and each overload set a C++ dispatcher running the
 * typecheck typemaps in SWIG's order.
 *
 * C# side (as the -julia module): overloads map onto C# overloads.  Each
 * distinct C# signature calls the entry of its own overload; overloads whose
 * C# signatures coincide are merged into one C# method calling the
 * dispatcher.  Trailing default arguments become one C# overload per arity.
 *
 * C# types come from the cstype typemap (primitives, casadi's GenericType,
 * ...), std::vector / std::map<std::string, .> / std::pair, and the proxy
 * classes.  Proxies inherit from their first wrapped base; members of further
 * bases are repeated in the derived class.
 * ----------------------------------------------------------------------------- */

#include "swigmod.h"
#include "cparse.h"
#include <string>
#include <vector>
#include <map>
#include <set>

static const char *usage = (char *) "\
.NET Options (available with -dotnet)\n\
     -namespace <ns>     - C# namespace of the generated code (default: module name)\n\
     -moduleclass <name> - static class holding the free functions (default: module name)\n\
     -dllname <name>     - native library loaded by DllImport (default: <module>_dotnet_wrap)\n\
\n";

static const char *cs_keywords[] = {
  "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue",
  "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
  "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock",
  "long", "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected",
  "public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string",
  "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort",
  "using", "virtual", "void", "volatile", "while", 0
};

/* Members of object and SwigObject a wrapped method must not hide */
static const char *cs_reserved_members[] = {
  "Equals", "GetHashCode", "GetType", "ToString", "Finalize", "MemberwiseClone", "ReferenceEquals",
  "Dispose", "IsValid", 0
};

class DOTNET : public Language {
public:
  DOTNET();
  virtual void main(int argc, char *argv[]);
  virtual int top(Node *n);
  virtual int functionWrapper(Node *n);
  virtual int variableWrapper(Node *n);
  virtual int constantWrapper(Node *n);
  virtual int classDirectorInit(Node *n);
  virtual int classDirectorEnd(Node *n);
  virtual int classDirectorConstructor(Node *n);
  virtual int classDirectorDefaultConstructor(Node *n);
  virtual int classDirectorMethod(Node *n, Node *parent, String *super);
  virtual int classDirectorDisown(Node *n);
  virtual int enumDeclaration(Node *n);
  virtual int enumvalueDeclaration(Node *n);
  virtual int classHandler(Node *n);
  virtual int insertDirective(Node *n);
  virtual int globalfunctionHandler(Node *n);
  virtual int memberfunctionHandler(Node *n);
  virtual int staticmemberfunctionHandler(Node *n);
  virtual int constructorHandler(Node *n);
  virtual int destructorHandler(Node *n);
  virtual int membervariableHandler(Node *n);
  virtual int staticmembervariableHandler(Node *n);

private:
  File *f_begin;
  String *f_runtime;
  String *f_header;
  String *f_wrappers;
  String *f_init;
  String *f_initbeforefunc;
  String *f_directors;
  String *f_directors_h;
  String *f_cs_runtime;   /* csrun.cs */
  String *f_cs_code;      /* %insert("dotnet"): namespace level */
  String *f_cs_module;    /* %insert("dotnetmodule"): module class body */
  String *f_cs_native;    /* DllImport declarations */
  String *f_cs_classes;
  String *f_cs_globals;   /* module class members */
  String *f_cs_register;  /* proxy factories */
  String *f_cs_init;      /* %insert("dotnetinit"): runs once, at the first call */
  String *module_name;
  String *ns_name;
  String *moduleclass_name;
  String *dll_name;
  String *class_name;
  Node *class_node;
  bool class_has_destructor;
  String *class_proxycode;

  enum CallKind { CK_GLOBAL, CK_MEMBER, CK_STATIC, CK_CTOR, CK_VAR };
  CallKind cur_kind;
  String *cur_name;

  /* One C# member of a class (or of the module class) */
  struct Member {
    std::string name;     /* C# name, for hiding across bases */
    std::string key;      /* name + C# parameter types: identical keys collide */
    std::string code;
    bool inherit;
  };
  std::map<Node *, std::vector<Member> > class_members;
  std::vector<Member> global_members;
  /* Properties being assembled from getters and setters: class -> name -> parts */
  struct Prop {
    std::string type, get, set;
    bool is_static;
  };
  std::map<Node *, std::map<std::string, Prop> > class_props;
  std::vector<Node *> class_order;
  std::map<Node *, std::string> class_proxy;   /* proxycode of each class */
  std::map<Node *, bool> class_deleter;
  std::map<std::string, long long> enum_values;
  long long enum_next;
  std::string fail_type;
  /* Docs: emitted once per (class, name) */
  std::set<std::string> documented;

  /* One overload as seen from C# */
  struct Overload {
    Node *n;
    std::string entry;               /* exported entry of this overload */
    int req, nin;
    bool self_director;              /* constructor taking the director self first */
    std::vector<std::string> types;  /* C# parameter types */
    std::vector<std::string> names;  /* C# parameter names */
    std::vector<std::string> outs;   /* C# output types: return value, then argouts */
  };

  void dispatchFunction(Node *n);
  void exportEntry(String *ename, String *wname);
  void emitCs(Node *n, const std::string &dispatcher);
  bool overloadInfo(Node *n, Overload &o);
  std::string csIdent(const std::string &name);
  std::string csMember(const std::string &name);
  SwigType *bareType(SwigType *t);
  String *lookupTypemap(const char *method, SwigType *t);
  std::string csType(SwigType *t, bool out);
  std::string docComment(Node *n, const std::string &indent);
  void addMember(const Member &m);
  void emitClass(Node *n);
  Node *primaryBase(Node *n);
  bool hasMembers(Node *n, std::set<Node *> &seen);
  size_t memberCount(Node *n, std::set<Node *> &seen);

  const char *get_implicitconv_flag(Node *klass) {
    int conv = 0;
    if (klass && GetFlag(klass, "feature:implicitconv")) {
      conv = 1;
    }
    return conv ? "SWIG_POINTER_IMPLICIT_CONV" : "0";
  }
};

DOTNET::DOTNET() :
  f_begin(0), f_runtime(0), f_header(0), f_wrappers(0), f_init(0), f_initbeforefunc(0),
  f_directors(0), f_directors_h(0), f_cs_runtime(0), f_cs_code(0), f_cs_module(0), f_cs_native(0),
  f_cs_classes(0), f_cs_globals(0), f_cs_register(0), f_cs_init(0), module_name(0), ns_name(0), moduleclass_name(0), dll_name(0),
  class_name(0), class_node(0), class_has_destructor(false), class_proxycode(0),
  cur_kind(CK_GLOBAL), cur_name(0), enum_next(0) {
  /* Directors: the constructor gets the C# object as hidden first argument */
  Delete(none_comparison);
  none_comparison = NewString("$arg && $arg->kind == SWIG_RV_FUN");
  director_prot_ctor_code = NewString("");
  Printv(director_prot_ctor_code,
	 "if ( $comparison ) { /* subclassed */\n",
	 "  $director_new \n",
	 "} else {\n", "  SWIG_exception_fail(SWIG_RuntimeError, \"accessing abstract class or protected constructor\");\n", "}\n", NIL);
  director_multiple_inheritance = 1;
  directorLanguage();
}

void DOTNET::main(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    if (!argv[i]) continue;
    if (strcmp(argv[i], "-help") == 0) {
      fputs(usage, stdout);
    } else if ((strcmp(argv[i], "-namespace") == 0 || strcmp(argv[i], "-moduleclass") == 0 || strcmp(argv[i], "-dllname") == 0) && i + 1 < argc && argv[i + 1]) {
      String *v = NewString(argv[i + 1]);
      if (strcmp(argv[i], "-namespace") == 0) ns_name = v;
      else if (strcmp(argv[i], "-moduleclass") == 0) moduleclass_name = v;
      else dll_name = v;
      Swig_mark_arg(i);
      Swig_mark_arg(i + 1);
      i++;
    }
  }
  SWIG_library_directory("dotnet");
  Preprocessor_define("SWIGDOTNET 1", 0);
  SWIG_config_file("dotnet.swg");
  allow_overloading();
}

std::string DOTNET::csIdent(const std::string &name) {
  for (const char **k = cs_keywords; *k; ++k)
    if (name == *k) return "@" + name;
  return name;
}

/* Member name: keywords escaped, names of object/SwigObject members get a trailing _ */
std::string DOTNET::csMember(const std::string &name) {
  for (const char **k = cs_reserved_members; *k; ++k)
    if (name == *k) return name + "_";
  return csIdent(name);
}

int DOTNET::top(Node *n) {
  module_name = Copy(Getattr(n, "name"));
  if (!ns_name) ns_name = Copy(module_name);
  if (!moduleclass_name) moduleclass_name = Copy(module_name);
  if (!dll_name) dll_name = NewStringf("%s_dotnet_wrap", module_name);
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
  f_cs_runtime = NewString("");
  f_cs_code = NewString("");
  f_cs_module = NewString("");
  f_cs_native = NewString("");
  f_cs_classes = NewString("");
  f_cs_globals = NewString("");
  f_cs_register = NewString("");
  f_cs_init = NewString("");

  Swig_register_filebyname("begin", f_begin);
  Swig_register_filebyname("runtime", f_runtime);
  Swig_register_filebyname("header", f_header);
  Swig_register_filebyname("wrapper", f_wrappers);
  Swig_register_filebyname("init", f_init);
  Swig_register_filebyname("initbeforefunc", f_initbeforefunc);
  Swig_register_filebyname("director", f_directors);
  Swig_register_filebyname("director_h", f_directors_h);
  Swig_register_filebyname("csruntime", f_cs_runtime);
  Swig_register_filebyname("dotnet", f_cs_code);
  Swig_register_filebyname("dotnetmodule", f_cs_module);
  Swig_register_filebyname("dotnetinit", f_cs_init);

  Node *mod = Getattr(n, "module");
  Node *options = mod ? Getattr(mod, "options") : 0;
  if (options && Getattr(options, "directors"))
    allow_directors();

  Swig_banner(f_begin);
  Printf(f_runtime, "\n\n#ifndef SWIGDOTNET\n#define SWIGDOTNET\n#endif\n\n");

  Language::top(n);

  if (Swig_directors_enabled())
    Swig_insert_file("director.swg", f_runtime);

  SwigType_emit_type_table(f_runtime, f_wrappers);

  Dump(f_runtime, f_begin);
  Dump(f_header, f_begin);
  Dump(f_directors_h, f_begin);
  Dump(f_directors, f_begin);
  Dump(f_wrappers, f_begin);
  Dump(f_initbeforefunc, f_begin);
  Printf(f_begin, "extern \"C\" SWIGEXPORT void swig_dotnet_init() {\n");
  Printf(f_begin, "  static int loaded = 0;\n");
  Printf(f_begin, "  if (loaded) return;\n");
  Printf(f_begin, "  loaded = 1;\n");
  Printf(f_begin, "  SWIG_InitializeModule(0);\n");
  Dump(f_init, f_begin);
  Printf(f_begin, "}\n");

  /* C# side */
  for (size_t i = 0; i < class_order.size(); ++i)
    emitClass(class_order[i]);

  String *csfile = NewStringf("%s%s.cs", SWIG_output_directory(), module_name);
  File *f_cs = NewFile(csfile, "w", SWIG_output_files());
  if (!f_cs) {
    FileErrorDisplay(csfile);
    Exit(EXIT_FAILURE);
  }
  Printf(f_cs, "// This file was automatically generated by SWIG (https://www.swig.org).\n");
  Printf(f_cs, "// Do not make changes to this file unless you know what you are doing.\n\n");
  Printf(f_cs, "#pragma warning disable 108, 114, 1591, 3021, 8981\n\n");
  Printf(f_cs, "namespace %s {\n\n", ns_name);
  Printf(f_cs, "%s\n", f_cs_runtime);
  Printf(f_cs, "internal static class SwigNative {\n");
  Printf(f_cs, "  internal const string Lib = \"%s\";\n", dll_name);
  Printf(f_cs, "%s", f_cs_native);
  Printf(f_cs, "}\n\n");
  Printf(f_cs, "internal static class SwigProxies {\n  internal static void Register() {\n%s%s  }\n}\n\n", f_cs_register, f_cs_init);
  Printf(f_cs, "%s", f_cs_classes);
  Printf(f_cs, "public static partial class %s {\n", moduleclass_name);
  {
    std::set<std::string> keys;
    for (size_t i = 0; i < global_members.size(); ++i) {
      if (!global_members[i].key.empty() && !keys.insert(global_members[i].key).second) continue;
      Printf(f_cs, "%s", global_members[i].code.c_str());
    }
    std::map<std::string, Prop> &gp = class_props[0];
    for (std::map<std::string, Prop>::iterator it = gp.begin(); it != gp.end(); ++it) {
      Prop &p = it->second;
      if (p.type.empty() || p.get.empty()) continue;
      Printf(f_cs, "  public static %s %s {\n    get { %s }\n", p.type.c_str(), it->first.c_str(), p.get.c_str());
      if (!p.set.empty()) Printf(f_cs, "    set { %s }\n", p.set.c_str());
      Printf(f_cs, "  }\n");
    }
  }
  Printf(f_cs, "%s", f_cs_globals);
  Printf(f_cs, "%s", f_cs_module);
  Printf(f_cs, "}\n\n");
  Printf(f_cs, "%s\n", f_cs_code);
  Printf(f_cs, "}\n");
  Delete(f_cs);
  Delete(csfile);

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

int DOTNET::insertDirective(Node *n) {
  String *section = Getattr(n, "section");
  if (!ImportMode && section && Cmp(section, "proxycode") == 0) {
    if (class_proxycode) {
      String *code = Getattr(n, "code");
      Swig_typemap_replace_embedded_typemap(code, n);
      int offset = Len(code) > 0 && *Char(code) == '\n' ? 1 : 0;
      Printv(class_proxycode, Char(code) + offset, "\n", NIL);
    }
    return SWIG_OK;
  }
  return Language::insertDirective(n);
}

/* ------------------------------------------------------------
 * exportEntry(): exported C trampoline and its DllImport
 * ------------------------------------------------------------ */

void DOTNET::exportEntry(String *ename, String *wname) {
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
  Printf(f_cs_native, "  [System.Runtime.InteropServices.DllImport(Lib, CallingConvention = System.Runtime.InteropServices.CallingConvention.Cdecl)]\n");
  Printf(f_cs_native, "  internal static extern int %s(int resc, [System.Runtime.InteropServices.In, System.Runtime.InteropServices.Out] System.IntPtr[] resv, int argc, [System.Runtime.InteropServices.In] System.IntPtr[] argv);\n", ename);
}

/* ------------------------------------------------------------
 * functionWrapper(): as in the MATLAB and Rust modules, with SwigRV* values
 * ------------------------------------------------------------ */

int DOTNET::functionWrapper(Node *n) {
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

  if (varargs) {
    if (p && (tm = Getattr(p, "tmap:in"))) {
      Replaceall(tm, "$input", "varargs");
      Printv(f->code, tm, "\n", NIL);
    }
  }

  for (p = l; p;) {
    if ((tm = Getattr(p, "tmap:check"))) {
      Replaceall(tm, "$target", Getattr(p, "lname"));
      Printv(f->code, tm, "\n", NIL);
      p = Getattr(p, "tmap:check:next");
    } else {
      p = nextSibling(p);
    }
  }

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

  int num_returns = 1;

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

  /* A call from C# on a director object runs the C++ implementation */
  int director_method = is_member_director(n) && !is_smart_pointer() && !destructor;
  if (director_method) {
    Wrapper_add_local(f, "director", "Swig::Director *director = 0");
    Append(f->code, "director = SWIG_DIRECTOR_CAST(arg1);\n");
    Wrapper_add_local(f, "upcall", "bool upcall = false");
    Append(f->code, "upcall = (director != 0);\n");
  }
  Swig_director_emit_dynamic_cast(n, f);

  if (destructor) {
    Append(f->code, "if (is_owned) {\n");
  }
  String *actioncode = emit_action(n);
  if (destructor) {
    Append(actioncode, "}\n");
  }

  Wrapper_add_local(f, "_out", "SwigRV * _out");

  if ((tm = Swig_typemap_lookup_out("out", n, Swig_cresult_name(), f, actioncode))) {
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
  Setattr(n, "dotnet:num_returns", NewStringf("%d", num_returns));

  Printf(f->code, "return 0;\n");
  Printf(f->code, "fail:\n");

  if (checkAttribute(n, "feature:customdoc", "1")) {
    String *protoType = NewString("");
    Printf(protoType, "    \"    ");
    Swig_prototype(n, protoType, "style_error");
    Printf(protoType, "\\n\"\n");
    Printf(f->code, "if (SWIG_lasterror_code == SWIG_TypeError) {\n");
    Printf(f->code, "std::string type_name = SWIG_Dotnet_ArgsTypeDesc(argc, argv);\n");
    Printf(f->code, "SWIG_Error_Format(SWIG_RuntimeError, \"No matching function for overload function '%s'.\"\n", Getattr(n, "sym:name"));
    Printf(f->code, "   \"  Prototype:\\n\"\n%s\n     \"  You have: %%s\", type_name.c_str());\n", protoType);
    Printf(f->code, "}\n");
    Delete(protoType);
  }

  Printv(f->code, cleanup, NIL);
  Printf(f->code, "return 1;\n");
  Printf(f->code, "}\n");

  Replaceall(f->code, "$cleanup", cleanup);

  Replaceall(f->code, "$symname", iname);
  Wrapper_print(f, f_wrappers);
  DelWrapper(f);

  /* Every overload is callable on its own: C# picks it when its signature is unique */
  if (overloaded) {
    String *ename = NewStringf("swigcs_%s%s", iname, Getattr(n, "sym:overname"));
    exportEntry(ename, overname);
    Setattr(n, "dotnet:entry", ename);
    Delete(ename);
  }

  if (last_overload) {
    dispatchFunction(n);
  } else if (!overloaded) {
    String *ename = NewStringf("swigcs_%s", iname);
    exportEntry(ename, wname);
    Setattr(n, "dotnet:entry", ename);
    emitCs(n, Char(ename));
    Delete(ename);
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

void DOTNET::dispatchFunction(Node *n) {
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

  Printf(f->def, "SWIGINTERN int %s(int resc, SwigRV *resv[], int argc, SwigRV *argv[]) {", wname);
  Printv(f->code, dispatch, "\n", NIL);

  if (customdoc)
    Printf(f->code, "std::string type_name = SWIG_Dotnet_ArgsTypeDesc(argc, argv);\n");
  Printf(f->code, "SWIG_Error_Format(SWIG_RuntimeError, \"No matching function for overload function '%s'.\"\n", iname);
  if (customdoc) {
    Printf(f->code, "   \"  Possible prototypes are:\\n\"\n%s\n     \"  You have: %%s\", type_name.c_str());\n", protoTypes);
  } else {
    Printf(f->code, "   \"  Possible C/C++ prototypes are:\\n\"%s);\n", protoTypes);
  }
  Printf(f->code, "return 1;\n");
  Printv(f->code, "}\n", NIL);

  Wrapper_print(f, f_wrappers);
  String *ename = NewStringf("swigcs_%s", iname);
  exportEntry(ename, wname);
  emitCs(n, Char(ename));
  Delete(ename);
  DelWrapper(f);
  Delete(dispatch);
  Delete(wname);
  Delete(protoTypes);
}

/* ------------------------------------------------------------
 * Variables: a C# property from the getter and setter
 * ------------------------------------------------------------ */

int DOTNET::variableWrapper(Node *n) {
  CallKind old = cur_kind;
  String *old_name = cur_name;
  if (cur_kind != CK_VAR) {
    cur_kind = CK_VAR;
    cur_name = Getattr(n, "sym:name");
  }
  int r = Language::variableWrapper(n);
  cur_kind = old;
  cur_name = old_name;
  return r;
}

/* ------------------------------------------------------------
 * Enums: track values so they become C# constants
 * ------------------------------------------------------------ */

int DOTNET::enumDeclaration(Node *n) {
  enum_next = 0;
  return Language::enumDeclaration(n);
}

int DOTNET::enumvalueDeclaration(Node *n) {
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
 * constantWrapper(): a C# const when known here, else a getter
 * ------------------------------------------------------------ */

int DOTNET::constantWrapper(Node *n) {
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
    String *ename = NewStringf("swigcs_%s", cname);
    exportEntry(ename, wname);
    std::string cs = csMember(Char(symname));
    std::map<std::string, long long>::iterator ev = enum_values.find(Char(symname));
    std::string out = csType(type, true);
    if (ev != enum_values.end()) {
      Printf(f_cs_globals, "  public const long %s = %lld;\n", cs.c_str(), ev->second);
    } else if (!out.empty()) {
      Printf(f_cs_globals, "  public static %s %s => SwigRT.Call1<%s>(SwigNative.%s, 1);\n", out.c_str(), cs.c_str(), out.c_str(), ename);
    }
    Delete(ename);
    Delete(wname);
    Delete(cname);
  } else {
    Swig_warning(WARN_TYPEMAP_CONST_UNDEF, input_file, line_number, "Unsupported constant value.\n");
    return SWIG_NOWRAP;
  }
  return SWIG_OK;
}

/* ------------------------------------------------------------
 * Directors: virtual methods call the C# object (a SWIG_RV_FUN value)
 * ------------------------------------------------------------ */

static SwigType *rvPointerType() {
  SwigType *t = NewString("SwigRV");
  SwigType_add_pointer(t);
  return t;
}

int DOTNET::classDirectorInit(Node *n) {
  String *declaration = Swig_director_declaration(n);
  Printf(f_directors_h, "\n%s\npublic:\n", declaration);
  Delete(declaration);
  return Language::classDirectorInit(n);
}

int DOTNET::classDirectorEnd(Node *n) {
  Printf(f_directors_h, "};\n\n");
  return Language::classDirectorEnd(n);
}

int DOTNET::classDirectorConstructor(Node *n) {
  Node *parent = Getattr(n, "parentNode");
  String *decl = Getattr(n, "decl");
  String *supername = Swig_class_name(parent);
  String *classname = NewStringf("SwigDirector_%s", supername);
  ParmList *superparms = Getattr(n, "parms");
  if (superparms && Getattr(superparms, "name") && Cmp(Getattr(superparms, "name"), "self") == 0 && Strstr(Getattr(superparms, "type"), "SwigRV"))
    superparms = nextSibling(superparms);
  ParmList *parms = CopyParmList(superparms);
  Parm *p = NewParm(rvPointerType(), NewString("self"), n);
  set_nextSibling(p, parms);
  parms = p;
  if (!Getattr(n, "defaultargs")) {
    Wrapper *w = NewWrapper();
    String *basetype = Getattr(parent, "classtype");
    String *target = Swig_method_decl(0, decl, classname, parms, 0);
    String *call = Swig_csuperclass_call(0, basetype, superparms);
    Printf(w->def, "%s::%s: %s, Swig::Director(self) {\n}\n", classname, target, call);
    Wrapper_print(w, f_directors);
    Delete(target);
    Delete(call);
    DelWrapper(w);
    target = Swig_method_decl(0, decl, classname, parms, 1);
    Printf(f_directors_h, "    %s;\n", target);
    Delete(target);
  }
  Delete(classname);
  Delete(supername);
  Delete(parms);
  return Language::classDirectorConstructor(n);
}

int DOTNET::classDirectorDefaultConstructor(Node *n) {
  String *classname = Swig_class_name(n);
  Printf(f_directors, "SwigDirector_%s::SwigDirector_%s(SwigRV *self) : Swig::Director(self) {\n}\n", classname, classname);
  Printf(f_directors_h, "    SwigDirector_%s(SwigRV *self);\n", classname);
  Delete(classname);
  return Language::classDirectorDefaultConstructor(n);
}

int DOTNET::classDirectorDisown(Node *n) {
  (void)n;
  return SWIG_OK;
}

int DOTNET::classDirectorMethod(Node *n, Node *parent, String *super) {
  String *decl = Getattr(n, "decl");
  String *name = Getattr(n, "name");
  String *classname = Getattr(parent, "sym:name");
  String *c_classname = Getattr(parent, "name");
  String *symname = Getattr(n, "sym:name");
  ParmList *l = Getattr(n, "parms");
  String *returntype = Getattr(n, "type");
  bool pure_virtual = Cmp(Getattr(n, "storage"), "virtual") == 0 && Cmp(Getattr(n, "value"), "0") == 0;
  bool ignored_method = GetFlag(n, "feature:ignore") ? true : false;
  bool is_void = !Cmp(returntype, "void") && !SwigType_ispointer_return(decl);

  Wrapper *w = NewWrapper();
  String *pclassname = NewStringf("SwigDirector_%s", classname);
  String *qualified_name = NewStringf("%s::%s", pclassname, name);
  SwigType *rtype = Getattr(n, "conversion_operator") ? 0 : Getattr(n, "classDirectorMethods:type");
  String *target = Swig_method_decl(rtype, decl, qualified_name, l, 0);
  Printf(w->def, "%s {", target);
  Delete(target);
  target = Swig_method_decl(rtype, decl, name, l, 1);
  String *declaration = NewStringf("    virtual %s;\n", target);
  Delete(target);

  String *super_call = Swig_method_call(super, l);
  String *base_code = NewString("");
  if (pure_virtual)
    Printf(base_code, "Swig::DirectorPureVirtualException::raise(\"Attempted to invoke pure virtual method %s::%s\");\n", SwigType_namestr(c_classname), SwigType_namestr(name));
  else
    Printf(base_code, is_void ? "%s;\nreturn;\n" : "return %s;\n", super_call);

  int status = SWIG_OK;
  if (ignored_method) {
    Printv(w->code, base_code, NIL);
  } else {
    if (!is_void) {
      String *cres = SwigType_lstr(returntype, "c_result");
      Printf(w->code, "%s;\n", cres);
      Delete(cres);
    }
    Swig_director_parms_fixup(l);
    Swig_typemap_attach_parms("in", l, 0);
    Swig_typemap_attach_parms("directorin", l, w);
    String *wrap_args = NewString("");
    String *arglist = NewString("");
    int idx = 0;
    for (Parm *p = l; p;) {
      if (checkAttribute(p, "tmap:in:numinputs", "0")) {
	p = Getattr(p, "tmap:in:next");
	continue;
      }
      String *tm = Getattr(p, "tmap:directorin");
      if (!tm) {
	Swig_warning(WARN_TYPEMAP_DIRECTORIN_UNDEF, input_file, line_number, "Unable to use type %s as a function argument in director method %s::%s (not overridable from C#).\n",
		     SwigType_str(Getattr(p, "type"), 0), SwigType_namestr(c_classname), SwigType_namestr(name));
	status = SWIG_NOWRAP;
	break;
      }
      char source[32];
      sprintf(source, "obj%d", idx++);
      tm = Copy(tm);
      Replaceall(tm, "$input", source);
      Replaceall(tm, "$owner", "0");
      Printf(wrap_args, "SwigRV *%s = 0;\n%s\n", source, tm);
      Printf(arglist, "%s%s", Len(arglist) ? ", " : "", source);
      Delete(tm);
      p = Getattr(p, "tmap:directorin:next");
    }
    String *out_code = NewString("");
    if (status == SWIG_OK && !is_void) {
      String *tm = Swig_typemap_lookup("directorout", n, "c_result", w);
      if (!tm) {
	Swig_warning(WARN_TYPEMAP_DIRECTOROUT_UNDEF, input_file, line_number, "Unable to use return type %s in director method %s::%s (not overridable from C#).\n",
		     SwigType_str(returntype, 0), SwigType_namestr(c_classname), SwigType_namestr(name));
	status = SWIG_NOWRAP;
      } else {
	Replaceall(tm, "$input", "swig_upres");
	Replaceall(tm, "$result", "c_result");
	Printv(out_code, tm, "\n", NIL);
      }
    }
    if (status == SWIG_OK) {
      Printv(w->code, wrap_args, NIL);
      if (idx)
	Printf(w->code, "SwigRV *swig_argv[%d] = {%s};\n", idx, arglist);
      else
	Printf(w->code, "SwigRV **swig_argv = 0;\n");
      Printf(w->code, "Swig::UpcallGuard swig_guard(swig_get_self());\n");
      Printf(w->code, "SwigRV *swig_upres = Swig::upcall(swig_get_self(), \"%s\", %d, swig_argv);\n", symname ? symname : name, idx);
      Printf(w->code, "if (swig_upres->kind == SWIG_RV_BASE) {\ndelete swig_upres;\n%s}\n", base_code);
      Printf(w->code, "if (swig_upres->kind == SWIG_RV_ERR) {\nstd::string swig_msg = swig_upres->str;\ndelete swig_upres;\nthrow Swig::DirectorMethodException(swig_msg);\n}\n");
      Printf(w->code, "try {\n%s} catch (...) {\ndelete swig_upres;\nthrow;\n}\n", out_code);
      Printf(w->code, "delete swig_upres;\n");
      if (!is_void) {
	String *rettype = SwigType_str(returntype, 0);
	Printf(w->code, "return (%s) c_result;\n", rettype);
	Delete(rettype);
      }
    }
    Delete(wrap_args);
    Delete(arglist);
    Delete(out_code);
  }
  if (status != SWIG_OK) {
    DelWrapper(w);
    Delete(declaration);
    Delete(base_code);
    Delete(super_call);
    Delete(pclassname);
    Delete(qualified_name);
    return SWIG_OK;
  }
  Append(w->code, "}\n");
  Replaceall(w->code, "$symname", symname);
  Wrapper_print(w, f_directors);
  Printv(f_directors_h, declaration, NIL);
  DelWrapper(w);
  Delete(declaration);
  Delete(base_code);
  Delete(super_call);
  Delete(pclassname);
  Delete(qualified_name);
  return SWIG_OK;
}

/* ------------------------------------------------------------
 * Handlers: record what kind of function is being wrapped
 * ------------------------------------------------------------ */

int DOTNET::globalfunctionHandler(Node *n) {
  if (cur_kind != CK_STATIC) {
    cur_kind = CK_GLOBAL;
    cur_name = Getattr(n, "sym:name");
  }
  return Language::globalfunctionHandler(n);
}

int DOTNET::memberfunctionHandler(Node *n) {
  cur_kind = CK_MEMBER;
  cur_name = Getattr(n, "sym:name");
  int r = Language::memberfunctionHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

int DOTNET::staticmemberfunctionHandler(Node *n) {
  cur_kind = CK_STATIC;
  cur_name = Getattr(n, "sym:name");
  int r = Language::staticmemberfunctionHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

int DOTNET::constructorHandler(Node *n) {
  cur_kind = CK_CTOR;
  cur_name = Getattr(n, "sym:name");
  if (Swig_directorclass(n)) {
    /* Hidden first argument: the C# object implementing the virtual methods */
    Parm *self = NewParm(rvPointerType(), NewString("self"), n);
    set_nextSibling(self, Getattr(n, "parms"));
    Setattr(n, "parms", self);
    Setattr(n, "wrap:self", "1");
    Delete(self);
  }
  int r = Language::constructorHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

int DOTNET::destructorHandler(Node *n) {
  return Language::destructorHandler(n);
}

int DOTNET::membervariableHandler(Node *n) {
  cur_kind = CK_VAR;
  cur_name = Getattr(n, "sym:name");
  int r = Language::membervariableHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

int DOTNET::staticmembervariableHandler(Node *n) {
  cur_kind = CK_VAR;
  cur_name = Getattr(n, "sym:name");
  int r = Language::staticmembervariableHandler(n);
  cur_kind = CK_GLOBAL;
  return r;
}

/* ------------------------------------------------------------
 * C# types of C++ types
 * ------------------------------------------------------------ */

SwigType *DOTNET::bareType(SwigType *t) {
  SwigType *s = Copy(t);
  if (SwigType_isreference(s)) SwigType_del_reference(s);
  if (SwigType_isrvalue_reference(s)) SwigType_del_rvalue_reference(s);
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

String *DOTNET::lookupTypemap(const char *method, SwigType *t) {
  Parm *p = NewParmWithoutFileLineInfo(t, "x");
  String *tm = Swig_typemap_lookup(method, p, "", 0);
  tm = tm ? Copy(tm) : 0;
  Delete(p);
  return tm;
}

static List *templateArgs(SwigType *r) {
  String *ta = SwigType_templateargs(r);
  String *inner = NewStringWithSize(Char(ta) + 1, Len(ta) - 2);
  List *args = SwigType_parmlist(inner);
  Delete(inner);
  Delete(ta);
  return args;
}

/* C# type of a C++ type (out: as returned), or "" if it has none */
std::string DOTNET::csType(SwigType *t, bool out) {
  SwigType *b = bareType(t);
  std::string r;
  String *tm = out ? lookupTypemap("csouttype", b) : 0;
  if (!tm) tm = lookupTypemap("cstype", b);
  if (tm) {
    r = Char(tm);
  } else {
    SwigType *rt = SwigType_typedef_resolve_all(b);
    if (SwigType_isenum(rt)) {
      r = "long";
    } else if (SwigType_istemplate(rt)) {
      String *prefix = SwigType_templateprefix(rt);
      List *args = templateArgs(rt);
      if (Strcmp(prefix, "std::vector") == 0 && Len(args) >= 1) {
	std::string e = csType(Getitem(args, 0), out);
	if (!e.empty()) r = (out ? "System.Collections.Generic.List<" : "System.Collections.Generic.IEnumerable<") + e + ">";
      } else if (Strcmp(prefix, "std::map") == 0 && Len(args) >= 2) {
	std::string k = csType(Getitem(args, 0), out), v = csType(Getitem(args, 1), out);
	if (k == "string" && !v.empty()) r = (out ? "System.Collections.Generic.Dictionary<string, " : "System.Collections.Generic.IDictionary<string, ") + v + ">";
      } else if (Strcmp(prefix, "std::pair") == 0 && Len(args) == 2) {
	std::string a = csType(Getitem(args, 0), out), c = csType(Getitem(args, 1), out);
	if (!a.empty() && !c.empty()) r = "(" + a + ", " + c + ")";
      }
      Delete(prefix);
      Delete(args);
    }
    if (r.empty()) {
      Node *cls = classLookup(b);
      if (cls && Len(Getattr(cls, "sym:name")) && !GetFlag(cls, "feature:ignore")) r = Char(Getattr(cls, "sym:name"));
    }
    Delete(rt);
  }
  Delete(tm);
  Delete(b);
  return r;
}

/* ------------------------------------------------------------
 * C# methods of an overload set
 * ------------------------------------------------------------ */

/* Inputs, outputs and entry of an overload; false if a type has no C# mapping */
bool DOTNET::overloadInfo(Node *n, Overload &o) {
  o.n = n;
  o.req = 0;
  o.nin = 0;
  o.self_director = false;
  String *e = Getattr(n, "dotnet:entry");
  o.entry = e ? Char(e) : "";
  ParmList *l = Getattr(n, "wrap:parms");
  bool skip_self = cur_kind == CK_MEMBER || (cur_kind == CK_VAR && l && Getattr(l, "self"));
  if (cur_kind == CK_CTOR && Getattr(n, "wrap:self")) {
    skip_self = true;
    o.self_director = true;
  }
  bool defaults = false;
  std::set<std::string> used;
  int idx = 0;
  for (Parm *p = l; p; p = nextSibling(p)) {
    if (skip_self) {
      skip_self = false;
      continue;
    }
    if (Getattr(p, "tmap:argout")) {
      std::string r = csType(Getattr(p, "type"), true);
      if (r.empty()) {
	fail_type = Char(SwigType_str(Getattr(p, "type"), 0));
	return false;
      }
      o.outs.push_back(r);
    }
    if (checkAttribute(p, "tmap:in:numinputs", "0")) continue;
    std::string a = csType(Getattr(p, "type"), false);
    if (a.empty()) {
      fail_type = Char(SwigType_str(Getattr(p, "type"), 0));
      return false;
    }
    o.types.push_back(a);
    String *pn = Getattr(p, "name");
    std::string nm = pn && Len(pn) ? Char(pn) : "";
    for (size_t i = 0; i < nm.size(); ++i) if (!isalnum((unsigned char)nm[i]) && nm[i] != '_') nm[i] = '_';
    if (nm.empty() || nm == "self" || used.count(nm)) {
      char b[16];
      sprintf(b, "a%d", idx);
      nm = b;
    }
    used.insert(nm);
    o.names.push_back(csIdent(nm));
    if (Getattr(p, "value")) defaults = true;
    if (!defaults) o.req++;
    o.nin++;
    idx++;
  }
  SwigType *rt = Getattr(n, "type");
  std::string ret;
  if (cur_kind != CK_CTOR && rt && SwigType_type(rt) != T_VOID) {
    ret = csType(rt, true);
    if (ret.empty()) {
      fail_type = Char(SwigType_str(rt, 0));
      return false;
    }
  }
  if (!ret.empty()) o.outs.insert(o.outs.begin(), ret);
  return true;
}

static std::string joinTypes(const std::vector<std::string> &v, size_t k) {
  std::string s;
  for (size_t i = 0; i < k && i < v.size(); ++i) s += (i ? "," : "") + v[i];
  return s;
}

static std::string retType(const std::vector<std::string> &outs) {
  if (outs.empty()) return "void";
  if (outs.size() == 1) return outs[0];
  std::string s = "(";
  for (size_t i = 0; i < outs.size(); ++i) s += (i ? ", " : "") + outs[i];
  return s + ")";
}

/* Statement returning the outputs of a call (call is an expression giving IntPtr[]) */
static std::string returnCall(const std::vector<std::string> &outs, const std::string &entry, const std::string &args) {
  char nout[16];
  sprintf(nout, "%d", (int)outs.size());
  if (outs.empty()) return "SwigRT.CallVoid(SwigNative." + entry + ", 1" + args + ");";
  if (outs.size() == 1) return "return SwigRT.Call1<" + outs[0] + ">(SwigNative." + entry + ", " + nout + args + ");";
  std::string s = "var _r = SwigRT.Call(SwigNative." + entry + ", " + nout + args + ");\n";
  s += "    try {\n      return (";
  for (size_t i = 0; i < outs.size(); ++i) {
    char b[16];
    sprintf(b, "%d", (int)i);
    s += std::string(i ? ", " : "") + "SwigRT.Take<" + outs[i] + ">(_r, " + b + ")";
  }
  return s + ");\n    } finally {\n      SwigRT.Release(_r);\n    }";
}

static std::string xmlEscape(const std::string &s) {
  std::string r;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '<') r += "&lt;";
    else if (s[i] == '>') r += "&gt;";
    else if (s[i] == '&') r += "&amp;";
    else r += s[i];
  }
  return r;
}

/* XML doc comment of an overload set (%feature("customdoc") or "docstring") */
std::string DOTNET::docComment(Node *n, const std::string &indent) {
  String *doc = 0;
  if (checkAttribute(n, "feature:customdoc", "1")) {
    Node *head = Getattr(n, "sym:overloaded") ? Getattr(n, "sym:overloaded") : n;
    doc = Swig_document_function(head);
  } else if (Getattr(n, "feature:docstring")) {
    doc = Copy(Getattr(n, "feature:docstring"));
  }
  if (!doc || Len(doc) == 0) return "";
  std::string s = xmlEscape(Char(doc));
  Delete(doc);
  std::string r = indent + "/// <summary>\n";
  size_t a = 0;
  while (a <= s.size()) {
    size_t b = s.find('\n', a);
    if (b == std::string::npos) b = s.size();
    std::string line = s.substr(a, b - a);
    r += indent + "/// " + line + "\n";
    a = b + 1;
  }
  return r + indent + "/// </summary>\n";
}

void DOTNET::addMember(const Member &m) {
  if (class_node && cur_kind != CK_GLOBAL) class_members[class_node].push_back(m);
  else global_members.push_back(m);
}

void DOTNET::emitCs(Node *n, const std::string &dispatcher) {
  if (Cmp(Getattr(n, "nodeType"), "destructor") == 0) {
    class_deleter[class_node] = true;
    return;
  }
  /* Overloads in the order the C++ dispatcher tries them */
  std::vector<Node *> nodes;
  if (Getattr(n, "sym:overloaded")) {
    List *ranked = Swig_overload_rank(n, false);
    for (int i = 0; ranked && i < Len(ranked); ++i) nodes.push_back((Node *)Getitem(ranked, i));
    Delete(ranked);
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
      skipped += "  // no C# mapping for " + fail_type + ": " + std::string(Char(decl)) + "\n";
      Delete(decl);
    }
  }
  bool member = cur_kind == CK_MEMBER;
  bool in_class = class_node && cur_kind != CK_GLOBAL;
  std::string cname = class_name ? Char(class_name) : "";
  std::string base = cur_name ? Char(cur_name) : "";

  /* Properties: data members, and %feature("dotnet:property") getters */
  if (cur_kind == CK_VAR) {
    if (ovs.empty()) return;
    Overload &o = ovs[0];
    bool setter = GetFlag(n, "memberset") || GetFlag(n, "varset");
    ParmList *l = Getattr(n, "wrap:parms");
    bool is_static = !(l && Getattr(l, "self"));
    std::string self = is_static ? "" : ", this";
    Prop &pr = class_props[in_class ? class_node : 0][csMember(base)];
    pr.is_static = is_static;
    if (setter) {
      if (o.types.empty()) return;
      pr.type = o.types[0];
      pr.set = "SwigRT.CallVoid(SwigNative." + o.entry + ", 1" + self + ", value);";
    } else {
      if (o.outs.empty()) return;
      pr.type = o.outs[0];
      pr.get = "return SwigRT.Call1<" + o.outs[0] + ">(SwigNative." + o.entry + ", 1" + self + ");";
    }
    return;
  }

  std::string csname;
  bool named_ctor = false;
  if (cur_kind == CK_CTOR) {
    named_ctor = base != cname;
    csname = named_ctor ? csMember(base) : cname;
  } else {
    csname = csMember(base);
    if (in_class && csname == cname) csname += "_";
  }

  /* One entry per (static-ness, C# parameter types): arities from the required ones on */
  struct Group {
    std::string key;
    size_t k;
    std::vector<Overload *> ovs;
  };
  std::vector<Group> groups;
  for (size_t i = 0; i < ovs.size(); ++i) {
    for (int k = ovs[i].req; k <= ovs[i].nin; ++k) {
      std::string key = joinTypes(ovs[i].types, k);
      size_t g = 0;
      while (g < groups.size() && !(groups[g].key == key && groups[g].k == (size_t)k)) ++g;
      if (g == groups.size()) {
	Group ng;
	ng.key = key;
	ng.k = k;
	groups.push_back(ng);
      }
      groups[g].ovs.push_back(&ovs[i]);
    }
  }

  std::string code = skipped;
  std::string indent = "  ";
  std::string dkey = cname + "|" + csname;
  if (!documented.count(dkey) && !groups.empty()) {
    documented.insert(dkey);
    code += docComment(n, indent);
  }
  bool prop = GetFlag(n, "feature:dotnet:property") && !named_ctor && cur_kind != CK_CTOR;
  for (size_t g = 0; g < groups.size(); ++g) {
    Group &gr = groups[g];
    Overload *o0 = gr.ovs[0];
    bool merged = gr.ovs.size() > 1;
    std::string entry = merged || o0->entry.empty() ? dispatcher : o0->entry;
    /* Outputs: those of the overload, or of all merged ones when they agree */
    std::vector<std::string> outs = o0->outs;
    for (size_t i = 1; i < gr.ovs.size(); ++i) {
      if (gr.ovs[i]->outs != outs) {
	size_t m = std::max(outs.size(), gr.ovs[i]->outs.size());
	outs = std::vector<std::string>(m, "object");
	if (m == 1) outs[0] = "object";
      }
    }
    std::string params, args;
    if (member) args = ", this";
    if (o0->self_director) args += ", new SwigRT.DirectorSelf(this, typeof(" + cname + "))";
    for (size_t a = 0; a < gr.k; ++a) {
      params += std::string(a ? ", " : "") + o0->types[a] + " " + o0->names[a];
      args += ", " + o0->names[a];
    }
    Member m;
    m.name = csname;
    char kbuf[16];
    sprintf(kbuf, "/%d", (int)gr.k);
    m.key = (cur_kind == CK_CTOR ? "new:" : csname + ":") + gr.key + kbuf;
    m.inherit = cur_kind != CK_CTOR;
    std::string body;
    if (cur_kind == CK_CTOR && !named_ctor) {
      body = indent + "public " + cname + "(" + params + ") : base(SwigRT.Owned.Pending) {\n" +
	indent + "  SwigRT.Construct(this, SwigNative." + entry + args + ");\n" + indent + "}\n";
      m.inherit = false;
    } else if (cur_kind == CK_CTOR) {
      body = indent + "public static " + cname + " " + csname + "(" + params + ") {\n" +
	indent + "  var _o = new " + cname + "(SwigRT.Owned.Pending);\n" +
	indent + "  SwigRT.Construct(_o, SwigNative." + entry + args + ");\n" + indent + "  return _o;\n" + indent + "}\n";
    } else if (prop && gr.k == 0 && outs.size() == 1) {
      std::string mod = member ? "public " : "public static ";
      body = indent + mod + outs[0] + " " + csname + " => SwigRT.Call1<" + outs[0] + ">(SwigNative." + entry + ", 1" + args + ");\n";
    } else {
      std::string mod = member ? "public " : "public static ";
      if (member && is_member_director(o0->n) && !merged) mod += "virtual ";
      std::string rc = returnCall(outs, entry, args);
      body = indent + mod + retType(outs) + " " + csname + "(" + params + ") {\n" + indent + "  " + rc + "\n" + indent + "}\n";
    }
    m.code = body;
    if (g == 0) m.code = code + body;
    addMember(m);
  }
  if (groups.empty() && !skipped.empty()) {
    Member m;
    m.name = csname;
    m.key = "";
    m.code = skipped;
    m.inherit = false;
    addMember(m);
  }
}

/* ------------------------------------------------------------
 * Classes
 * ------------------------------------------------------------ */

bool DOTNET::hasMembers(Node *n, std::set<Node *> &seen) {
  if (!seen.insert(n).second) return false;
  if (!class_members[n].empty() || !class_props[n].empty() || !class_proxy[n].empty()) return true;
  List *bases = Getattr(n, "bases");
  for (int b = 0; bases && b < Len(bases); ++b)
    if (hasMembers(Getitem(bases, b), seen)) return true;
  return false;
}

/* The base a proxy extends: the wrapped base with the most members (own and inherited);
   mixin bases without members lose to a real base wherever they are listed */
Node *DOTNET::primaryBase(Node *n) {
  List *bases = Getattr(n, "bases");
  Node *best = 0;
  size_t best_count = 0;
  for (int b = 0; bases && b < Len(bases); ++b) {
    Node *base = Getitem(bases, b);
    if (!Getattr(base, "dotnet:proxy")) continue;
    std::set<Node *> seen;
    size_t c = memberCount(base, seen);
    if (!best || c > best_count) {
      best = base;
      best_count = c;
    }
  }
  return best;
}

size_t DOTNET::memberCount(Node *n, std::set<Node *> &seen) {
  if (!seen.insert(n).second) return 0;
  size_t c = class_members[n].size() + class_props[n].size() + (class_proxy[n].empty() ? 0 : 1);
  List *bases = Getattr(n, "bases");
  for (int b = 0; bases && b < Len(bases); ++b) c += memberCount(Getitem(bases, b), seen);
  return c;
}

int DOTNET::classHandler(Node *n) {
  String *old_class_name = class_name;
  Node *old_class_node = class_node;
  bool old_has_destructor = class_has_destructor;
  String *old_proxycode = class_proxycode;
  class_name = Getattr(n, "sym:name");
  class_node = n;
  class_has_destructor = false;
  class_proxycode = NewString("");
  Setattr(n, "dotnet:proxy", "1");

  Language::classHandler(n);

  class_proxy[n] = Char(class_proxycode);
  class_order.push_back(n);

  Delete(class_proxycode);
  class_name = old_class_name;
  class_node = old_class_node;
  class_has_destructor = old_has_destructor;
  class_proxycode = old_proxycode;
  return SWIG_OK;
}

void DOTNET::emitClass(Node *n) {
  String *cname = Getattr(n, "sym:name");
  SwigType *t = Copy(Getattr(n, "name"));
  SwigType_add_pointer(t);
  SwigType_remember(t);
  String *mangled = SwigType_manglestr(t);
  Node *pb = primaryBase(n);
  std::string base = pb ? Char(Getattr(pb, "sym:name")) : "SwigObject";

  String *c = f_cs_classes;
  Printf(c, "public partial class %s : %s {\n", cname, base.c_str());
  Printf(c, "  static System.IntPtr swigType_;\n");
  Printf(c, "  internal override System.IntPtr SwigTypeInfo => swigType_ != System.IntPtr.Zero ? swigType_ : (swigType_ = SwigRT.TypeQuery(\"%s\"));\n", mangled);
  if (class_deleter[n]) {
    Printf(c, "  static readonly SwigRT.WrapFn swigDeleter_ = SwigNative.swigcs_delete_%s;\n", cname);
    Printf(c, "  internal override SwigRT.WrapFn SwigDeleter => swigDeleter_;\n");
  } else {
    Printf(c, "  internal override SwigRT.WrapFn SwigDeleter => null;\n");
  }
  Printf(c, "  internal %s(SwigRT.Owned o) : base(o) {}\n", cname);
  Printf(f_cs_register, "    SwigRT.Register(\"%s\", o => new %s(o));\n", mangled, cname);

  /* Own members, then those of further bases not hidden (the primary base's are inherited) */
  std::set<std::string> names, keys;
  std::set<Node *> inherited;
  for (Node *b = pb; b; b = primaryBase(b)) inherited.insert(b);
  std::vector<Member> &own = class_members[n];
  for (size_t i = 0; i < own.size(); ++i) {
    names.insert(own[i].name);
    if (!own[i].key.empty() && !keys.insert(own[i].key).second) continue;
    Printf(c, "%s", own[i].code.c_str());
  }
  std::map<std::string, Prop> props = class_props[n];
  std::vector<Node *> queue(1, n);
  std::set<Node *> seen(queue.begin(), queue.end());
  for (size_t q = 0; q < queue.size(); ++q) {
    List *bases = Getattr(queue[q], "bases");
    for (int b = 0; bases && b < Len(bases); ++b) {
      Node *bn = Getitem(bases, b);
      if (!seen.insert(bn).second) continue;
      queue.push_back(bn);
      if (inherited.count(bn)) continue;
      std::vector<Member> &inh = class_members[bn];
      std::set<std::string> bnames;
      for (size_t i = 0; i < inh.size(); ++i) {
	if (!inh[i].inherit || names.count(inh[i].name)) continue;
	bnames.insert(inh[i].name);
	if (!inh[i].key.empty() && !keys.insert(inh[i].key).second) continue;
	Printf(c, "%s", inh[i].code.c_str());
      }
      names.insert(bnames.begin(), bnames.end());
      std::map<std::string, Prop> &bp = class_props[bn];
      for (std::map<std::string, Prop>::iterator it = bp.begin(); it != bp.end(); ++it)
	if (!props.count(it->first) && !names.count(it->first)) props[it->first] = it->second;
    }
  }
  for (std::map<std::string, Prop>::iterator it = props.begin(); it != props.end(); ++it) {
    Prop &p = it->second;
    if (p.type.empty() || p.get.empty()) continue;
    Printf(c, "  public %s%s %s {\n    get { %s }\n", p.is_static ? "static " : "", p.type.c_str(), it->first.c_str(), p.get.c_str());
    if (!p.set.empty()) Printf(c, "    set { %s }\n", p.set.c_str());
    Printf(c, "  }\n");
  }
  Printf(c, "%s", class_proxy[n].c_str());
  Printf(c, "}\n\n");
  Delete(mangled);
  Delete(t);
}

extern "C" Language *swig_dotnet(void) {
  return new DOTNET();
}
