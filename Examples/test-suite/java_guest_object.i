%module(directors="1") java_guest_object

/* Guest-object mode (javaguest.swg) and Cleaner lifetime (javacleaner.swg): typemaps
   converting Java objects in C++, overloads merged after erasure, jstype widen variants,
   directors with guest values from C++ threads, customdoc as Javadoc. */

%include <javaguest.swg>
%include <javacleaner.swg>

%feature("java:typebound", "Cloneable");
%feature("customdoc", "1");
%feature("customdoc:arg:self", "self");
%feature("customdoc:proto:constructor", "$name($in)");
%feature("customdoc:proto:single_out", "$name($in) -> $out");
%feature("customdoc:proto:normal", "$name($in) -> ($out)");
%feature("customdoc:proto:void", "$name($in)");
%feature("customdoc:arg:normal", "$type $name");
%feature("customdoc:arg:only", "$type $name");
%feature("customdoc:arg:only:out", "$type");
%feature("customdoc:arg:separator", ", ");
%feature("customdoc:main", "  $brief\n\n$overview\n$main");
%feature("docstring") first "First element of a list <T> & co.";

%{
#include <vector>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>
%}

%typemap(javainterfaces) Vec, Pt "AutoCloseable, Cloneable"
%immutable Bag::what;

%inline %{
struct Vec {
  std::vector<double> v;
  Vec() {}
  Vec(double x) : v(1, x) {}
  double get(int i) const { return v.at(i); }
  int size() const { return static_cast<int>(v.size()); }
};
struct Pt {
  double x, y;
  Pt() : x(0), y(0) {}
  Pt(double x, double y) : x(x), y(y) {}
};
struct Base {
  int tag;
  Base() : tag(42) {}
};
struct Derived : Base {
  virtual ~Derived() {}
  virtual int id() const { return 7; }
};
%}

%fragment("conv", "header", fragment=SWIG_AsVal_frag(double)) {
SWIGINTERN bool to_vec(jobject o, Vec *out) {
  void *p = 0;
  if (SWIG_IsOK(SWIG_ConvertPtr(o, &p, SWIGTYPE_p_Vec, SWIG_POINTER_NO_NULL))) { if (out) *out = *static_cast<Vec*>(p); return true; }
  double d;
  if (SWIG_IsOK(SWIG_AsVal(double)(o, &d))) { if (out) *out = Vec(d); return true; }
  return false;
}
SWIGINTERN bool to_pt(jobject o, Pt *out) {
  void *p = 0;
  if (SWIG_IsOK(SWIG_ConvertPtr(o, &p, SWIGTYPE_p_Pt, SWIG_POINTER_NO_NULL))) { if (out) *out = *static_cast<Pt*>(p); return true; }
  Vec v;
  if (to_vec(o, &v) && v.size() >= 1) { if (out) *out = Pt(v.get(0), v.size() > 1 ? v.get(1) : 0); return true; }
  return false;
}
template<typename T> bool to_list(jobject o, std::vector<T> *out, bool (*conv)(jobject, T*)) {
  if (!SWIG_JavaGuestIsList(o)) return false;
  jint n = SWIG_JavaGuestListSize(o);
  if (out) out->resize(n);
  for (jint i = 0; i < n; ++i) {
    jobject e = SWIG_JavaGuestListGet(o, i);
    bool ok = conv(e, out ? &(*out)[i] : 0);
    SWIG_JavaGuestRelease(e);
    if (!ok) return false;
  }
  return true;
}
}

%define %guest_type(T, CONV, PREC, WIDEN)
%typemap(jni) const T&, T "jobject"
%typemap(jtype) const T&, T "Object"
%typemap(jstype, widen=WIDEN) const T&, T "T"
%typemap(javain) const T& "$javainput"
%typemap(javaout) T { return $imclassname.swigCast($jnicall); }
%typemap(in, fragment="conv") const T& (T tmp) {
  if (!CONV($input, &tmp)) SWIG_exception_fail(SWIG_TypeError, "expected " #T);
  $1 = &tmp;
}
%typemap(typecheck, precedence=PREC, fragment="conv") const T& { $1 = CONV($input, 0); }
%typemap(out) T { $result = SWIG_NewPointerObj(new T($1), $descriptor(T*), SWIG_POINTER_OWN); }

%typemap(jni) const std::vector<T>& "jobject"
%typemap(jtype) const std::vector<T>& "Object"
%typemap(jstype) const std::vector<T>& "java.util.List<T>"
%typemap(javain) const std::vector<T>& "$javainput"
%typemap(in, fragment="conv") const std::vector<T>& (std::vector<T> tmp) {
  if (!to_list<T>($input, &tmp, CONV)) SWIG_exception_fail(SWIG_TypeError, "expected List<" #T ">");
  $1 = &tmp;
}
%typemap(typecheck, precedence=PREC, fragment="conv") const std::vector<T>& { $1 = to_list<T>($input, 0, CONV); }
%enddef
%guest_type(Vec, to_vec, 500, "double")
%typemap(directorin, descriptor="L$packagepath/Vec;") Vec ""
%typemap(directorout) Vec { if (!to_vec($input, &$result)) SWIG_exception_fail(SWIG_TypeError, "visit must return a Vec"); }
%typemap(javadirectorout) Vec "$javacall"
%typemap(directorin, descriptor="Ljava/util/List;") const std::vector<Vec>& {
  $input = SWIG_JavaGuestListNew((jint)$1.size());
  for (size_t i = 0; i < $1.size(); ++i)
    SWIG_JavaGuestListAppend($input, SWIG_NewPointerObj(new Vec($1[i]), SWIGTYPE_p_Vec, SWIG_POINTER_OWN));
}
%typemap(javadirectorin) const std::vector<Vec>& "$imclassname.swigCast($jniinput)"
%guest_type(Pt, to_pt, 510, "Vec,double")

%typemap(jni) const Base& "jobject"
%typemap(jtype) const Base& "Object"
%typemap(jstype) const Base& "Base"
%typemap(javain) const Base& "$javainput"
%typemap(in) const Base& {
  void *p = 0;
  if (!SWIG_IsOK(SWIG_ConvertPtr($input, &p, SWIGTYPE_p_Base, SWIG_POINTER_NO_NULL))) SWIG_exception_fail(SWIG_TypeError, "expected Base");
  $1 = static_cast<Base*>(p);
}

%typemap(jni) std::string "jobject"
%typemap(jtype) std::string "Object"
%typemap(jstype) std::string "String"
%typemap(javaout) std::string { return (String) $jnicall; }
%typemap(out) std::string { $result = SWIG_JavaGuestNewString($1); }

%inline %{
Vec twice(const Vec& x) { Vec r = x; for (auto& e : r.v) e *= 2; return r; }
double total(const std::vector<Vec>& xs) { double s = 0; for (auto& x : xs) for (double e : x.v) s += e; return s; }
int tag_of(const Base& b) { return b.tag; }
std::string greet() { return "h\xc3\xa9llo \xf0\x9f\x98\x80"; }
void boom() { throw std::runtime_error("boom from C++"); }

// Erasure collisions: List<Vec> vs List<Pt>
std::string kind(const std::vector<Vec>&) { return "vec"; }
std::string kind(const std::vector<Pt>&) { return "pt"; }
Vec first(const std::vector<Vec>& v) { return v.at(0); }
Pt first(const std::vector<Pt>& v) { return v.at(0); }
// Widening ties: (double,double) from both families, Vec family has lower precedence
std::string join(const Vec&, const Vec&) { return "vec"; }
std::string join(const Pt&, const Pt&) { return "pt"; }

struct Bag {
  std::string what;
  Bag(const std::vector<Vec>&) : what("vec") {}
  Bag(const std::vector<Pt>&) : what("pt") {}
  std::string count(const std::vector<Vec>& v) const { return "vec" + std::to_string(v.size()); }
  std::string count(const std::vector<Pt>& v) const { return "pt" + std::to_string(v.size()); }
};
%}

%feature("director") Visitor;
%inline %{
struct Visitor {
  virtual ~Visitor() {}
  virtual Vec visit(const std::vector<Vec>& xs) { return Vec(-1); }
};
Vec run_visitor(Visitor& v, const std::vector<Vec>& xs) { return v.visit(xs); }
double run_threaded(Visitor& v, int nthreads, int rounds) {
  std::atomic<long> total(0);
  std::vector<std::thread> ts;
  for (int t = 0; t < nthreads; ++t)
    ts.emplace_back([&, t]() {
      for (int r = 0; r < rounds; ++r) {
        std::vector<Vec> xs(1, Vec(t + 1));
        total += (long)v.visit(xs).get(0);
      }
    });
  for (auto& th : ts) th.join();
  return (double)total;
}
%}

%{
static int guest_init_ran = 0;
%}
%init %{
  guest_init_ran = 1;
%}
%inline %{
int init_ran() { return guest_init_ran; }
%}
