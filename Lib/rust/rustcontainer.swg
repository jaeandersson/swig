/* rustcontainer.swg -- std sequence traits over SwigRV.
 *
 * A sequence converts from a list (element by element), a dense numeric
 * vector (each entry as a numeric scalar of the vector's element type; an
 * empty one if such a scalar converts) or a wrapped sequence.  It returns as
 * a numeric column of f64, i64 or bool when the element type is arithmetic,
 * else as a list.  Every element is checked, also on the check-only path.
 */

%include <ruststdcommon.swg>

%fragment("RustSeqTraits","header",fragment="StdTraits")
{
#include <type_traits>
namespace swig {
  // Append a converted element; false on failure
  template <class T>
  struct rust_seq_append {
    template <class Seq>
    static bool append(Seq* seq, SwigRV* obj) {
      typename Seq::iterator it = seq->insert(seq->end(), T());
      return SWIG_IsOK(swig::asval(obj, &*it));
    }
  };
  template <>
  struct rust_seq_append<bool> {
    template <class Seq>
    static bool append(Seq* seq, SwigRV* obj) {
      bool v;
      if (!SWIG_IsOK(swig::asval(obj, &v))) return false;
      seq->insert(seq->end(), v);
      return true;
    }
  };

  // Returned sequence: a numeric column for arithmetic T, else a list (0 if an element fails)
  template <class T, bool arith = std::is_arithmetic<T>::value>
  struct rust_seq_from {
    template <class Seq>
    static SwigRV* from(const Seq& seq) {
      int elem = std::is_same<T, bool>::value ? SWIG_RV_BOOL : std::is_floating_point<T>::value ? SWIG_RV_F64 : SWIG_RV_I64;
      SwigRV* r = SWIG_Rust_NewNum(elem, seq.size(), 1);
      size_t i = 0;
      for (typename Seq::const_iterator it = seq.begin(); it != seq.end(); ++it, ++i) {
        if (elem == SWIG_RV_F64) r->f64[i] = static_cast<double>(*it);
        else r->i64[i] = static_cast<long long>(*it);
      }
      return r;
    }
  };
  template <class T>
  struct rust_seq_from<T, false> {
    template <class Seq>
    static SwigRV* from(const Seq& seq) {
      SwigRV* r = SWIG_Rust_NewList(seq.size());
      size_t i = 0;
      for (typename Seq::const_iterator it = seq.begin(); it != seq.end(); ++it, ++i) {
        r->items[i] = swig::from<T>(*it);
        if (!r->items[i]) {
          delete r;
          return 0;
        }
      }
      return r;
    }
  };
}
}

%fragment("StdSequenceTraits","header",fragment="RustSeqTraits")
{
namespace swig {
  template <class Seq, class T = typename Seq::value_type >
  struct traits_asptr_stdseq {
    typedef Seq sequence;
    typedef T value_type;

    // One element (seq==0: check only)
    static bool add(sequence* seq, SwigRV* e) {
      return seq ? rust_seq_append<value_type>::append(seq, e) : swig::check<value_type>(e);
    }

    // Elements of a list or a dense numeric vector (seq==0: check only)
    static bool fill(SwigRV* obj, sequence* seq) {
      if (obj->kind == SWIG_RV_LIST) {
        for (size_t i = 0; i < obj->items.size(); ++i) {
          if (!add(seq, obj->items[i])) return false;
        }
        return true;
      }
      SwigRV e;
      e.kind = SWIG_RV_NUM;
      e.elem = obj->elem;
      e.rows = e.cols = 1;
      if (e.elem == SWIG_RV_F64) e.f64.resize(1); else e.i64.resize(1);
      long long n = obj->numel();
      if (n == 0) return swig::check<value_type>(&e);
      for (long long i = 0; i < n; ++i) {
        if (e.elem == SWIG_RV_F64) e.f64[0] = obj->f64[i]; else e.i64[0] = obj->i64[i];
        if (!add(seq, &e)) return false;
      }
      return true;
    }

    static int asptr(SwigRV* obj, sequence **seq) {
      if (!obj) return SWIG_ERROR;
      if (obj->kind == SWIG_RV_LIST || SWIG_Rust_IsVector(obj)) {
        if (!seq) return fill(obj, 0) ? SWIG_OK : SWIG_ERROR;
        sequence* p = new sequence();
        if (!fill(obj, p)) {
          delete p;
          return SWIG_ERROR;
        }
        *seq = p;
        return SWIG_NEWOBJ;
      }
      sequence *p = 0;
      swig_type_info *descriptor = swig::type_info<sequence>();
      if (descriptor && SWIG_IsOK(SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0)) && p) {
        if (seq) *seq = p;
        return SWIG_OLDOBJ;
      }
      return SWIG_ERROR;
    }
  };

  template <class Seq, class T = typename Seq::value_type >
  struct traits_from_stdseq {
    static SwigRV* from(const Seq& seq) {
      return rust_seq_from<T>::from(seq);
    }
  };
}
}
