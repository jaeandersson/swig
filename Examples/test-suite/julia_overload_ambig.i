/* Overloads with complementary partially-typed params must not produce
   mutually ambiguous Julia methods (Julia multiple dispatch).  Mirrors the
   downstream extract_parametric / dplesol shapes: a wrapped class type and an
   "Any"-typed (composite by-value) argument land in complementary positions.
   A call whose argument types intersect both signatures, with no more-specific
   method covering that intersection, is a genuine Julia ambiguity. */
%module julia_overload_ambig

%inline %{
class A { public: int v; A(): v(0) {} };
class C { public: int v; C(): v(0) {} };
struct Bag { int n; };   /* stands in for a composite by-value arg */
%}

/* Route Bag across the boundary as a native Julia value (jlparam "Any"),
   exactly like the downstream std::vector<MatType> arguments. */
%typemap(ctype)    Bag, const Bag& "void*"
%typemap(jltype)   Bag, const Bag& "Any"
%typemap(jlparam)  Bag, const Bag& "Any"
%typemap(in)       const Bag& "{ static Bag _swig_bag; $1 = &_swig_bag; }"
%typemap(typecheck, precedence=2000) const Bag& "$1 = 1;"

%inline %{
/* f: (Bag,A) and (A,Bag), each with a trailing default.  -> (Any,A) and
   (A,Any).  A call f(A, A) intersects both and no method covers it: ambiguous.
   (No (A,A) overload exists, unlike the simpler case, so it cannot resolve.) */
int f(const Bag& a, const A& b, int opts = 0) { (void)a;(void)b; return 2+opts; }
int f(const A& a, const Bag& b, int opts = 0) { (void)a;(void)b; return 3+opts; }

/* g: (C,C,Bag) vs (Bag,Bag,C) + trailing default.  -> (C,C,Any) and
   (Any,Any,C).  Mutually ambiguous at arity 3 and 4. */
int g(const C& a, const C& b, const Bag& c, int opts = 0)   { (void)a;(void)b;(void)c; return 10+opts; }
int g(const Bag& a, const Bag& b, const C& c, int opts = 0) { (void)a;(void)b;(void)c; return 20+opts; }

/* h: two overloads merged into one method, with DIFFERENT parameter NAMES in
   the wildcard slots (mirrors the casadi Function ctor).  Slot 1 is (A vs Bag)
   -> Any; the second overload calls its first arg `name` (not `obj`).  The
   merged can-probe must use the merged positional names, not the per-overload
   stale names, or the generated Julia raises UndefVarError on the folded
   branch. */
int h(const A& obj, const Bag& tail) { (void)obj;(void)tail; return 30; }
int h(const Bag& name, const A& tail) { (void)name;(void)tail; return 40; }
int h(const C& thing, const Bag& tail) { (void)thing;(void)tail; return 50; }
%}
