/* -----------------------------------------------------------------------------
 * julia.cxx
 *
 * Julia language module for SWIG.  Emits a flat extern-"C" C++ wrapper
 * (consumed via ccall) plus a Julia module source file (<module>.jl) with
 * proxy types, finalizers and typed methods.  Overloads map onto Julia's
 * multiple dispatch; overloads sharing a Julia signature are resolved by a
 * generated C dispatcher running their typecheck typemaps in SWIG's order.
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
  String *f_directors;    /* SwigDirector_<C> method bodies */
  String *f_directors_h;  /* SwigDirector_<C> class decls */

  String *module_name;
  String *f_type_init;    /* body of _swig_jl_init_types() */
  String *f_proxy_pairs;  /* ("Name", Name), entries for _proxy_types */
  Hash   *jl_methods;     /* "fname|sig-types" -> List of Hash{fname,sig,body,node,...} */
  List   *jl_method_order;
  Hash   *class_statics;  /* class jlname -> Hash of static fnames */
  Hash   *class_members;  /* class jlname -> List of method-line templates (@SELF@) */
  Hash   *class_methods;  /* class jlname -> Hash of instance method fname -> "1" */
  Hash   *class_bases;    /* class jlname -> List of base jlnames */
  String *class_jlname;   /* sym:name of class being processed, 0 outside */
  String *bare_symname;   /* unqualified member name captured pre-transform */
  String *memberget_field; /* data member whose getter is being wrapped */
  Hash   *enum_seen;      /* @enum type names already emitted (dedup) */
  Hash   *director_classes; /* C++ classname -> "1" for director-enabled classes */
  Hash   *jl_docs;        /* Julia fname -> List of documentation sections (customdoc) */
  List   *jl_doc_order;
  bool    in_ctor, in_static;
  bool    class_has_copyctor;  /* a copy ctor was wrapped for the current class */
  bool    class_has_refs;      /* the current proxy owns Julia objects its C++ object borrows */
  Hash   *refs_classes;        /* proxies with a _swig_refs field */
  Hash   *overload_rank;       /* wrap:name -> position in Swig_overload_rank of its set */
  Hash   *jl_protos;           /* overload set -> name of its prototypes constant */
  String *f_jl_protos;         /* those constants */
  String *f_dispatch;          /* C dispatchers of merged overloads */
  int     n_dispatch;
  int     n_skipped;

public:
  JULIA() : f_begin(0), f_runtime(0), f_header(0), f_wrappers(0), f_init(0),
            f_jl_types(0), f_jl_body(0), f_jl_exports(0), f_directors(0), f_directors_h(0), module_name(0),
            f_type_init(0), f_proxy_pairs(0), jl_methods(0), jl_method_order(0), class_statics(0), class_members(0), class_methods(0), class_bases(0), class_jlname(0),
            bare_symname(0), memberget_field(0), enum_seen(0), director_classes(0), jl_docs(0), jl_doc_order(0), in_ctor(false), in_static(false), class_has_copyctor(false), class_has_refs(false), refs_classes(0), overload_rank(0), jl_protos(0), f_jl_protos(0), f_dispatch(0), n_dispatch(0), n_skipped(0) {}

  virtual void main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SWIG_library_directory("julia");
    Preprocessor_define("SWIGJULIA 1", 0);
    SWIG_config_file("julia.swg");
    allow_overloading();
    directorLanguage();  /* enable %feature("director"); gated per-module in top() */
    /* normal ctors always build the plain class; director subclassing goes
       through a dedicated _swig_new_SwigDirector_<C> wrapper (emitted in
       classDirectorConstructor) so the leading jl_self arg stays explicit. */
    Delete(none_comparison);
    none_comparison = NewString("0");
  }

  /* eval/include are auto-bound in every Julia module and cannot be
     redefined or extended: such method names get a trailing underscore */
  static String *jl_method_name(String *name) {
    if (Strcmp(name, "eval") == 0 || Strcmp(name, "include") == 0) return NewStringf("%s_", name);
    return Copy(name);
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

  /* split a Julia method signature ("a::A, b::Any, opts::Integer=Dict()") into
     per-parameter (name, type, hasdefault) triples.  Splitting on top-level
     commas only -- a type annotation may itself contain commas (e.g.
     Union{Integer,Enum}, AbstractVector{<:Real}).  When `defs` is non-null it
     receives each slot's default literal ("" when none). */
  /* Replace whole-identifier occurrences of `from` in s by `to`; a match right
     after '.' is a field access (x.ptr), not a variable, and is left alone. */
  static void rename_ident(String *s, String *from, String *to) {
    String *out = NewString("");
    const char *p = Char(s), *f = Char(from);
    size_t fl = strlen(f);
    if (fl == 0) return;
    for (size_t i = 0; p[i];) {
      bool idprev = i > 0 && (isalnum((unsigned char)p[i - 1]) || p[i - 1] == '_' || p[i - 1] == '.');
      if (!idprev && strncmp(p + i, f, fl) == 0 && !(isalnum((unsigned char)p[i + fl]) || p[i + fl] == '_')) {
        Append(out, to);
        i += fl;
      } else {
        Putc(p[i], out);
        ++i;
      }
    }
    Clear(s);
    Append(s, out);
    Delete(out);
  }

  void split_signature(String *sig, List *names, List *types, List *hasdef, List *defs = 0) {
    const char *s = Char(sig);
    int depth = 0, start = 0, i = 0;
    for (;; ++i) {
      char c = s[i];
      if (c == '{' || c == '(') depth++;
      else if (c == '}' || c == ')') depth--;
      else if ((c == ',' && depth == 0) || c == '\0') {
        if (i > start) {
          String *part = NewStringWithSize(s + start, i - start);
          String *t = part; while (*Char(t) == ' ') { String *t2 = NewString(Char(t) + 1); if (t != part) Delete(t); t = t2; }
          const char *cc = Strstr(t, "::");
          String *nm = cc ? NewStringWithSize(Char(t), (int)(cc - Char(t))) : Copy(t);
          Replaceall(nm, " ", "");
          String *ty = cc ? NewString(cc + 2) : NewString("Any");
          const char *eq = Strstr(ty, "=");
          int hd = eq ? 1 : 0;
          String *dv = eq ? NewString(eq + 1) : NewString("");
          if (eq) { String *t2 = NewStringWithSize(Char(ty), (int)(eq - Char(ty))); Delete(ty); ty = t2; }
          Append(names, nm); Append(types, ty); Append(hasdef, NewStringf("%d", hd));
          if (defs) Append(defs, dv); else Delete(dv);
          if (t != part) Delete(t);
          Delete(part);
        }
        start = i + 1;
        if (c == '\0') break;
      }
    }
  }

  /* a "wildcard" Julia parameter type imposes no dispatch constraint: a call
     with a concrete value still matches it.  Two overloads that disagree only
     by having a concrete type where the other has a wildcard, in complementary
     positions, are mutually ambiguous under multiple dispatch. */
  bool is_wildcard_type(String *t) {
    return Strcmp(t, "Any") == 0;
  }

  /* same-arity signatures are mutually ambiguous when, position by position,
     every slot is "compatible" (equal, or one side wildcard) AND each side is
     strictly more specific than the other in at least one slot.  Then neither
     subsumes the other and a call hitting their intersection is ambiguous. */
  /* same arity and identical type in every slot */
  bool sigs_equal(List *ta, List *tb) {
    if (Len(ta) != Len(tb)) return false;
    for (int i = 0; i < Len(ta); ++i)
      if (Strcmp((String *)Getitem(ta, i), (String *)Getitem(tb, i)) != 0) return false;
    return true;
  }

  bool sigs_ambiguous(List *ta, List *tb) {
    if (Len(ta) != Len(tb)) return false;
    bool a_tighter = false, b_tighter = false;
    for (int i = 0; i < Len(ta); ++i) {
      String *x = (String *)Getitem(ta, i), *y = (String *)Getitem(tb, i);
      if (Strcmp(x, y) == 0) continue;
      bool xw = is_wildcard_type(x), yw = is_wildcard_type(y);
      if (xw && !yw) b_tighter = true;        /* b constrains slot i, a does not */
      else if (yw && !xw) a_tighter = true;   /* a constrains slot i, b does not */
      else return false;                      /* two distinct concrete types: incomparable */
    }
    return a_tighter && b_tighter;
  }

  /* same arity and no slot holds two distinct concrete types (so generalising
     the disagreeing slots to Any yields a single well-defined signature) */
  bool sigs_compatible(List *ta, List *tb) {
    if (Len(ta) != Len(tb)) return false;
    for (int i = 0; i < Len(ta); ++i) {
      String *x = (String *)Getitem(ta, i), *y = (String *)Getitem(tb, i);
      if (Strcmp(x, y) == 0) continue;
      if (!is_wildcard_type(x) && !is_wildcard_type(y)) return false;
    }
    return true;
  }

  /* does any slot impose no dispatch constraint? */
  bool has_wildcard(List *t) {
    for (int i = 0; i < Len(t); ++i) if (is_wildcard_type((String *)Getitem(t, i))) return true;
    return false;
  }

  /* rebuild jl_methods / jl_method_order so each collected method with trailing
     default args becomes one entry per reachable arity (default-free). */
  void expand_default_arg_arities() {
    if (!jl_method_order) return;
    Hash *nm = NewHash(); List *norder = NewList();
    for (int oi = 0; oi < Len(jl_method_order); ++oi) {
      List *lst = (List *)Getattr(jl_methods, (String *)Getitem(jl_method_order, oi));
      for (int ei = 0; lst && ei < Len(lst); ++ei) {
        Hash *e = (Hash *)Getitem(lst, ei);
        List *names = NewList(), *types = NewList(), *hd = NewList(), *defs = NewList();
        split_signature(Getattr(e, "sig"), names, types, hd, defs);
        int total = Len(names), req = total;
        for (int i = total - 1; i >= 0; --i) {
          if (Strcmp((String *)Getitem(hd, i), "1") == 0) req = i; else break;
        }
        for (int a = req; a <= total; ++a) {
          /* signature for arity a: first a params, defaults stripped */
          String *sig = NewString("");
          String *stypes = NewString("");
          for (int i = 0; i < a; ++i) {
            if (i > 0) Printf(sig, ", ");
            Printf(sig, "%s::%s", (String *)Getitem(names, i), (String *)Getitem(types, i));
            Printf(stypes, "%s,", (String *)Getitem(types, i));
          }
          /* body/preserve: splice dropped trailing params back as
             their default literals so the C wrapper still gets all arguments. */
          String *body = Copy(Getattr(e, "body"));
          String *pres = Copy(Getattr(e, "preserve"));
          for (int i = a; i < total; ++i) {
            String *pn = (String *)Getitem(names, i), *dv = (String *)Getitem(defs, i);
            rename_ident(body, pn, dv);
            /* a dropped param can't still be GC-preserved (it has no binding) */
            String *pp = NewStringf("%s ", pn); Replaceall(pres, pp, ""); Delete(pp);
          }
          Hash *ne = NewHash();
          Setattr(ne, "fname", Copy(Getattr(e, "fname")));
          Setattr(ne, "sig", sig);
          Setattr(ne, "body", body);
          Setattr(ne, "preserve", pres);
          Setattr(ne, "node", Getattr(e, "node"));
          if (a < total) Setattr(ne, "defaulted", "1");
          if (Getattr(e, "partial")) Setattr(ne, "partial", NewStringWithSize(Char(Getattr(e, "partial")), a < Len(Getattr(e, "partial")) ? a : Len(Getattr(e, "partial"))));
          if (Getattr(e, "dispname")) Setattr(ne, "dispname", Copy(Getattr(e, "dispname")));
          String *key = NewStringf("%s|%s", Getattr(e, "fname"), stypes);
          List *nl = (List *)Getattr(nm, key);
          if (!nl) { nl = NewList(); Setattr(nm, key, nl); Append(norder, Copy(key)); }
          Append(nl, ne);
          Delete(key); Delete(stypes);
        }
        Delete(names); Delete(types); Delete(hd); Delete(defs);
      }
    }
    Delete(jl_methods); Delete(jl_method_order);
    jl_methods = nm; jl_method_order = norder;
  }

  /* Emit Julia methods for the collected groups.  Overloads that share an
     exact typed signature (several C++ overloads collapsed to Any params), and
     overloads whose Julia signatures would be MUTUALLY AMBIGUOUS under multiple
     dispatch (a concrete type and an Any wildcard in complementary positions),
     are merged into one method whose ambiguous slots are generalised to Any and
     whose correct C wrapper is selected at runtime by a C dispatcher.
     `methods`/`order` are mutated (consumed groups emptied). */
  void emit_method_groups(File *jf, Hash *methods, List *order) {
    if (!order) return;
    List *out = NewList();   /* per Julia method: Hash{entries, fallback} */
    for (int oi = 0; oi < Len(order); ++oi) {
      String *key = (String *)Getitem(order, oi);
      List *lst = (List *)Getattr(methods, key);
      if (!lst || Len(lst) == 0) continue;  /* claimed by an earlier merge */
      /* Merge overloads whose Julia signatures would collide under multiple
         dispatch.  Only signatures with a wildcard (Any) slot can collide --
         a wildcard fails to separate them, and a sibling overload with a
         concrete type in that slot, plus a wildcard elsewhere, is mutually
         ambiguous.  Seed a cluster from a wildcard-bearing group and fold in
         every same-fname, same-arity, compatible group that also bears a
         wildcard, generalising disagreeing slots to Any.  Fully concrete
         groups (e.g. (MX,MX)) are never merged: distinct concretes can't
         collide, and Julia resolves them against the generalised method by
         specificity, so they keep their fast direct method.  Iterating to a
         fixpoint collapses (MX,Any),(Any,MX),(DM,Any),(Any,DM),(Any,Any),...
         -- regardless of declaration order -- into one method. */
      {
        String *fname0 = Getattr((Hash *)Getitem(lst, 0), "fname");
        List *members = NewList();  /* per cluster-group: its split type list */
        List *gen = NewList();
        {
          List *n0 = NewList(), *t0 = NewList(), *d0 = NewList();
          split_signature(Getattr((Hash *)Getitem(lst, 0), "sig"), n0, t0, d0);
          Append(members, t0);
          for (int pi = 0; pi < Len(t0); ++pi) Append(gen, Copy((String *)Getitem(t0, pi)));
          Delete(n0); Delete(d0);
        }
        {
          /* Phase A -- ambiguity-transitive closure.  Fold a later group if it
             is MUTUALLY AMBIGUOUS (complementary wildcards) with any member
             already in the cluster.  Comparing against members (not the
             widened gen) keeps the chain connected: (Any,MX) bridges (MX,Any)
             to (DM,Any) even after gen has become (Any,Any).  A fully concrete
             group like (MX,MX) is ambiguous with none, so it stays out. */
          bool changed = true;
          while (changed) {
            changed = false;
            for (int oj = oi + 1; oj < Len(order); ++oj) {
              String *k2 = (String *)Getitem(order, oj);
              List *l2 = (List *)Getattr(methods, k2);
              if (!l2 || Len(l2) == 0) continue;
              Hash *r2 = (Hash *)Getitem(l2, 0);
              if (Strcmp(fname0, Getattr(r2, "fname")) != 0) continue;
              List *nb = NewList(), *tb = NewList(), *db = NewList();
              split_signature(Getattr(r2, "sig"), nb, tb, db);
              bool amb = false;
              for (int mi = 0; mi < Len(members) && !amb; ++mi)
                if (sigs_ambiguous((List *)Getitem(members, mi), tb)) amb = true;
              if (amb) {
                for (int z = 0; z < Len(l2); ++z) Append(lst, Getitem(l2, z));
                Append(members, Copy(tb));
                for (int pi = 0; pi < Len(gen); ++pi)  /* widen gen by this group */
                  if (Strcmp((String *)Getitem(gen, pi), (String *)Getitem(tb, pi)) != 0)
                    Setitem(gen, pi, NewString("Any"));
                Setattr(methods, k2, NewList());  /* mark consumed */
                changed = true;
              }
              Delete(nb); Delete(tb); Delete(db);
            }
          }
          /* Phase B -- absorb any remaining group whose signature already
             equals the cluster's generalised signature (e.g. an all-Any
             (vector,vector) overload).  It would otherwise re-define the
             merged method.  Only meaningful once gen carries a wildcard. */
          if (has_wildcard(gen)) {
            changed = true;
            while (changed) {
              changed = false;
              for (int oj = oi + 1; oj < Len(order); ++oj) {
                String *k2 = (String *)Getitem(order, oj);
                List *l2 = (List *)Getattr(methods, k2);
                if (!l2 || Len(l2) == 0) continue;
                Hash *r2 = (Hash *)Getitem(l2, 0);
                if (Strcmp(fname0, Getattr(r2, "fname")) != 0) continue;
                List *nb = NewList(), *tb = NewList(), *db = NewList();
                split_signature(Getattr(r2, "sig"), nb, tb, db);
                if (sigs_equal(gen, tb)) {
                  for (int z = 0; z < Len(l2); ++z) Append(lst, Getitem(l2, z));
                  Append(members, Copy(tb));
                  Setattr(methods, k2, NewList());  /* mark consumed */
                  changed = true;
                }
                Delete(nb); Delete(tb); Delete(db);
              }
            }
          }
          for (int mi = 0; mi < Len(members); ++mi) Delete(Getitem(members, mi));
          Delete(members);
        }
        Delete(gen);
      }
      Hash *m = NewHash();
      Setattr(m, "entries", Copy(lst));
      Append(out, m);
    }
    add_fallbacks(out);
    sort_by_overload_rank(out);
    for (int i = 0; i < Len(out); ++i) {
      Hash *m = (Hash *)Getitem(out, i);
      List *lst = (List *)Getattr(m, "entries");
      if (Len(lst) == 1 && !Getattr(m, "fallback")) {
        Hash *e = (Hash *)Getitem(lst, 0);
        String *pv = Getattr(e, "preserve");
        if (Len(pv) > 0)
          Printf(jf, "%s(%s) = GC.@preserve %sbegin %s end\n",
                 Getattr(e, "fname"), Getattr(e, "sig"), pv, Getattr(e, "body"));
        else
          Printf(jf, "%s(%s) = %s\n", Getattr(e, "fname"), Getattr(e, "sig"), Getattr(e, "body"));
      } else {
        emit_dispatch_group(jf, lst, Getattr(m, "fallback") != 0);
      }
    }
    Delete(out);
  }

  /* per slot the type all entries share, else Any (also where partial, with partial_any) */
  List *common_types(List *lst, bool partial_any) {
    List *res = 0;
    for (int ei = 0; ei < Len(lst); ++ei) {
      Hash *e = (Hash *)Getitem(lst, ei);
      List *ne = NewList(), *te = NewList(), *de = NewList();
      split_signature(Getattr(e, "sig"), ne, te, de);
      if (!res) { res = te; te = 0; }
      else
        for (int pi = 0; pi < Len(res) && pi < Len(te); ++pi)
          if (Strcmp((String *)Getitem(res, pi), (String *)Getitem(te, pi)) != 0) Setitem(res, pi, NewString("Any"));
      String *pf = Getattr(e, "partial");
      for (int pi = 0; partial_any && pf && pi < Len(res) && pi < Len(pf); ++pi)
        if (Char(pf)[pi] == '1') Setitem(res, pi, NewString("Any"));
      Delete(ne); Delete(te); Delete(de);
    }
    return res;
  }

  /* per name and arity with a partial jlparam: an Any-typed method dispatching the rest at runtime */
  void add_fallbacks(List *out) {
    Hash *fams = NewHash();
    List *forder = NewList();
    Hash *partial = NewHash();
    for (int i = 0; i < Len(out); ++i) {
      List *lst = (List *)Getattr((Hash *)Getitem(out, i), "entries");
      for (int ei = 0; ei < Len(lst); ++ei) {
        Hash *e = (Hash *)Getitem(lst, ei);
        List *ne = NewList(), *te = NewList(), *de = NewList();
        split_signature(Getattr(e, "sig"), ne, te, de);
        /* a static method's ::Type{C} slot keeps it apart from same-named functions */
        bool st = Len(ne) > 0 && Len((String *)Getitem(ne, 0)) == 0;
        String *fk = NewStringf("%s|%d|%s", Getattr(e, "fname"), Len(te), st ? (String *)Getitem(te, 0) : "");
        List *fam = (List *)Getattr(fams, fk);
        if (!fam) { fam = NewList(); Setattr(fams, fk, fam); Append(forder, Copy(fk)); }
        Append(fam, e);
        if (Getattr(e, "partial") && Strchr(Getattr(e, "partial"), '1')) Setattr(partial, fk, "1");
        Delete(fk); Delete(ne); Delete(te); Delete(de);
      }
    }
    for (int fi = 0; fi < Len(forder); ++fi) {
      if (!Getattr(partial, (String *)Getitem(forder, fi))) continue;
      List *fam = (List *)Getattr(fams, (String *)Getitem(forder, fi));
      List *ft = common_types(fam, true);
      Hash *fb = NewHash();
      Setattr(fb, "entries", Copy(fam));
      Setattr(fb, "fallback", "1");
      bool merged = false;
      for (int i = 0; i < Len(out) && !merged; ++i) {  /* a method of that signature joins it */
        List *ol = (List *)Getattr((Hash *)Getitem(out, i), "entries");
        List *ot = common_types(ol, false);
        if (Strcmp(Getattr((Hash *)Getitem(ol, 0), "fname"), Getattr((Hash *)Getitem(fam, 0), "fname")) == 0
            && sigs_equal(ot, ft)) {
          Setitem(out, i, fb);
          merged = true;
        }
        Delete(ot);
      }
      if (!merged) Append(out, fb);
      Delete(ft);
    }
    Delete(fams); Delete(forder); Delete(partial);
  }

  /* emission order is SWIG's overload order: Julia's tie-break on overlaps keeps the first */
  void sort_by_overload_rank(List *out) {
    Hash *fidx = NewHash(), *sidx = NewHash();
    List *sorted = NewList();
    for (int i = 0; i < Len(out); ++i) {
      Hash *m = (Hash *)Getitem(out, i);
      List *lst = (List *)Getattr(m, "entries");
      long best = -1;
      for (int ei = 0; ei < Len(lst); ++ei) {
        Hash *e = (Hash *)Getitem(lst, ei);
        Node *n = Getattr(e, "node");
        String *fn = Getattr(e, "fname");
        if (!Getattr(fidx, fn)) Setattr(fidx, fn, NewStringf("%d", Len(fidx)));
        String *hk = NewStringf("%s|%p", fn, Getattr(n, "sym:overloaded") ? Getattr(n, "sym:overloaded") : n);
        if (!Getattr(sidx, hk)) Setattr(sidx, hk, NewStringf("%d", Len(sidx)));
        long k = atol(Char(Getattr(fidx, fn))) * 100000000L + atol(Char(Getattr(sidx, hk))) * 100000L + overload_pos(n);
        if (best < 0 || k < best) best = k;
        Delete(hk);
      }
      String *ks = NewStringf("%ld", best);
      Setattr(m, "jl:order", ks);
      int pos = Len(sorted);
      while (pos > 0 && atol(Char(Getattr((Hash *)Getitem(sorted, pos - 1), "jl:order"))) > best) --pos;
      Insert(sorted, pos, m);
    }
    Clear(out);
    for (int i = 0; i < Len(sorted); ++i) Append(out, Getitem(sorted, i));
    Delete(sorted); Delete(fidx); Delete(sidx);
  }

  /* position of n in its overload set as Swig_overload_rank orders it (cached per set) */
  int overload_pos(Node *n) {
    Node *head = Getattr(n, "sym:overloaded");
    String *wn = Getattr(n, "wrap:name");
    if (!head || !wn) return 0;
    if (!overload_rank) overload_rank = NewHash();
    if (!Getattr(overload_rank, wn)) {
      List *ranked = Swig_overload_rank(head, true);
      for (int i = 0; ranked && i < Len(ranked); ++i)
        Setattr(overload_rank, Getattr((Node *)Getitem(ranked, i), "wrap:name"), NewStringf("%d", i));
      Delete(ranked);
      if (!Getattr(overload_rank, wn)) Setattr(overload_rank, wn, "99999");
    }
    return atoi(Char(Getattr(overload_rank, wn)));
  }

  /* Julia string constant listing the prototypes of n's overload set, emitted once per set */
  String *prototypes_const(Node *n) {
    Node *head = Getattr(n, "sym:overloaded");
    if (!head) head = n;
    String *key = NewStringf("%p", head);
    if (!jl_protos) { jl_protos = NewHash(); f_jl_protos = NewString(""); }
    String *cname = Getattr(jl_protos, key);
    if (!cname) {
      String *txt = NewString("");
      if (checkAttribute(head, "feature:customdoc", "1")) {
        /* Swig_prototypes_error yields one C string literal per prototype */
        String *lits = Swig_prototypes_error(head);
        List *lines = Split(lits, '\n', -1);
        for (int i = 0; i < Len(lines); ++i) {
          String *l = (String *)Getitem(lines, i);
          const char *q = strchr(Char(l), '"');
          const char *e = strstr(Char(l), "\\n\"");
          if (q && e && e > q) Printf(txt, "%s\n", NewStringWithSize(q + 1, (int)(e - q - 1)));
        }
        Delete(lines); Delete(lits);
      } else {
        for (Node *c = head; c; c = Getattr(c, "sym:nextSibling")) {
          if (!Getattr(c, "wrap:name")) continue;
          String *decl = Swig_name_decl(c);
          Printf(txt, "    %s\n", decl);
          Delete(decl);
        }
      }
      Replaceall(txt, "\\", "\\\\");
      Replaceall(txt, "$", "\\$");
      Replaceall(txt, "\"", "\\\"");
      cname = NewStringf("_swig_protos_%d", Len(jl_protos) + 1);
      Printf(f_jl_protos, "const %s = \"%s\"\n", cname, txt);
      Setattr(jl_protos, key, cname);
      Delete(txt);
    }
    Delete(key);
    return cname;
  }

  /* one Julia method whose C dispatcher runs the overloads' typechecks in SWIG's order */
  void emit_dispatch_group(File *jf, List *lst, bool fallback) {
    {  /* sets as first met, those needing defaults last; each in Swig_overload_rank order */
      Hash *sets = NewHash(), *exact = NewHash();
      for (int i = 0; i < Len(lst); ++i) {
        Hash *e = (Hash *)Getitem(lst, i);
        Node *n = Getattr(e, "node");
        String *hk = NewStringf("%p", Getattr(n, "sym:overloaded") ? Getattr(n, "sym:overloaded") : n);
        if (!Getattr(sets, hk)) Setattr(sets, hk, NewStringf("%d", Len(sets)));
        if (!Getattr(e, "defaulted")) Setattr(exact, hk, "1");
        Setattr(e, "jl:set", hk);
        Delete(hk);
      }
      List *sorted = NewList();
      for (int i = 0; i < Len(lst); ++i) {
        Hash *e = (Hash *)Getitem(lst, i);
        String *hk = Getattr(e, "jl:set");
        int key = (Getattr(exact, hk) ? 0 : 100000000) + atoi(Char(Getattr(sets, hk))) * 100000 + overload_pos(Getattr(e, "node"));
        Setattr(e, "jl:order", NewStringf("%d", key));
        int pos = Len(sorted);
        while (pos > 0 && atoi(Char(Getattr((Hash *)Getitem(sorted, pos - 1), "jl:order"))) > key) --pos;
        Insert(sorted, pos, e);
      }
      Clear(lst);
      for (int i = 0; i < Len(sorted); ++i) Append(lst, Getitem(sorted, i));
      Delete(sorted); Delete(sets); Delete(exact);
    }
    Hash *e0 = (Hash *)Getitem(lst, 0);
    /* outer signature: a slot the entries disagree on is generalised to Any */
    List *names0 = NewList(), *types0 = NewList(), *def0 = NewList();
    split_signature(Getattr(e0, "sig"), names0, types0, def0);
    List *outtypes = common_types(lst, fallback);
    /* no trailing defaults: each default-arg arity is its own merged method */
    String *osig = NewString("");
    String *dargs = NewString("");    /* the arguments, boxed for the C dispatcher */
    String *dtypes = NewString("");
    String *dparms = NewString("");
    List *dslot = NewList();          /* outer Julia type of each boxed argument */
    int nargs = 0;
    for (int pi = 0; pi < Len(names0); ++pi) {
      String *nm = (String *)Getitem(names0, pi);
      if (pi > 0) Printf(osig, ", ");
      Printf(osig, "%s::%s", nm, (String *)Getitem(outtypes, pi));
      if (Len(nm) == 0) continue;     /* ::Type{C} of a static method */
      Printf(dargs, "%s, ", nm);
      Printf(dtypes, "Any, ");
      Printf(dparms, "%sjl_value_t* argv%d", nargs ? ", " : "", nargs);
      Append(dslot, Getitem(outtypes, pi));
      ++nargs;
    }
    String *dname = NewStringf("_swig_jl_dispatch_%d", ++n_dispatch);
    Printf(f_dispatch, "extern \"C\" SWIGEXPORT int %s(%s) {\n", dname, nargs ? Char(dparms) : "void");
    String *protos = NewString("");
    Hash *seenp = NewHash();
    Printf(jf, "function %s(%s)\n", Getattr(e0, "fname"), osig);
    Printf(jf, "    _swig_k = ccall((:%s, _lib), Cint, (%s), %s)\n", dname, dtypes, dargs);
    for (int ei = 0; ei < Len(lst); ++ei) {
      Hash *e = (Hash *)Getitem(lst, ei);
      Node *n = Getattr(e, "node");
      /* C side: the typecheck typemaps of the arguments given, in order */
      {
        Printf(f_dispatch, "  try {\n    int _v = 0; (void)_v;\n");
        int nb = 0, j = 0;
        for (Parm *p = Getattr(n, "wrap:parms"); p && j < nargs; ) {
          if (checkAttribute(p, "tmap:in:numinputs", "0")) { p = Getattr(p, "tmap:in:next"); continue; }
          String *tc = Getattr(p, "tmap:typecheck");
          bool typed_self = Getattr(p, "self") && !is_wildcard_type((String *)Getitem(dslot, j));
          if (tc && Len(tc) > 0 && !typed_self) {  /* Julia dispatch already typed self */
            String *b = Copy(tc);
            String *iv = NewStringf("argv%d", j);
            Replaceid(b, Getattr(p, "lname"), "_v");
            Replaceall(b, "$implicitconv", "0");
            Replaceall(b, "$input", iv);
            Replaceall(b, "\n", "\n    ");
            Printf(f_dispatch, "    %s\n    if (_v) {\n", b);
            Delete(b); Delete(iv);
            ++nb;
          }
          ++j;
          p = Getattr(p, "tmap:in:next") ? Getattr(p, "tmap:in:next") : nextSibling(p);
        }
        Printf(f_dispatch, "    return %d;\n", ei);
        for (; nb > 0; --nb) Printf(f_dispatch, "    }\n");
        Printf(f_dispatch, "  } catch (...) {}\n");
      }
      /* rename args to entry-0 names via placeholders, so (x,y)->(y,x) cannot chain */
      String *body = Copy(Getattr(e, "body"));
      String *pv = Copy(Getattr(e, "preserve"));
      {
        List *ne = NewList(), *te = NewList(), *de = NewList();
        split_signature(Getattr(e, "sig"), ne, te, de);
        String *parts[] = {body, pv};
        for (int pass = 0; pass < 2; ++pass) {
          for (int pi = 0; pi < Len(ne) && pi < Len(names0); ++pi) {
            String *nm = (String *)Getitem(ne, pi);
            if (Strcmp(nm, (String *)Getitem(names0, pi)) == 0) continue;
            String *tmp = NewStringf("_swigjl_arg%d_", pi);
            for (int k = 0; k < 2; ++k) {
              if (pass == 0) rename_ident(parts[k], nm, tmp);
              else rename_ident(parts[k], tmp, (String *)Getitem(names0, pi));
            }
            Delete(tmp);
          }
        }
        Delete(ne); Delete(te); Delete(de);
      }
      Printf(jf, "    %s _swig_k == %d\n", ei == 0 ? "if" : "elseif", ei);
      if (Len(pv) > 0)
        Printf(jf, "        return GC.@preserve %sbegin %s end\n", pv, body);
      else
        Printf(jf, "        return %s\n", body);
      Delete(body); Delete(pv);
      String *pc = prototypes_const(n);
      if (!Getattr(seenp, pc)) {
        Setattr(seenp, pc, "1");
        Printf(protos, "%s%s", Len(protos) ? " * " : "", pc);
      }
    }
    Printf(f_dispatch, "  return -1;\n}\n\n");
    Printf(jf, "    end\n    _swig_overload_error(\"%s\", %s, (%s))\nend\n",
           Getattr(e0, "dispname") ? Getattr(e0, "dispname") : Getattr(e0, "fname"), protos, dargs);
    Delete(names0); Delete(types0); Delete(def0); Delete(outtypes); Delete(osig);
    Delete(dargs); Delete(dtypes); Delete(dparms); Delete(dslot); Delete(dname); Delete(protos); Delete(seenp);
  }

  /* Collect class `cls`'s own instance-method names plus those inherited from
     all transitive bases into `out` (fname -> "1", or "property" for a member
     marked %feature("julia:property")).  The most derived class decides. */
  void collect_flattened_methods(String *cls, Hash *out, Hash *seen) {
    if (Getattr(seen, cls)) return;
    Setattr(seen, cls, "1");
    Hash *own = class_methods ? (Hash *)Getattr(class_methods, cls) : 0;
    if (own) {
      Iterator mi = First(own);
      while (mi.key) { if (!Getattr(out, mi.key)) Setattr(out, mi.key, mi.item); mi = Next(mi); }
    }
    List *bb = class_bases ? (List *)Getattr(class_bases, cls) : 0;
    if (bb) for (int i = 0; i < Len(bb); ++i) collect_flattened_methods((String *)Getitem(bb, i), out, seen);
  }

  /* Same as collect_flattened_methods but for STATIC method names, mirroring
     the base-class static forwarders so an inherited static is reachable as
     Derived.static(...) wherever Derived's getproperty is consulted. */
  void collect_flattened_statics(String *cls, Hash *out, Hash *seen) {
    if (Getattr(seen, cls)) return;
    Setattr(seen, cls, "1");
    Hash *own = class_statics ? (Hash *)Getattr(class_statics, cls) : 0;
    if (own) { Iterator si = First(own); while (si.key) { Setattr(out, si.key, "1"); si = Next(si); } }
    List *bb = class_bases ? (List *)Getattr(class_bases, cls) : 0;
    if (bb) for (int i = 0; i < Len(bb); ++i) collect_flattened_statics((String *)Getitem(bb, i), out, seen);
  }

  /* For every proxy class with at least one instance method (own or inherited),
     emit a `const Set{Symbol}` of its method names, plus a `Base.getproperty`
     that maps `obj.method(args...)` to `method(obj, args...)`.  Struct fields
     (`ptr`, ...) short-circuit to `getfield` FIRST and the function is `@inline`
     so generated-code `self.ptr` constant-folds to a plain field load. */
  void emit_method_style_getproperty(File *jf) {
    /* union of all classes that have own methods or that have bases */
    Hash *classes = NewHash();
    if (class_methods) { Iterator ci = First(class_methods); while (ci.key) { Setattr(classes, ci.key, "1"); ci = Next(ci); } }
    if (class_bases) { Iterator ci = First(class_bases); while (ci.key) { Setattr(classes, ci.key, "1"); ci = Next(ci); } }
    if (Len(classes) == 0) { Delete(classes); return; }
    Printf(jf, "\n# method-style property access: obj.method(args...) -> method(obj, args...)\n");
    Iterator ci = First(classes);
    while (ci.key) {
      String *cls = (String *)ci.key;
      Hash *flat = NewHash(); Hash *seen = NewHash();
      collect_flattened_methods(cls, flat, seen);
      Delete(seen);
      if (Len(flat) > 0 || Getattr(class_methods, cls)) {
        String *set = NewString("");
        String *props = NewString("");
        Iterator mi = First(flat);
        bool first = true;
        while (mi.key) {
          Printf(set, "%s:%s", first ? "" : ", ", mi.key); first = false;
          if (Strcmp(mi.item, "property") == 0) Printf(props, "%s:%s", Len(props) ? ", " : "", mi.key);
          mi = Next(mi);
        }
        Printf(jf, "const _swigjl_methods_%s = Set{Symbol}([%s])\n", cls, set);
        /* zero-argument members marked %feature("julia:property"): obj.name is
           the value itself (like a Python @property), not a bound method */
        String *propcheck = NewString("");
        if (Len(props) > 0) {
          Printf(jf, "const _swigjl_properties_%s = Set{Symbol}([%s])\n", cls, props);
          Printf(propcheck, "    s in _swigjl_properties_%s && return getfield(@__MODULE__, s)(x)\n", cls);
        }
        Printf(jf,
          "@inline function Base.getproperty(x::%s, s::Symbol)\n"
          "    s in fieldnames(%s) && return getfield(x, s)\n"
          "%s"
          "    if s in _swigjl_methods_%s\n"
          "        return _SwigBound(getfield(@__MODULE__, s), x)\n"
          "    end\n"
          "    return _swig_getproperty(x, Val(s))\n"
          "end\n",
          cls, cls, propcheck, cls);
        Delete(set); Delete(props); Delete(propcheck);
      }
      Delete(flat);
      ci = Next(ci);
    }
    Delete(classes);
  }

  /* For every proxy class with at least one static method (own or inherited),
     emit a `Base.getproperty(::Type{T}, s::Symbol)` mapping `T.static(args...)`
     to `static(T, args...)` (the `::Type{T}` dispatch form already emitted).
     A literal-`===` chain (not a Set/`in`) is used deliberately so the compiler
     CONST-FOLDS for literal symbols: `T.name`, `T.parameters` etc. still infer
     to the exact DataType field type with zero overhead.  Any name not matched
     falls through to `getfield(T, s)`, so DataType reflection/printing is
     unaffected.  Classes with zero statics emit nothing. */
  void emit_static_method_style_getproperty(File *jf) {
    Hash *classes = NewHash();
    if (class_statics) { Iterator ci = First(class_statics); while (ci.key) { Setattr(classes, ci.key, "1"); ci = Next(ci); } }
    if (class_bases) { Iterator ci = First(class_bases); while (ci.key) { Setattr(classes, ci.key, "1"); ci = Next(ci); } }
    if (Len(classes) == 0) { Delete(classes); return; }
    Printf(jf, "\n# static method-style access: T.static(args...) -> static(T, args...)\n");
    Iterator ci = First(classes);
    while (ci.key) {
      String *cls = (String *)ci.key;
      Hash *flat = NewHash(); Hash *seen = NewHash();
      collect_flattened_statics(cls, flat, seen);
      Delete(seen);
      if (Len(flat) > 0) {
        Printf(jf, "@inline function Base.getproperty(::Type{%s}, s::Symbol)\n", cls);
        Iterator si = First(flat);
        bool first = true;
        while (si.key) {
          Printf(jf,
            "    %s s === :%s\n"
            "        return _SwigBound(%s, %s)\n",
            first ? "if" : "elseif", si.key, si.key, cls);
          first = false;
          si = Next(si);
        }
        Printf(jf, "    end\n    return getfield(%s, s)\nend\n", cls);
      }
      Delete(flat);
      ci = Next(ci);
    }
    Delete(classes);
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
    f_proxy_pairs = NewString("");
    f_jl_types   = NewString("");
    f_jl_body    = NewString("");
    f_jl_exports = NewString("");
    f_directors    = NewString("");
    f_directors_h  = NewString("");
    f_dispatch     = NewString("");

    /* per-module directors=1 option flips director emission on */
    Node *opts = Getattr(n, "options");
    if (opts && Getattr(opts, "directors")) allow_directors();

    Swig_register_filebyname("begin",   f_begin);
    Swig_register_filebyname("runtime", f_runtime);
    Swig_register_filebyname("header",  f_header);
    Swig_register_filebyname("wrapper", f_wrappers);
    Swig_register_filebyname("init",    f_init);
    Swig_register_filebyname("director",   f_directors);
    Swig_register_filebyname("director_h", f_directors_h);
    /* director runtime (Swig::Director base) before the core fills runtime */

    Swig_banner(f_begin);
    Printf(f_runtime,
      "#include <string>\n#include <cstring>\n#include <cstdlib>\n#include <cstddef>\n"
      "#include <exception>\n\n"
      "static thread_local int  swig_jl_err_code = 0;\n"
      "static thread_local std::string swig_jl_err_msg;\n"
      "#define SWIG_JL_ENTER() do { swig_jl_err_code = 0; } while (0)\n"
      "#define SWIG_JL_CATCH(ret) \\\n"
      "  catch (const std::exception& e) { swig_jl_err_code = 1; swig_jl_err_msg = e.what(); return ret; } \\\n"
      "  catch (...) { swig_jl_err_code = 1; swig_jl_err_msg = \"unknown C++ exception\"; return ret; }\n"
      "static char* swig_jl_strdup(const std::string& s) { char* p = (char*)malloc(s.size()+1); memcpy(p, s.c_str(), s.size()+1); return p; }\n\n");
    /* f_header follows swiglabels.swg, which defines SWIGEXPORT */
    Printf(f_header,
      "extern \"C\" {\n"
      "SWIGEXPORT int swig_jl_last_error_code() { return swig_jl_err_code; }\n"
      "SWIGEXPORT const char* swig_jl_last_error_msg() { return swig_jl_err_msg.c_str(); }\n"
      "SWIGEXPORT void swig_jl_str_free(char* p) { free(p); }\n"
      "}\n\n");

    Language::top(n);
    /* after swiglabels.swg (inserted by top), so SWIGEXPORT is defined */
    if (Swig_directors_enabled()) Swig_insert_file("director.swg", f_runtime);

    SwigType_emit_type_table(f_runtime, f_wrappers);
    Printf(f_init, "extern \"C\" SWIGEXPORT void _swig_jl_init_types() {\n  SWIG_InitializeModule(0);\n%s}\n", f_type_init);

    String *jlfile = NewStringf("%s%s.jl", SWIG_output_directory(), module_name);
    File *jf = NewFile(jlfile, "w", SWIG_output_files());
    if (!jf) { FileErrorDisplay(jlfile); Exit(EXIT_FAILURE); }
    Printf(jf, "# Generated by SWIG -julia. Do not edit.\n");
    Printf(jf, "module %s\n\n", module_name);
    // Ask Julia for the platform's extension rather than baking in ".so", so the
    // generated module is portable to macOS and Windows without post-processing.
    Printf(jf, "const _lib = joinpath(@__DIR__, \"lib%s_wrap.\" * Base.Libc.Libdl.dlext)\n\n", module_name);
    Printf(jf, "function __init__()\n    ccall((:_swig_jl_init_types, _lib), Cvoid, ())\n    for (n, t) in _proxy_types\n        ccall((:swig_jl_register_proxy_type, _lib), Cvoid, (Cstring, Any), n, t)\n    end\n");
    if (Swig_directors_enabled())
      Printf(jf, "    ccall((:swig_jl_set_module, _lib), Cvoid, (Any,), @__MODULE__)\n");
    Printf(jf, "end\n\n");
    Printf(jf,
      "struct SwigError <: Exception\n    msg::String\nend\n"
      "Base.showerror(io::IO, e::SwigError) = print(io, \"%s error: \", e.msg)\n"
      "Base.show(io::IO, e::SwigError) = print(io, \"SwigError(\", repr(e.msg), \")\")\n\n"
      "function _check(v)\n"
      "    if ccall((:swig_jl_last_error_code, _lib), Cint, ()) != 0\n"
      "        throw(SwigError(unsafe_string(ccall((:swig_jl_last_error_msg, _lib), Cstring, ()))))\n"
      "    end\n    v\nend\n\n"
      "# obj.method / T.static: the method bound to its object, callable like a closure\n"
      "struct _SwigBound{F,S} <: Base.Function\n    f::F\n    self::S\nend\n"
      "(b::_SwigBound)(args...; kw...) = b.f(b.self, args...; kw...)\n"
      "Base.show(io::IO, b::_SwigBound) = b.self isa Type ? print(io, nameof(b.self), \".\", nameof(b.f)) :\n"
      "    print(io, nameof(b.f), \" bound to a \", nameof(typeof(b.self)))\n"
      "Base.show(io::IO, ::MIME\"text/plain\", b::_SwigBound) = show(io, b)\n\n"
      "# further properties: add a method for (::YourProxy, ::Val{:name})\n"
      "_swig_getproperty(x, ::Val{s}) where {s} = getfield(x, s)\n"
      "_swig_upcast(x, base::String) = ccall((:swig_jl_upcast, _lib), Ptr{Cvoid}, (Any, Cstring), x, base)\n\n"
      "# no overload matched: the prototypes and the argument types given\n"
      "_swig_typename(T) = sprint(show, T; context = :module => @__MODULE__)\n"
      "function _swig_typedesc(x)\n"
      "    T = _swig_typename(typeof(x))\n"
      "    x isa AbstractVector && !isconcretetype(eltype(x)) && !isempty(x) || return T\n"
      "    string(T, \"[\", join(unique(_swig_typename(typeof(e)) for e in x), \", \"), \"]\")\n"
      "end\n"
      "function _swig_overload_msg(name::String, protos::String, args::Tuple)\n"
      "    have = string(name, \"(\", join((\"::\" * _swig_typedesc(a) for a in args), \", \"), \")\")\n"
      "    string(\"Wrong number or type of arguments for overloaded function '\", name, \"'.\\n\",\n"
      "        \"  Possible prototypes are:\\n\", protos, \"  You have: \", have)\n"
      "end\n"
      "# behind invokelatest: inferring the message would slow the first call of every caller\n"
      "_swig_overload_error(name, protos, args) = throw(SwigError(Base.invokelatest(_swig_overload_msg, name, protos, args)))\n\n"
      "function _takestr(p::Ptr{UInt8})\n"
      "    _check(p)\n    s = unsafe_string(p)\n"
      "    ccall((:swig_jl_str_free, _lib), Cvoid, (Ptr{UInt8},), p)\n    s\nend\n\n",
      module_name);
    Printf(jf, "%s",
      "# Overloads whose argument unions overlap are ambiguous there: the overlap calls the\n"
      "# overload fitting more arguments more tightly, else the one emitted first (SWIG's order).\n"
      "function _swig_disambiguate(mod::Module)\n"
      "    for _ in 1:3\n"
      "        added = false\n"
      "        for n in Base.names(mod; all = true)\n"
      "            Base.isdefined(mod, n) || continue\n"
      "            f = Base.getfield(mod, n)\n"
      "            (f isa Base.Function || f isa Core.DataType) && Base.parentmodule(f) === mod || continue\n"
      "            ms = Base.sort!(Base.collect(Base.methods(f, mod)), by = m -> m.line)\n"
      "            for i in Base.eachindex(ms), j in (i + 1):Base.length(ms)\n"
      "                m1, m2 = ms[i], ms[j]\n"
      "                m1.sig isa DataType && m2.sig isa DataType && Base.isambiguous(m1, m2) || continue\n"
      "                s = Base.typeintersect(m1.sig, m2.sig)\n"
      "                s isa DataType || continue\n"
      "                ts = Base.collect(s.parameters)[2:end]\n"
      "                Base.any(t -> t isa Core.TypeofVararg, ts) && continue\n"
      "                p1, p2 = m1.sig.parameters, m2.sig.parameters\n"
      "                score = Base.length(p1) == Base.length(p2) ?\n"
      "                    Base.sum(Base.Int(p1[k] <: p2[k]) - Base.Int(p2[k] <: p1[k]) for k in 2:Base.length(p1); init = 0) : 0\n"
      "                w = score < 0 ? m2 : m1\n"
      "                a = [Base.Symbol(:x, k) for k in Base.eachindex(ts)]\n"
      "                F = f isa Core.DataType ? Type{f} : Base.typeof(f)\n"
      "                Core.eval(mod, :((::$F)($([:($(a[k])::$(ts[k])) for k in Base.eachindex(ts)]...)) =\n"
      "                    Base.invoke($f, $(Tuple{Base.collect(w.sig.parameters)[2:end]...}), $(a...))))\n"
      "                added = true\n"
      "            end\n"
      "        end\n"
      "        added || break\n"
      "    end\n"
      "end\n"
      "\n");
    if (Swig_directors_enabled())
      Printf(jf, "struct _SwigNoOverride end\nconst _swig_director_no_override = _SwigNoOverride()\n\n");
    Dump(f_jl_types, jf);
    Printf(jf, "\nconst _proxy_types = [\n%s]\n\n", f_proxy_pairs);
    Dump(f_jl_body, jf);
    /* Expand every collected method that carries trailing default arguments
       into one entry per reachable arity, dropping the Julia-level default.
       A C++ default-arg overload f(a, b, opts=...) otherwise becomes a single
       Julia method matching two arities, and its shorter arity can collide
       with a different overload's longer arity (e.g. conditional / to_function
       below).  Per-arity entries expose those collisions to the merge pass and
       let it disambiguate each arity independently.  The trailing dropped
       parameters are spliced back into the body/preserve as their default
       literals, so the chosen C wrapper still receives every argument. */
    expand_default_arg_arities();
    /* drain collected methods */
    emit_method_groups(jf, jl_methods, jl_method_order);
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
        /* collect inherited member entries into a fresh per-derived group set,
           then run the same expand+merge+emit as for globals so the flattened
           overloads are disambiguated for the derived class too. */
        Hash *dmeth = NewHash(); List *dorder = NewList();
        /* Collision keys of the derived class's OWN members. A base-class
           forwarder must not be emitted for a signature the derived class
           already defines: its own definition is emitted earlier, so the
           forwarder would silently overwrite the override (and, since Julia
           1.12, break precompilation outright -- "Method overwriting is not
           permitted during Module precompilation"). The statics path below
           already guards this way. */
        Hash *ownkeys = NewHash();
        {
          List *oml = class_members ? (List *)Getattr(class_members, derived) : 0;
          if (oml) {
            for (int mi = 0; mi < Len(oml); ++mi) {
              Hash *src = (Hash *)Getitem(oml, mi);
              List *nn = NewList(), *tt = NewList(), *dd = NewList();
              split_signature(Getattr(src, "sig"), nn, tt, dd);
              String *st = NewString("");
              for (int z = 0; z < Len(tt); ++z) Printf(st, "%s,", (String *)Getitem(tt, z));
              String *key = NewStringf("%s|%s", Getattr(src, "fname"), st);
              Setattr(ownkeys, key, "1");
              Delete(nn); Delete(tt); Delete(dd); Delete(st); Delete(key);
            }
          }
        }
        for (int qi = 0; qi < Len(queue); ++qi) {
          String *b = (String *)Getitem(queue, qi);
          if (Getattr(seen, b)) continue;
          Setattr(seen, b, "1");
          List *ml = class_members ? (List *)Getattr(class_members, b) : 0;
          Hash *bkeys = NewHash();   /* this base's signatures hide those of farther bases */
          if (ml) {
            for (int mi = 0; mi < Len(ml); ++mi) {
              Hash *src = (Hash *)Getitem(ml, mi);
              /* rebind self::<base> to self::<derived> in the signature */
              String *sig = Copy(Getattr(src, "sig"));
              String *selfpat = NewStringf("self::%s", Getattr(src, "selftype"));
              String *selfrep = NewStringf("self::%s", derived);
              Replaceall(sig, selfpat, selfrep);
              Delete(selfpat); Delete(selfrep);
              Hash *de = NewHash();
              Setattr(de, "fname", Copy(Getattr(src, "fname")));
              Setattr(de, "sig", sig);
              /* an opaque `self.ptr` is the derived object's address: cast it to the base */
              String *upcast = NewStringf("_swig_upcast(self, \"%s\")", Getattr(src, "selftype"));
              String *dbody = Copy(Getattr(src, "body"));
              Replaceall(dbody, "self.ptr", upcast);
              Delete(upcast);
              Setattr(de, "body", dbody);
              Setattr(de, "preserve", Copy(Getattr(src, "preserve")));
              Setattr(de, "node", Getattr(src, "node"));
              Setattr(de, "partial", Copy(Getattr(src, "partial")));
              /* collision key: fname + per-arg TYPES of the rebound signature */
              List *nn = NewList(), *tt = NewList(), *dd = NewList();
              split_signature(sig, nn, tt, dd);
              String *st = NewString("");
              for (int z = 0; z < Len(tt); ++z) Printf(st, "%s,", (String *)Getitem(tt, z));
              String *key = NewStringf("%s|%s", Getattr(src, "fname"), st);
              Setattr(bkeys, key, "1");
              if (Getattr(ownkeys, key)) {   /* derived or a nearer base overrides it */
                Delete(de);
                Delete(nn); Delete(tt); Delete(dd); Delete(st); Delete(key);
                continue;
              }
              List *gl = (List *)Getattr(dmeth, key);
              if (!gl) { gl = NewList(); Setattr(dmeth, key, gl); Append(dorder, Copy(key)); }
              Append(gl, de);
              Delete(nn); Delete(tt); Delete(dd); Delete(st); Delete(key);
            }
          }
          for (Iterator ki = First(bkeys); ki.key; ki = Next(ki)) Setattr(ownkeys, ki.key, "1");
          Delete(bkeys);
          Hash *set = class_statics ? (Hash *)Getattr(class_statics, b) : 0;
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
        /* expand default-arg arities for this derived set, then merge+emit */
        {
          Hash *save_m = jl_methods; List *save_o = jl_method_order;
          jl_methods = dmeth; jl_method_order = dorder;
          expand_default_arg_arities();
          emit_method_groups(jf, jl_methods, jl_method_order);
          Delete(jl_methods); Delete(jl_method_order);
          jl_methods = save_m; jl_method_order = save_o;
        }
        Delete(queue); Delete(seen); Delete(ownkeys);
        ci = Next(ci);
      }
    }
    emit_method_style_getproperty(jf);
    emit_static_method_style_getproperty(jf);
    emit_docs(jf);
    if (f_jl_protos) Printf(jf, "\n# prototypes listed when no overload matches\n%s\n", f_jl_protos);
    Printf(jf, "_swig_disambiguate(@__MODULE__)\n");
    if (Len(f_jl_exports) > 0) Printf(jf, "export %s\n", f_jl_exports);
    Printf(jf, "\nend # module %s\n", module_name);
    Delete(jf);

    /* the C dispatchers exist once the Julia methods are emitted */
    Dump(f_runtime, f_begin);
    Dump(f_header, f_begin);
    Dump(f_directors_h, f_begin);
    Dump(f_directors, f_begin);
    Dump(f_wrappers, f_begin);
    Dump(f_dispatch, f_begin);
    Dump(f_init, f_begin);

    if (n_skipped > 0)
      Printf(stderr, "swig -julia: skipped %d declaration(s) with unsupported types\n", n_skipped);

    Delete(f_begin);
    return SWIG_OK;
  }

  /* %feature("customdoc"): record the documentation of an overload set once,
     under the Julia name it is reachable by; classes sharing a text share a section. */
  void record_doc(Node *n, String *fname) {
    if (!checkAttribute(n, "feature:customdoc", "1")) return;
    /* at the last overload, so every overload's wrap:parms is set */
    if (Getattr(n, "sym:nextSibling")) return;
    if (Getattr(n, "sym:overloaded")) n = Getattr(n, "sym:overloaded");
    String *doc = Swig_document_function(n);
    if (!doc || Len(doc) == 0) return;
    while (Strstr(doc, "\n\n\n")) Replaceall(doc, "\n\n\n", "\n\n");
    Chop(doc);
    while (Len(doc) > 0 && (*Char(doc) == '\n' || *Char(doc) == ' ')) Delitem(doc, 0);
    if (Len(doc) == 0) return;
    if (!jl_docs) { jl_docs = NewHash(); jl_doc_order = NewList(); }
    Hash *secs = (Hash *)Getattr(jl_docs, fname);   /* text -> List of class names */
    if (!secs) {
      secs = NewHash(); Setattr(jl_docs, fname, secs); Append(jl_doc_order, Copy(fname));
      Setattr(secs, "#order", NewList());
    }
    List *owners = (List *)Getattr(secs, doc);
    if (!owners) { owners = NewList(); Setattr(secs, doc, owners); Append((List *)Getattr(secs, "#order"), Copy(doc)); }
    if (class_jlname && Strcmp(class_jlname, fname) != 0) Append(owners, NewStringf("`%s.%s`", class_jlname, fname));
  }

  /* One @doc per Julia function: Julia shows a function's documentation across
     all its methods, so the sections of every class defining the name are joined. */
  void emit_docs(File *jf) {
    if (!jl_doc_order) return;
    Printf(jf, "\n# documentation (%%feature(\"customdoc\"))\n");
    for (int i = 0; i < Len(jl_doc_order); ++i) {
      String *fname = (String *)Getitem(jl_doc_order, i);
      const char *c = Char(fname);
      bool ident = isalpha((unsigned char)c[0]) || c[0] == '_';
      for (const char *q = c; *q && ident; ++q) ident = isalnum((unsigned char)*q) || *q == '_' || *q == '!';
      if (!ident) continue;
      Hash *secs = (Hash *)Getattr(jl_docs, fname);
      List *order = (List *)Getattr(secs, "#order");
      String *txt = NewString("");
      for (int k = 0; k < Len(order); ++k) {
        String *doc = (String *)Getitem(order, k);
        List *owners = (List *)Getattr(secs, doc);
        if (k) Printf(txt, "\n\n---\n\n");
        for (int o = 0; o < Len(owners); ++o) Printf(txt, "%s%s", o ? ", " : "**", (String *)Getitem(owners, o));
        if (Len(owners)) Printf(txt, "**\n\n");
        Append(txt, doc);
      }
      Replaceall(txt, "\\", "\\\\");
      Replaceall(txt, "$", "\\$");
      Replaceall(txt, "\"", "\\\"");
      Printf(jf, "@doc \"\"\"\n%s\n\"\"\" %s\n", txt, fname);
      Delete(txt);
    }
  }

  void add_export(const String *name) {
    if (Char(name)[0] == '_') return;  /* a leading underscore marks an internal name */
    String *probe = NewStringf("|%s|", name);
    String *hay = NewStringf("|%s|", f_jl_exports);
    Replaceall(hay, ", ", "|");
    bool present = Strstr(hay, probe) != 0;
    Delete(probe); Delete(hay);
    if (present) return;
    if (Len(f_jl_exports) > 0) Printv(f_jl_exports, ", ", NIL);
    Printv(f_jl_exports, name, NIL);
  }

  /* a usable Julia identifier: [A-Za-z_][A-Za-z0-9_]*, not a reserved word */
  static bool jl_identifier(const String *s) {
    const char *p = Char(s);
    if (!p || !*p || (!isalpha((unsigned char)*p) && *p != '_')) return false;
    for (; *p; ++p) if (!isalnum((unsigned char)*p) && *p != '_') return false;
    static const char *kw[] = {"end","begin","function","module","baremodule",
      "if","else","elseif","while","for","try","catch","finally","return","do",
      "let","local","global","const","struct","mutable","abstract","primitive",
      "type","quote","macro","using","import","export","in","isa","where","true",
      "false","break","continue",0};
    for (int i = 0; kw[i]; ++i) if (Strcmp(s, kw[i]) == 0) return false;
    return true;
  }

  void skip(Node *n, const char *why) {
    ++n_skipped;
    Printf(f_jl_body, "# skipped %s (%s)\n", Getattr(n, "sym:name"), why);
  }

  /* C expression boxing a scalar/string/class element `expr` (of resolved
     type `et`) into a jl_value_t*, or 0 if the element kind is unsupported.
     The std::vector<SWIGTYPE>/std::pair<SWIGTYPE,SWIGTYPE> typemaps in
     julia.swg only fire when the matcher generalises template args -- which
     it does not for instantiated templates -- so the backend synthesises the
     marshaling here for std::vector and std::pair returns. */
  String *elem_box(SwigType *et, const char *expr) {
    SwigType *r = SwigType_typedef_resolve_all(et);
    String *res = 0;
    String *s = SwigType_str(r, 0);
    if (Strcmp(s, "double") == 0 || Strcmp(s, "float") == 0)
      res = NewStringf("jl_box_float64((double)(%s))", expr);
    else if (Strcmp(s, "bool") == 0)
      res = NewStringf("jl_box_bool((%s) ? 1 : 0)", expr);
    else if (SwigType_isenum(r) || Strcmp(s, "int") == 0 || Strcmp(s, "long") == 0 ||
             Strcmp(s, "long long") == 0 || Strcmp(s, "short") == 0 || Strcmp(s, "size_t") == 0 ||
             Strstr(s, "unsigned") || Strcmp(s, "casadi_int") == 0)
      res = NewStringf("jl_box_int64((int64_t)(%s))", expr);
    else if (Strcmp(s, "std::string") == 0 || Strcmp(s, "string") == 0)
      res = NewStringf("jl_cstr_to_string((%s).c_str())", expr);
    else if (classLookup(r)) {  /* class element: heap-copy + proxy wrap */
      SwigType *pt = Copy(r);
      SwigType_add_pointer(pt);
      SwigType_remember(pt);
      String *mangled = SwigType_manglestr(pt);
      String *ct = SwigType_str(r, 0);
      res = NewStringf("SWIG_NewPointerObj(new %s(%s), SWIGTYPE%s, SWIG_POINTER_OWN)", ct, expr, mangled);
      Delete(mangled); Delete(pt); Delete(ct);
    }
    Delete(s); Delete(r);
    return res;
  }

  /* Julia element-type name for a resolved scalar/string SwigType, or 0 if
     the kind has no concrete Julia eltype (used for empty-literal defaults). */
  String *julia_eltype(SwigType *et) {
    SwigType *r = SwigType_typedef_resolve_all(et);
    String *s = SwigType_str(r, 0);
    String *res = 0;
    if (Strcmp(s, "double") == 0 || Strcmp(s, "float") == 0)
      res = NewString("Float64");
    else if (Strcmp(s, "bool") == 0)
      res = NewString("Bool");
    else if (SwigType_isenum(r) || Strcmp(s, "int") == 0 || Strcmp(s, "long") == 0 ||
             Strcmp(s, "long long") == 0 || Strcmp(s, "short") == 0 || Strcmp(s, "size_t") == 0 ||
             Strstr(s, "unsigned") || Strcmp(s, "casadi_int") == 0)
      res = NewString("Int64");
    else if (Strcmp(s, "std::string") == 0 || Strcmp(s, "string") == 0)
      res = NewString("String");
    Delete(s); Delete(r);
    return res;
  }

  /* Julia literal for a std::numeric_limits<T>::f() default, or 0 */
  String *julia_limit(String *dv) {
    const char *c = Char(dv), *lt = strstr(c, "numeric_limits<"), *gt = lt ? strrchr(c, '>') : 0;
    if (!lt || !gt) return 0;
    String *ts = NewStringWithSize(lt + 15, (int)(gt - lt - 15));
    Replaceall(ts, " ", "");
    String *et = julia_eltype(ts);
    Delete(ts);
    if (!et) return 0;
    bool isf = Strcmp(et, "Float64") == 0;
    String *res = 0;
    if (strstr(gt, "::max()")) res = NewStringf(isf ? "floatmax(%s)" : "typemax(%s)", et);
    else if (strstr(gt, "::lowest()")) res = NewStringf(isf ? "-floatmax(%s)" : "typemin(%s)", et);
    else if (strstr(gt, "::min()")) res = NewStringf(isf ? "floatmin(%s)" : "typemin(%s)", et);
    else if (isf && strstr(gt, "::infinity()")) res = NewString("Inf");
    else if (isf && strstr(gt, "::epsilon()")) res = NewString("eps(Float64)");
    Delete(et);
    return res;
  }

  /* Empty Julia literal for a default-constructed std::vector<E>/std::map<K,V>
     argument.  With a known element kind, emit the precise typed literal
     ("Float64[]", "Dict{String,Int64}()", "Dict{String,Vector{String}}()");
     otherwise -- only when `permissive` (the Julia annotation is `Any`, so an
     untyped literal still type-checks) -- emit the bare "[]" / "Dict()".
     Returns 0 (no default) if the type is neither vector nor map, or the
     element kind is unknown and the annotation is not permissive. */
  String *julia_empty_default(SwigType *t, bool permissive) {
    SwigType *r = SwigType_typedef_resolve_all(t);
    if (SwigType_isreference(r)) SwigType_del_reference(r);
    if (SwigType_isqualifier(r)) SwigType_del_qualifier(r);
    String *res = 0;
    if (SwigType_istemplate(r)) {
      String *prefix = SwigType_templateprefix(r);
      List *targs = SwigType_parmlist(r);
      if (Strcmp(prefix, "std::vector") == 0 && Len(targs) >= 1) {
        String *e = julia_eltype((SwigType *)Getitem(targs, 0));
        if (e) { res = NewStringf("%s[]", e); Delete(e); }
        else if (permissive) res = NewString("[]");
      } else if (Strcmp(prefix, "std::map") == 0 && Len(targs) == 2) {
        String *k = julia_eltype((SwigType *)Getitem(targs, 0));
        String *v = julia_eltype((SwigType *)Getitem(targs, 1));
        if (!v) {  /* value may itself be a vector/map: recurse for its literal type */
          String *inner = julia_empty_default((SwigType *)Getitem(targs, 1), false);
          if (inner) {
            /* derive the Julia eltype from the literal: "T[]" -> "Vector{T}" */
            const char *ic = Char(inner);
            size_t il = Len(inner);
            if (il >= 2 && ic[il-1] == ']' && ic[il-2] == '[') {
              String *base = NewStringWithSize(ic, (int)(il - 2));
              v = NewStringf("Vector{%s}", base); Delete(base);
            }
            Delete(inner);
          }
        }
        if (k && v) res = NewStringf("Dict{%s,%s}()", k, v);
        else if (permissive) res = NewString("Dict()");
        if (k) Delete(k);
        if (v) Delete(v);
      }
      Delete(prefix); Delete(targs);
    }
    Delete(r);
    return res;
  }

  /* If `rt` is std::vector<E> or std::pair<A,B> with marshallable elements,
     fill ct/jt/jlout and a C `out` body ($1 -> $result); return true. */
  bool synth_template_return(SwigType *rt, String **ct, String **jt, String **jlout, String **outcode) {
    SwigType *r = SwigType_typedef_resolve_all(rt);
    if (SwigType_isreference(r)) SwigType_del_reference(r);
    if (SwigType_isqualifier(r)) SwigType_del_qualifier(r);
    bool done = false;
    if (SwigType_istemplate(r)) {
      String *prefix = SwigType_templateprefix(r);
      List *targs = SwigType_parmlist(r);
      bool is_vec = Strcmp(prefix, "std::vector") == 0 && Len(targs) >= 1;
      bool is_pair = Strcmp(prefix, "std::pair") == 0 && Len(targs) == 2;
      bool ref = SwigType_isreference(SwigType_typedef_resolve_all(rt));
      const char *deref = ref ? "(*$1)" : "$1";
      String *rs = SwigType_str(r, 0);  /* concrete container type, e.g. std::vector<Widget> */
      /* bind a typed const-ref to $1: a SwigValueWrapper return converts
         implicitly to const T&, but member access on it would not. */
      String *bind = NewStringf("const %s& __r = %s;", rs, deref);
      if (is_vec) {
        SwigType *et = (SwigType *)Getitem(targs, 0);
        String *probe = elem_box(et, "__r[0]");  /* probe only: validate element kind */
        if (probe) {
          *ct = NewString("jl_value_t*"); *jt = NewString("Any");
          *jlout = NewString("_check($call)::Vector");
          String *es = SwigType_str(SwigType_typedef_resolve_all(et), 0);
          String *box = elem_box(et, "__e");
          String *elt = julia_eltype(et);
          String *eltc = 0;
          if (elt) eltc = NewStringf("(jl_value_t*)jl_%s_type", Strcmp(elt, "Float64") == 0 ? "float64" :
                                     Strcmp(elt, "Int64") == 0 ? "int64" : Strcmp(elt, "Bool") == 0 ? "bool" : "string");
          else {
            String *pn = proxy_name(et);
            eltc = pn ? NewStringf("swig_jl_proxy_type(\"%s\")", pn) : NewString("(jl_value_t*)jl_any_type");
            if (pn) Delete(pn);
          }
          *outcode = NewString("");
          Printf(*outcode,
            "  { %s\n"
            "    jl_value_t* __a = jl_apply_array_type((jl_value_t*)jl_any_type, 1);\n"
            "    jl_array_t* __ja = jl_alloc_array_1d(__a, __r.size());\n"
            "    JL_GC_PUSH1(&__ja);\n"
            "    for (size_t __i=0; __i<__r.size(); ++__i) {\n"
            "      const %s& __e = __r[__i];\n"
            "      swig_jl_aset((jl_value_t*)__ja, __i, %s);\n"
            "    }\n"
            "    $result = swig_jl_narrow((jl_value_t*)__ja, %s);\n"
            "    JL_GC_POP(); }", bind, es, box, eltc);
          Delete(es); Delete(box); Delete(probe); Delete(eltc);
          if (elt) Delete(elt);
          done = true;
        }
      } else if (is_pair) {
        SwigType *t1 = (SwigType *)Getitem(targs, 0);
        SwigType *t2 = (SwigType *)Getitem(targs, 1);
        String *b1 = elem_box(t1, "__r.first");
        String *b2 = elem_box(t2, "__r.second");
        if (b1 && b2) {
          *ct = NewString("jl_value_t*"); *jt = NewString("Any");
          *jlout = NewString("_check($call)::Tuple");
          *outcode = NewStringf("  { %s\n    $result = swig_jl_make_tuple2(%s, %s); }", bind, b1, b2);
          done = true;
        }
        if (b1) Delete(b1);
        if (b2) Delete(b2);
      }
      Delete(rs); Delete(bind);
      Delete(prefix); Delete(targs);
    }
    Delete(r);
    if (done) Swig_fragment_emit(NewString("SwigJlRuntime"));  /* julia.h + helpers */
    return done;
  }

  /* ---- the single emission point: every callable lands here ---- */

  virtual int functionWrapper(Node *n) {
    if (Getattr(n, "memberset")) return SWIG_OK;  /* data members are read-only */
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
    Setattr(n, "wrap:parms", parms);   /* read by the customdoc prototypes */
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
    /* an explicit jlout typemap on a class return (e.g. a typedef meant to come
       back as a native Julia value) overrides wrapping it in the proxy */
    bool ret_is_class = ret_proxy != 0 && !ret_jlout;
    /* std::vector/std::pair returns: the typemap matcher won't generalise
       instantiated template args to SWIGTYPE, so synthesise the marshaling. */
    String *synth_out = 0;
    if (!ret_jlout && !ret_is_class) {
      String *sct = 0, *sjt = 0, *sjl = 0, *soc = 0;
      if (synth_template_return(returntype, &sct, &sjt, &sjl, &soc)) {
        ret_ct = sct; ret_jt = sjt; ret_jlout = sjl; synth_out = soc;
      }
    }
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
    String *partial = NewString(in_static ? "0" : "");  /* per Julia slot: partial annotation */
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
      Append(partial, jp && Getattr(p, "tmap:jlparam:partial") ? "1" : "0");
      {  /* compactdefaultargs: literal + empty-container defaults, trailing run */
        String *dv = Getattr(p, "value");
        String *jdv = NewString("");
        if (dv && Len(dv) > 0) {
          const char *dc = Char(dv);
          size_t dl = Len(dv);
          if (Strcmp(dv, "true") == 0 || Strcmp(dv, "false") == 0) Printv(jdv, dv, NIL);
          else if (dl >= 2 && dc[0] == '"' && dc[dl-1] == '"') Printv(jdv, dv, NIL);
          else if (dl >= 2 && dc[dl-2] == '(' && dc[dl-1] == ')') {
            /* default-constructed value, e.g. T(), std::vector<E>(), Dict():
               opaque-ptr class/container args (passed as `.ptr`, see below)
               get the wrapped default ctor; by-value (jl_value_t*) containers
               get an empty Julia literal matching their jlparam annotation. */
            if (pproxy && !SwigType_istemplate(SwigType_typedef_resolve_all(Getattr(p, "type"))))
              Printf(jdv, "%s()", pproxy);
            else if (Strstr(dv, "numeric_limits<")) {
              String *lit = julia_limit(dv);
              if (lit) { Printv(jdv, lit, NIL); Delete(lit); }
            } else {
              String *lit = julia_empty_default(Getattr(p, "type"), jl_any);
              if (lit) { Printv(jdv, lit, NIL); Delete(lit); }
            }
          } else {
            bool numeric = Len(dv) > 0;
            for (const char *c = Char(dv); *c; ++c)
              if (!(isdigit(*c) || *c == '.' || *c == '-' || *c == '+' || *c == 'e')) { numeric = false; break; }
            if (numeric) Printv(jdv, dv, NIL);
            else if (Strcmp(dv, "inf") == 0 || Strcmp(dv, "casadi::inf") == 0) Printv(jdv, "Inf", NIL);
            else if (Strcmp(dv, "-inf") == 0 || Strcmp(dv, "-casadi::inf") == 0) Printv(jdv, "-Inf", NIL);
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
    /* collect argout parms (OUTPUT and INOUT): void-returning functions hand
       them back as the Julia return value, others as (result, outputs...) */
    List *argouts = NewList();
    for (Parm *q = parms; q; q = nextSibling(q)) {
      if (Getattr(q, "tmap:argout")) Append(argouts, q);
    }
    bool is_void = Strcmp(ret_ct, "void") == 0;
    bool argout_mode = is_void && Len(argouts) > 0;
    bool extra_outs = !is_void && Len(argouts) > 0;
    if (argout_mode) {
      ret_ct = NewString("jl_value_t*");
      ret_jt = NewString("Any");
      is_void = false;
    }
    if (!is_void) emit_return_variable(n, returntype, w);
    String *actioncode = emit_action(n);
    /* director member wrappers carry an `if (upcall)` base-vs-virtual branch;
       the flat-C surface always calls the virtual (which dispatches to the
       Julia override when present), so pin upcall to false. */
    if (Strstr(actioncode, "upcall")) Wrapper_add_local(w, "upcall", "bool upcall = false");

    String *outtm = 0;
    String *argouttm = NewString("");
    if (argout_mode || extra_outs) {
      String *outtm = argouttm;
      for (int ai = 0; ai < Len(argouts); ++ai) {
        Parm *q = (Parm *)Getitem(argouts, ai);
        /* container output-args (std::vector<E>&, std::pair&): the generic
           argout body marshals via the interface's from_ptr, which has no
           Julia overload for primitive-element vectors -> reuse the synth
           return marshaling, appending the value to the result tuple. */
        String *sct = 0, *sjt = 0, *sjl = 0, *soc = 0;
        if (synth_template_return(Getattr(q, "type"), &sct, &sjt, &sjl, &soc)) {
          String *m = Copy(soc);
          Replaceall(m, "$1", Getattr(q, "lname"));
          Replaceall(m, "$result", "__ov");
          Printf(outtm, "  { jl_value_t* __ov = 0;\n%s\n    _outv = SWIG_AppendOutput(_outv, __ov); }\n", m);
          Delete(m); Delete(sct); Delete(sjt); Delete(sjl); Delete(soc);
          continue;
        }
        String *b = Copy(Getattr(q, "tmap:argout"));
        Replaceall(b, "%append_output(", "_outv = SWIG_AppendOutput(_outv, ");
        Replaceall(b, "$result", "_outv");
        Printv(outtm, b, "\n", NIL);
        Delete(b);
      }
      if (extra_outs) Replaceall(argouttm, "_outv = SWIG_AppendOutput(_outv, ", "swig_jl_push_output(jouts, ");
    }
    if (argout_mode) {
      outtm = argouttm;
    } else if (synth_out) {
      /* the standard 'out' path embeds the action in the typemap; the synth
         path supplies only the marshaling, so run the action first. */
      outtm = NewStringf("%s\n%s", actioncode, synth_out);
      Replaceall(outtm, "$1", Swig_cresult_name());
      Replaceall(outtm, "$result", "_outv");
    } else if (!is_void) {
      outtm = Swig_typemap_lookup_out("out", n, Swig_cresult_name(), w, actioncode);
      if (!outtm) {
        skip(n, "return conversion");
        DelWrapper(w); Delete(wname);
        return SWIG_NOWRAP;
      }
      Replaceall(outtm, "$result", "_outv");
    }

    if (extra_outs) Printf(csig, "%sjl_value_t* jouts", Len(csig) ? ", " : "");
    Printf(f_wrappers, "extern \"C\" SWIGEXPORT %s %s(%s) {\n  SWIG_JL_ENTER();\n  try {\n",
           ret_ct, wname, Len(csig) ? Char(csig) : "void");
    Printv(f_wrappers, w->locals, NIL);
    if (!is_void) Printf(f_wrappers, "  %s _outv%s;\n", ret_ct, argout_mode ? " = 0" : "");
    Printv(f_wrappers, w->code, NIL);
    if (is_void) {
      Printv(f_wrappers, actioncode, NIL);
      Printf(f_wrappers, "  return;\n");
    } else {
      if (argout_mode) Printv(f_wrappers, actioncode, NIL);
      Printv(f_wrappers, outtm, "\n", NIL);
      if (extra_outs) Printv(f_wrappers, argouttm, NIL);
      Printf(f_wrappers, "  return _outv;\n");
    }
    Printf(f_wrappers, "  } SWIG_JL_CATCH(%s)\n}\n\n",
           is_void ? "" : (Strstr(ret_ct, "*") ? "0" : (Strcmp(ret_ct, "double") == 0 ? "0.0" : "0")));

    /* ---- Julia side ---- */
    String *callcore = extra_outs
      ? NewStringf("ccall((:%s, _lib), %s, (%sAny, ), %s_swigjl_outs, )", wname, ret_jt, jccall_types, jccall_args)
      : NewStringf("ccall((:%s, _lib), %s, (%s), %s)", wname, ret_jt, jccall_types, jccall_args);
    String *body = 0;
    if (argout_mode) {
      body = NewStringf("_check(%s)", callcore);
    } else if (in_ctor) {
      body = NewStringf("%s(_check(%s))", class_jlname, callcore);
    } else if (ret_is_class) {
      body = NewStringf("%s(_check(%s))", ret_proxy, callcore);
    } else {
      body = Copy(ret_jlout);
      Replaceall(body, "$call", callcore);
    }
    if (extra_outs) {
      String *b2 = NewStringf("let _swigjl_outs = Any[]; (%s, _swigjl_outs...) end", body);
      Delete(body); body = b2;
    }

    /* %feature("julia:keep"): the C++ object borrows the other arguments; the
       receiver proxy keeps them (or nothing, for an overload without any) */
    if (GetFlag(n, "feature:julia:keep") && class_jlname && !in_ctor && !in_static) {
      String *kept = NewString("");
      int nk = 0;
      List *kn = NewList(), *kt = NewList(), *kd = NewList();
      split_signature(jargs, kn, kt, kd);
      for (int i = 1; i < Len(kn); ++i) {
        Printf(kept, "%s%s", nk ? ", " : "", (String *)Getitem(kn, i));
        ++nk;
      }
      Delete(kn); Delete(kt); Delete(kd);
      String *kv = nk == 0 ? NewString("nothing") : nk == 1 ? Copy(kept) : NewStringf("(%s,)", kept);
      String *b2 = NewStringf("(_swigjl_r = %s; setfield!(self, :_swig_refs, %s); _swigjl_r)", body, kv);
      Delete(body); body = b2;
      Delete(kept); Delete(kv);
      class_has_refs = true;
    }
    if (memberget_field && Getattr(n, "memberget")) {
      if (Len(preserve) > 0)
        Printf(f_jl_body, "_swig_getproperty(self::%s, ::Val{:%s}) = GC.@preserve %sbegin %s end\n",
               class_jlname, memberget_field, preserve, body);
      else
        Printf(f_jl_body, "_swig_getproperty(self::%s, ::Val{:%s}) = %s\n", class_jlname, memberget_field, body);
      Delete(body); Delete(callcore); Delete(csig); Delete(jargs); Delete(jccall_types); Delete(jccall_args);
      Delete(preserve); Delete(wname);
      if (ret_proxy) Delete(ret_proxy);
      DelWrapper(w);
      return SWIG_OK;
    }
    String *fname = in_ctor ? Copy(class_jlname)
                  : bare_symname ? Copy(bare_symname) : Copy(symname);
    if (!in_ctor) { String *safe = jl_method_name(fname); Delete(fname); fname = safe; }
    String *sig = NewString("");
    if (in_static) Printf(sig, "::Type{%s}%s", class_jlname, Len(jargs) ? ", " : "");
    Printv(sig, jargs, NIL);
    String *jline = NewString("");
    if (Len(preserve) > 0)
      Printf(jline, "%s(%s) = GC.@preserve %sbegin %s end\n", fname, sig, preserve, body);
    else
      Printf(jline, "%s(%s) = %s\n", fname, sig, body);
    /* collision key: name + per-arg TYPES (drop arg names and defaults) */
    String *sigtypes = NewString("");
    {
      String *tmp = Copy(sig);
      List *parts = Split(tmp, ',', -1);
      for (int pi = 0; pi < Len(parts); ++pi) {
        String *part = (String *)Getitem(parts, pi);
        const char *cc = Strstr(part, "::");
        String *ty = cc ? NewString(cc + 2) : Copy(part);
        {  /* strip default value */
          const char *eq = Strstr(ty, "=");
          if (eq) { String *t2 = NewStringWithSize(Char(ty), (int)(eq - Char(ty))); Delete(ty); ty = t2; }
        }
        Printf(sigtypes, "%s,", ty);
        Delete(ty);
      }
      Delete(parts); Delete(tmp);
    }
    {
      String *key = NewStringf("%s|%s", fname, sigtypes);
      if (!jl_methods) { jl_methods = NewHash(); jl_method_order = NewList(); }
      List *lst = (List *)Getattr(jl_methods, key);
      if (!lst) { lst = NewList(); Setattr(jl_methods, key, lst); Append(jl_method_order, Copy(key)); }
      Hash *e = NewHash();
      Setattr(e, "fname", Copy(fname));
      Setattr(e, "sig", Copy(sig));
      Setattr(e, "body", Copy(body));
      Setattr(e, "preserve", Copy(preserve));
      Setattr(e, "node", n);
      Setattr(e, "partial", Copy(partial));
      if (in_static && class_jlname) Setattr(e, "dispname", NewStringf("%s.%s", class_jlname, fname));
      Append(lst, e);
      Delete(key);
    }
    record_doc(n, fname);
    Delete(sigtypes);
    if (class_jlname && !in_static && !in_ctor) {
      /* member method: register a structured entry for derived-class
         flattening.  Stored entries flow through the same expand+merge+emit as
         globals (per derived class), so inherited overloads are disambiguated
         too.  `selftype` marks the leading self:: annotation to rewrite. */
      String *selfpat = NewStringf("self::%s", class_jlname);
      if (Strstr(sig, selfpat)) {
        Hash *me = NewHash();
        Setattr(me, "fname", Copy(fname));
        Setattr(me, "sig", Copy(sig));
        Setattr(me, "body", Copy(body));
        Setattr(me, "preserve", Copy(preserve));
        Setattr(me, "node", n);
        Setattr(me, "partial", Copy(partial));
        Setattr(me, "selftype", Copy(class_jlname));
        if (!class_members) class_members = NewHash();
        List *lst = (List *)Getattr(class_members, class_jlname);
        if (!lst) { lst = NewList(); Setattr(class_members, class_jlname, lst); }
        Append(lst, me);
        /* record this instance method name for method-style getproperty */
        if (!class_methods) class_methods = NewHash();
        Hash *ms = (Hash *)Getattr(class_methods, class_jlname);
        if (!ms) { ms = NewHash(); Setattr(class_methods, class_jlname, ms); }
        Setattr(ms, fname, GetFlag(n, "feature:julia:property") ? "property" : "1");
      }
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
    Delete(preserve); Delete(partial); Delete(wname);
    if (ret_proxy) Delete(ret_proxy);
    DelWrapper(w);
    return SWIG_OK;
  }

  /* ---- context plumbing around the Language base transforms ---- */

  virtual int classHandler(Node *n) {
    class_jlname = Getattr(n, "sym:name");
    String *cname = Getattr(n, "name");
    class_has_copyctor = false;

    class_has_refs = false;
    add_export(class_jlname);

    String *cxxname = SwigType_namestr(cname);
    Printf(f_proxy_pairs, "    (\"%s\", %s),\n", class_jlname, class_jlname);
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
      "extern \"C\" SWIGEXPORT void _swig_%s_delete(void *p) {\n"
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
    /* _swig_refs: Julia objects the C++ object borrows (a director's self,
       %feature("julia:keep") arguments); inherited by derived proxies */
    {
      if (refs_classes && Getattr(refs_classes, class_jlname)) class_has_refs = true;
      List *bl = class_bases ? (List *)Getattr(class_bases, class_jlname) : 0;
      if (bl) for (int bi = 0; bi < Len(bl); ++bi)
        if (refs_classes && Getattr(refs_classes, Getitem(bl, bi))) class_has_refs = true;
      if (class_has_refs) {
        if (!refs_classes) refs_classes = NewHash();
        Setattr(refs_classes, class_jlname, "1");
      }
    }
    Printf(f_jl_types,
      "mutable struct %s\n    ptr::Ptr{Cvoid}\n%s"
      "    function %s(p::Ptr{Cvoid})\n"
      "        x = new(p%s)\n"
      "        finalizer(o -> (ccall((:_swig_%s_delete, _lib), Cvoid, (Ptr{Cvoid},), o.ptr); o.ptr = C_NULL), x)\n"
      "        x\n    end\nend\n",
      class_jlname, class_has_refs ? "    _swig_refs::Any\n" : "", class_jlname,
      class_has_refs ? ", nothing" : "", class_jlname);
    /* copy-constructible proxy: give Julia an independent copy/deepcopy
       (proxies otherwise alias one C++ object).  %feature("julia:copy", "m")
       names a member method to use instead, for a copy ctor that shares. */
    String *copy_method = Getattr(n, "feature:julia:copy");
    if (copy_method) {
      Printf(f_jl_body, "Base.copy(x::%s) = %s(x)\n", class_jlname, copy_method);
      Printf(f_jl_body, "Base.deepcopy(x::%s) = %s(x)\n", class_jlname, copy_method);
    } else if (class_has_copyctor) {
      Printf(f_jl_body, "Base.copy(x::%s) = %s(x)\n", class_jlname, class_jlname);
      Printf(f_jl_body, "Base.deepcopy(x::%s) = %s(x)\n", class_jlname, class_jlname);
    }
    class_has_copyctor = false;
    class_jlname = 0;
    return SWIG_OK;
  }

  /* True if `n` is a copy constructor of class `cls`: a single in-parameter
     whose type resolves (after stripping const/ref) to the class itself. */
  bool is_copy_constructor(Node *n, Node *cls) {
    ParmList *parms = Getattr(n, "parms");
    if (!parms || ParmList_len(parms) != 1) return false;
    SwigType *pt = Getattr(parms, "type");
    if (!pt) return false;
    SwigType *r = SwigType_typedef_resolve_all(pt);
    if (SwigType_isreference(r)) SwigType_del_reference(r);
    if (SwigType_isqualifier(r)) SwigType_del_qualifier(r);
    Node *pc = classLookup(r);
    bool same = pc && cls && Getattr(pc, "name") &&
                Strcmp(Getattr(pc, "name"), Getattr(cls, "name")) == 0;
    Delete(r);
    return same;
  }

  virtual int constructorHandler(Node *n) {
    Node *cls = Swig_methodclass(n);
    if (cls && is_copy_constructor(n, cls)) class_has_copyctor = true;
    in_ctor = true;
    int r = Language::constructorHandler(n);
    in_ctor = false;
    return r;
  }

  /* a const member overloaded by a non-const one with the same parameters is
     unreachable in C++ through a non-const object; wrap the non-const one only */
  bool const_shadowed(Node *n) {
    String *decl = Getattr(n, "decl");
    if (!decl || strncmp(Char(decl), "q(const).", 9) != 0) return false;
    String *base = NewString(Char(decl) + 9);
    bool found = false;
    for (Node *o = Getattr(n, "sym:overloaded"); o && !found; o = Getattr(o, "sym:nextSibling"))
      if (o != n && !Equal(Getattr(o, "storage"), "static") && Equal(Getattr(o, "decl"), base)) found = true;
    Delete(base);
    return found;
  }

  virtual int memberfunctionHandler(Node *n) {
    if (const_shadowed(n)) return SWIG_NOWRAP;
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

  /* Named enums get a typed Julia @enum (instances named <Enum>_<member>);
     anonymous enums and plain constants fall through to integer consts. */
  virtual int enumDeclaration(Node *n) {
    if (ImportMode) return SWIG_OK;
    if (GetFlag(n, "feature:ignore")) return SWIG_OK;
    String *ename = Getattr(n, "sym:name");
    /* anonymous, $unnamed$, or already-emitted type: per-value consts only */
    if (!ename || !jl_identifier(ename)) return Language::enumDeclaration(n);
    if (enum_seen && Getattr(enum_seen, ename)) return Language::enumDeclaration(n);
    /* collect members with C auto-increment; explicit values must be integer
       literals (enumnumval). Bail to plain consts if any is a C++ expression. */
    List *names = NewList();
    List *vals = NewList();
    bool ok = true;
    long next = 0;
    for (Node *c = firstChild(n); c && ok; c = nextSibling(c)) {
      if (Strcmp(nodeType(c), "enumitem") != 0) continue;
      if (GetFlag(c, "feature:ignore")) continue;
      String *ev = Getattr(c, "enumvalue");
      String *nv = Getattr(c, "enumnumval");
      if (ev && !nv) { ok = false; break; }  /* non-literal C++ expression */
      String *mn = Getattr(c, "sym:name");
      if (!jl_identifier(mn)) { ok = false; break; }
      if (nv) next = (long)strtol(Char(nv), 0, 0);
      Append(names, mn);
      Append(vals, NewStringf("%ld", next));
      ++next;
    }
    if (!ok || Len(names) == 0) {
      Delete(names); Delete(vals);
      return Language::enumDeclaration(n);  /* fall back to integer consts */
    }
    /* typed @enum, instances named <Enum>_<member> to avoid clashing with the
       bare integer consts (kept for back-compat) emitted via emit_children. */
    String *prefix = NewStringf("%s_", ename);
    Printf(f_jl_body, "@enum %s::Cint ", ename);
    for (int i = 0; i < Len(names); ++i) {
      String *inst = NewStringf("%s%s", prefix, Getitem(names, i));
      Printf(f_jl_body, "%s=%s ", inst, Getitem(vals, i));
      add_export(inst);
      Delete(inst);
    }
    Printf(f_jl_body, "\n");
    add_export(ename);
    if (!enum_seen) enum_seen = NewHash();
    Setattr(enum_seen, ename, "1");
    Delete(prefix); Delete(names); Delete(vals);
    return Language::enumDeclaration(n);  /* also emit bare integer consts */
  }

  virtual int constantWrapper(Node *n) {
    String *symname = Getattr(n, "sym:name");
    if (!jl_identifier(symname)) { skip(n, "name not a Julia identifier"); return SWIG_OK; }
    String *value = Getattr(n, "value");
    SwigType *t = Getattr(n, "type");
    String *ts = SwigType_str(t, 0);
    /* class-scope: reference the fully-qualified C++ name in the getter */
    String *cref = (class_jlname && Getattr(n, "name")) ? Getattr(n, "name") : value;
    if (Strcmp(ts, "int") == 0 || Strcmp(ts, "long") == 0 || Strstr(ts, "long long")) {
      /* value may be a C++ qualified name (casadi::OP_ADD); emit a C getter */
      Printf(f_wrappers, "extern \"C\" SWIGEXPORT long long _swig_const_%s() { return (long long)(%s); }\n",
             symname, cref);
      Printf(f_jl_body, "const %s = Int(ccall((:_swig_const_%s, _lib), Clonglong, ()))\n",
             symname, symname);
      add_export(symname);
    } else if (Strcmp(ts, "double") == 0) {
      Printf(f_wrappers, "extern \"C\" SWIGEXPORT double _swig_const_%s() { return (double)(%s); }\n",
             symname, cref);
      Printf(f_jl_body, "const %s = ccall((:_swig_const_%s, _lib), Cdouble, ())\n",
             symname, symname);
      add_export(symname);
    }
    return SWIG_OK;  /* silently skip strings/other for now */
  }

  /* ---- directors: subclass a C++ class from Julia ----
   *
   * Each director-marked class C gets a C++ `SwigDirector_C : public C,
   * public Swig::Director`.  Its overridden virtuals marshal C++ args to
   * Julia (directorin), call the method's own module function `<method>(self,
   * args...)`, and marshal the Julia return back (directorout).  The Julia
   * side: a user defines `struct MySub <: CDirector` + `<method>(self::MySub,
   * args...) = ...` and constructs `C(self)` -- the proxy owns `self`, the
   * SwigDirector_C borrows it.  Non-overridden methods fall through to the C++ base.
   * %feature("julia:director", "Name") names the abstract supertype (default CDirector). */

  static String *director_type(Node *cls) {
    String *t = Getattr(cls, "feature:julia:director");
    return t && Len(t) > 0 ? Copy(t) : NewStringf("%sDirector", Getattr(cls, "sym:name"));
  }

  virtual int classDirectorInit(Node *n) {
    /* Julia types implementing the director subtype this abstract type */
    String *dt = director_type(n);
    Printf(f_jl_types, "abstract type %s end\n", dt);
    add_export(dt);
    String *declaration = Swig_director_declaration(n);
    Printf(f_directors_h, "\n%s\npublic:\n", declaration);
    Delete(declaration);
    return Language::classDirectorInit(n);
  }

  virtual int classDirectorEnd(Node *n) {
    Printf(f_directors_h, "};\n\n");
    return Language::classDirectorEnd(n);
  }

  /* Emit the dedicated director-construction C wrapper + Julia entry for a
     class C: _swig_new_SwigDirector_C(jl_self) -> void*; Julia C(self) builds
     a SwigDirector_C borrowing self.  The leading jl_value_t* self is defaulted
     so the (dead) auto-generated `new SwigDirector_C()` branch also compiles. */
  void emit_director_construct(String *supername, Node *cls) {
    String *dt = director_type(cls);
    Printf(f_wrappers,
      "extern \"C\" SWIGEXPORT void* _swig_new_SwigDirector_%s(jl_value_t *self) {\n"
      "  SWIG_JL_ENTER();\n  try { return static_cast<void*>(new SwigDirector_%s(self)); }\n"
      "  SWIG_JL_CATCH(0)\n}\n\n", supername, supername);
    /* the proxy owns a Ref holding self; the director borrows that Ref */
    Printf(f_jl_body,
      "function %s(self::%s)\n"
      "    r = Base.RefValue{Any}(self)\n"
      "    x = %s(ccall((:_swig_new_SwigDirector_%s, _lib), Ptr{Cvoid}, (Any,), r))\n"
      "    setfield!(x, :_swig_refs, r)\n"
      "    x\nend\n",
      supername, dt, supername, supername);
    if (!refs_classes) refs_classes = NewHash();
    Setattr(refs_classes, supername, "1");  /* director classes are declared before classHandler */
    Delete(dt);
  }

  /* ctor: prepend `jl_value_t* jl_self=0` to the user ctor parms; base-init the
     C++ class and Swig::Director(jl_self). */
  virtual int classDirectorConstructor(Node *n) {
    Node *parent = Getattr(n, "parentNode");
    String *supername = Swig_class_name(parent);
    String *classname = NewStringf("SwigDirector_%s", supername);
    String *decl = Getattr(n, "decl");
    ParmList *superparms = Getattr(n, "parms");
    /* append a trailing, defaulted `jl_value_t* jl_self=0`; trailing keeps the
       auto-generated `new SwigDirector_C(args)` (dead, $comparison==0) legal. */
    ParmList *parms = CopyParmList(superparms);
    SwigType *self_type = NewString("jl_value_t *");
    Parm *self_p = NewParm(self_type, NewString("jl_self"), n);
    Setattr(self_p, "value", "0");
    if (!parms) parms = self_p;
    else { Parm *last = parms; while (nextSibling(last)) last = nextSibling(last); set_nextSibling(last, self_p); }
    if (!Getattr(n, "defaultargs")) {
      Wrapper *w = NewWrapper();
      String *target = Swig_method_decl(0, decl, classname, parms, 0);
      String *call = Swig_csuperclass_call(0, Getattr(parent, "classtype"), superparms);
      Printf(w->def, "%s::%s : %s, Swig::Director(jl_self) { }\n\n", classname, target, call);
      Wrapper_print(w, f_directors);
      Delete(target); Delete(call); DelWrapper(w);
      String *hdr = Swig_method_decl(0, decl, classname, parms, 1);
      Printf(f_directors_h, "    %s;\n", hdr);
      Delete(hdr);
      if (ParmList_len(superparms) == 0)
        emit_director_construct(supername, parent);
    }
    Delete(classname); Delete(supername); Delete(self_type); Delete(parms);
    return Language::classDirectorConstructor(n);
  }

  virtual int classDirectorDefaultConstructor(Node *n) {
    String *classname = Swig_class_name(n);
    Wrapper *w = NewWrapper();
    Printf(w->def, "SwigDirector_%s::SwigDirector_%s(jl_value_t *jl_self) : Swig::Director(jl_self) { }\n\n",
           classname, classname);
    Wrapper_print(w, f_directors);
    DelWrapper(w);
    Printf(f_directors_h, "    SwigDirector_%s(jl_value_t *jl_self = 0);\n", classname);
    emit_director_construct(classname, n);
    Delete(classname);
    return Language::classDirectorDefaultConstructor(n);
  }

  virtual int classDirectorMethod(Node *n, Node *parent, String *super) {
    (void)super;
    String *name = Getattr(n, "name");
    String *classname = Getattr(parent, "sym:name");
    String *c_classname = Getattr(parent, "name");
    SwigType *returntype = Getattr(n, "type");
    ParmList *l = Getattr(n, "parms");
    String *decl = Getattr(n, "decl");
    String *storage = Getattr(n, "storage");
    String *value = Getattr(n, "value");
    bool pure_virtual = (Cmp(storage, "virtual") == 0) && (value && Cmp(value, "0") == 0);
    bool is_void = (Cmp(returntype, "void") == 0);
    int status = SWIG_OK;

    SwigType *rtype = Getattr(n, "conversion_operator") ? 0 : Getattr(n, "classDirectorMethods:type");
    String *pclassname = NewStringf("SwigDirector_%s", classname);
    String *qualname = NewStringf("%s::%s", pclassname, name);
    String *impl_sig = Swig_method_decl(rtype, decl, qualname, l, 0);
    String *hdr_sig = Swig_method_decl(rtype, decl, name, l, 1);

    Swig_director_parms_fixup(l);
    Swig_typemap_attach_parms("directorin", l, 0);

    Wrapper *w = NewWrapper();
    Printf(w->def, "%s {\n", impl_sig);

    /* upcall: the method's own Julia name on self, as the wrapper names it */
    String *dispatch = jl_method_name(Getattr(n, "sym:name"));
    String *dtype = director_type(parent);
    String *where = NewStringf("%s.%s", dtype, dispatch);
    String *args_build = NewString("");
    int nargs = 0;
    for (Parm *p = l, *pn = 0; p; p = pn) {
      pn = nextSibling(p);
      if (checkAttribute(p, "tmap:in:numinputs", "0")) continue;
      String *tm = Getattr(p, "tmap:directorin");
      if (!tm) {
        Swig_warning(WARN_TYPEMAP_DIRECTORIN_UNDEF, input_file, line_number,
                     "Unable to use type %s as a function argument in director method %s::%s (skipping director method).\n",
                     SwigType_str(Getattr(p, "type"), 0), SwigType_namestr(c_classname), SwigType_namestr(name));
        status = SWIG_NOWRAP; break;
      }
      String *body = Copy(tm);
      Replaceall(body, "$input", NewStringf("__jargs[%d]", nargs + 1));
      /* a multi-argument typemap covers the parameters up to tmap:directorin:next */
      pn = Getattr(p, "tmap:directorin:next");
      Replaceall(body, "$1", Getattr(p, "name"));
      Printf(args_build, "    { %s }\n", body);
      ++nargs;
      Delete(body);
    }

    /* return marshaling (skipped for void) */
    String *ret_marshal = NewString("");
    if (status == SWIG_OK && !is_void) {
      String *tm = Swig_typemap_lookup("directorout", n, Swig_cresult_name(), 0);
      if (!tm) {
        Swig_warning(WARN_TYPEMAP_DIRECTOROUT_UNDEF, input_file, line_number,
                     "Unable to use return type %s in director method %s::%s (skipping director method).\n",
                     SwigType_str(returntype, 0), SwigType_namestr(c_classname), SwigType_namestr(name));
        status = SWIG_NOWRAP;
      } else {
        String *cres = SwigType_lstr(returntype, "c_result");
        Printf(ret_marshal, "      %s;\n", cres);
        Replaceall(tm, "$input", "__r");
        Replaceall(tm, "$result", "c_result");
        /* a return that does not convert: pop the GC frame, name the override */
        Printf(ret_marshal, "      try { %s }\n", tm);
        Printf(ret_marshal, "      catch (const std::exception &__e) {\n"
               "        JL_GC_POP();\n"
               "        throw std::runtime_error(std::string(\"Julia director %s returned an invalid value: \") + __e.what());\n"
               "      }\n", where);
        Delete(cres); Delete(tm);
      }
    }

    if (status == SWIG_OK) {
      /* base-call expression for non-overridden / pure paths */
      String *basecall = NewString("");
      if (pure_virtual) {
        Printf(basecall, "Swig::DirectorPureVirtualException(\"%s::%s\")", classname, name);
      }
      Printf(w->code,
        "  jl_value_t *__self = swig_get_self();\n"
        "  jl_value_t *__f = Swig::swig_director_fn(\"%s\");\n", dispatch);
      Printf(w->code, "  if (__self && __f) {\n");
      Printf(w->code, "    jl_value_t **__jargs;\n");
      Printf(w->code, "    JL_GC_PUSHARGS(__jargs, %d);\n", nargs + 1);
      Printf(w->code, "    __jargs[0] = __self;\n");
      Printv(w->code, args_build, NIL);
      Printf(w->code, "    jl_value_t *__r = jl_call(__f, __jargs, %d);\n", nargs + 1);
      /* upcall raised: surface the Julia exception's own message to C++ */
      Printf(w->code,
        "    if (!__r) {\n"
        "      std::string __em = Swig::swig_director_exception_msg();\n"
        "      JL_GC_POP();\n"
        "      throw std::runtime_error(\"Julia director %s threw: \" + __em);\n"
        "    }\n", where);
      /* a non-overridden method returns the no-override sentinel -> fall to base */
      Printf(w->code,
        "    if (__r != Swig::swig_director_global(\"_swig_director_no_override\")) {\n");
      if (is_void) {
        Printf(w->code, "      (void)__r; JL_GC_POP(); return;\n");
      } else {
        Printv(w->code, ret_marshal, NIL);
        Printf(w->code, "      JL_GC_POP(); return c_result;\n");
      }
      Printf(w->code, "    }\n    JL_GC_POP();\n  }\n");
      /* fall-through: pure -> throw, else call the C++ base impl */
      if (pure_virtual) {
        Printf(w->code, "  throw %s;\n", basecall);
      } else {
        if (is_void) Printf(w->code, "  %s::%s(", c_classname, name);
        else Printf(w->code, "  return %s::%s(", c_classname, name);
        int comma = 0;
        for (Parm *p = l; p; p = nextSibling(p)) {
          String *pn = Getattr(p, "name");
          if (!pn || Len(pn) == 0) continue;
          if (comma) Printf(w->code, ", ");
          Printf(w->code, "%s", pn); comma = 1;
        }
        Printf(w->code, ");\n");
      }
      Printf(w->code, "}\n\n");
      Delete(basecall);

      Printf(f_directors_h, "    virtual %s;\n", hdr_sig);
      Wrapper_print(w, f_directors);
      if (!director_classes) director_classes = NewHash();
      Setattr(director_classes, c_classname, "1");
      /* not overridden: the no-override sentinel sends the call to the C++
         base.  One per class and name (overloads/defaultargs share it). */
      if (!enum_seen) enum_seen = NewHash();  /* reuse dedup hash for dispatch names */
      String *dkey = NewStringf("__disp_%s", where);
      if (!Getattr(enum_seen, dkey)) {
        Setattr(enum_seen, dkey, "1");
        Printf(f_jl_body, "%s(self::%s, args...) = _swig_director_no_override\n", dispatch, dtype);
      }
      Delete(dkey);
    }

    Delete(args_build); Delete(ret_marshal); Delete(dispatch); Delete(where); Delete(dtype);
    Delete(impl_sig); Delete(hdr_sig); Delete(qualname); Delete(pclassname);
    DelWrapper(w);
    return SWIG_OK;
  }

  virtual int destructorHandler(Node *) { return SWIG_OK; } /* emitted in classHandler */
  /* public data members: obj.field reads them through _swig_getproperty */
  virtual int membervariableHandler(Node *n) {
    if (!class_jlname) return SWIG_OK;
    memberget_field = Copy(Getattr(n, "sym:name"));
    if (!class_methods) class_methods = NewHash();
    if (!Getattr(class_methods, class_jlname)) Setattr(class_methods, class_jlname, NewHash());
    int r = Language::membervariableHandler(n);
    Delete(memberget_field); memberget_field = 0;
    return r;
  }
  virtual int staticmembervariableHandler(Node *) { return SWIG_OK; }
  virtual int globalvariableHandler(Node *) { return SWIG_OK; }
};

static Language *new_swig_julia() { return new JULIA(); }
extern "C" Language *swig_julia(void) { return new_swig_julia(); }
