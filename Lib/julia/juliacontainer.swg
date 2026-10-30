/* juliacontainer.swg -- std sequence traits over Julia arrays.
 *
 * A sequence converts from any Julia Array (all elements, linear order) or a
 * wrapped sequence; it returns as a 1-D Array.  Arithmetic elements use the
 * array's raw data when its element type is Float64, Int64 or Bool, with the
 * SWIG_AsVal rules; other arrays convert element by element (swig::asval).
 * Every element is checked, also on the check-only path (seq==0).
 */

%include <juliastdcommon.swg>

%fragment("JuliaArrayTraits","header",fragment="StdTraits",fragment="SwigJlRuntime",
          fragment="SWIG_jl_double_to")
{
%#include <stdint.h>
namespace swig {
  // Element policy of julia arrays; the default boxes every element
  struct julia_array_traits_none {
    // Element type of the returned Array, or 0 for Vector{Any}
    static jl_value_t* eltype() { return 0; }
    // Convert all n elements of a native array (seq==0: check only); -1 if not native
    template <class Seq>
    static int read(jl_value_t*, size_t, Seq*) { return -1; }
    // Fill an Array of eltype() with seq
    template <class Seq>
    static void write(jl_value_t*, const Seq&) {}
  };

  template <class T>
  struct julia_array_traits : julia_array_traits_none {};

  // Arithmetic: Float64, Int64 and Bool arrays (bool: Bool only) read in place; returned as Float64, Int64 or Bool
  template <class T>
  struct julia_arith_array_traits : julia_array_traits_none {
    static jl_value_t* eltype() {
      if (std::is_same<T, bool>::value) return (jl_value_t*)jl_bool_type;
      if (std::is_floating_point<T>::value) return (jl_value_t*)jl_float64_type;
      return (jl_value_t*)jl_int64_type;
    }
    template <class S, class Seq>
    static int read_as(jl_value_t* a, size_t n, Seq* seq) {
      const S* d = n ? swig_jl_array_ptr<S>(a) : 0;
      for (size_t i = 0; i < n; ++i) {
        T v;
        if (!SWIG_IsOK(SWIG_jl_double_to<T>(static_cast<double>(d[i]), seq ? &v : 0))) return 0;
        if (seq) seq->insert(seq->end(), v);
      }
      return 1;
    }
    template <class Seq>
    static int read(jl_value_t* a, size_t n, Seq* seq) {
      jl_value_t* et = (jl_value_t*)jl_array_eltype(a);
      if (std::is_same<T, bool>::value && et != (jl_value_t*)jl_bool_type) return -1;
      if (et == (jl_value_t*)jl_float64_type) return read_as<double>(a, n, seq);
      if (et == (jl_value_t*)jl_int64_type) return read_as<int64_t>(a, n, seq);
      if (et == (jl_value_t*)jl_bool_type) return read_as<uint8_t>(a, n, seq);
      return -1;
    }
    template <class Seq>
    static void write(jl_value_t* a, const Seq& seq) {
      if (seq.empty()) return;
      size_t i = 0;
      if (std::is_same<T, bool>::value) {
        uint8_t* d = swig_jl_array_ptr<uint8_t>(a);
        for (typename Seq::const_iterator it = seq.begin(); it != seq.end(); ++it, ++i) d[i] = *it ? 1 : 0;
      } else if (std::is_floating_point<T>::value) {
        double* d = swig_jl_array_ptr<double>(a);
        for (typename Seq::const_iterator it = seq.begin(); it != seq.end(); ++it, ++i) d[i] = static_cast<double>(*it);
      } else {
        int64_t* d = swig_jl_array_ptr<int64_t>(a);
        for (typename Seq::const_iterator it = seq.begin(); it != seq.end(); ++it, ++i) d[i] = static_cast<int64_t>(*it);
      }
    }
  };

  template <> struct julia_array_traits<bool> : julia_arith_array_traits<bool> {};
  template <> struct julia_array_traits<signed char> : julia_arith_array_traits<signed char> {};
  template <> struct julia_array_traits<unsigned char> : julia_arith_array_traits<unsigned char> {};
  template <> struct julia_array_traits<short> : julia_arith_array_traits<short> {};
  template <> struct julia_array_traits<unsigned short> : julia_arith_array_traits<unsigned short> {};
  template <> struct julia_array_traits<int> : julia_arith_array_traits<int> {};
  template <> struct julia_array_traits<unsigned int> : julia_arith_array_traits<unsigned int> {};
  template <> struct julia_array_traits<long> : julia_arith_array_traits<long> {};
  template <> struct julia_array_traits<unsigned long> : julia_arith_array_traits<unsigned long> {};
  template <> struct julia_array_traits<long long> : julia_arith_array_traits<long long> {};
  template <> struct julia_array_traits<unsigned long long> : julia_arith_array_traits<unsigned long long> {};
  template <> struct julia_array_traits<float> : julia_arith_array_traits<float> {};
  template <> struct julia_array_traits<double> : julia_arith_array_traits<double> {};

  // std::string: returned as Vector{String}
  template <>
  struct julia_array_traits<std::string> : julia_array_traits_none {
    static jl_value_t* eltype() { return (jl_value_t*)jl_string_type; }
    template <class Seq>
    static void write(jl_value_t* a, const Seq& seq) {
      jl_value_t* s = 0;
      JL_GC_PUSH1(&s);
      try {
        size_t i = 0;
        for (typename Seq::const_iterator it = seq.begin(); it != seq.end(); ++it, ++i) {
          s = jl_pchar_to_string(it->data(), it->size());
          swig_jl_aset(a, i, s);
        }
      } catch (...) {
        JL_GC_POP();
        throw;
      }
      JL_GC_POP();
    }
  };

  // Julia type a returned T has: native arrays, else the proxy registered under its type name, else Any
  template <class T>
  struct julia_eltype {
    static jl_value_t* get() {
      jl_value_t* e = julia_array_traits<T>::eltype();
      if (e) return e;
      std::string n = swig::type_name<T>();
      std::string::size_type c = n.rfind("::");
      if (c != std::string::npos) n = n.substr(c + 2);
      std::map<std::string, jl_value_t*>::iterator it = swig_jl_proxy_types().find(n);
      return it == swig_jl_proxy_types().end() ? (jl_value_t*)jl_any_type : it->second;
    }
  };
  template <class T, class A>
  struct julia_eltype<std::vector<T, A> > {
    static jl_value_t* get() { return jl_apply_array_type(julia_eltype<T>::get(), 1); }
  };

  // Append a converted element; false on failure
  template <class T>
  struct julia_seq_append {
    template <class Seq>
    static bool append(Seq* seq, jl_value_t* obj) {
      typename Seq::iterator it = seq->insert(seq->end(), T());
      return SWIG_IsOK(swig::asval(obj, &*it));
    }
  };
  template <>
  struct julia_seq_append<bool> {
    template <class Seq>
    static bool append(Seq* seq, jl_value_t* obj) {
      bool v;
      if (!SWIG_IsOK(swig::asval(obj, &v))) return false;
      seq->insert(seq->end(), v);
      return true;
    }
  };
}
}

%fragment("StdSequenceTraits","header",fragment="JuliaArrayTraits")
{
namespace swig {
  template <class Seq, class T = typename Seq::value_type >
  struct traits_asptr_stdseq {
    typedef Seq sequence;
    typedef T value_type;
    typedef julia_array_traits<T> array_traits;

    // Elements of a Julia Array (seq==0: check only)
    static bool fill(jl_value_t* obj, sequence* seq) {
      jl_value_t* e = 0;
      bool ok = true;
      JL_GC_PUSH1(&e);
      try {
        size_t n = swig_jl_array_len(obj);
        int nat = array_traits::read(obj, n, seq);
        if (nat >= 0) {
          ok = nat > 0;
        } else {
          for (size_t i = 0; ok && i < n; ++i) {
            e = swig_jl_aref(obj, i);
            ok = seq ? julia_seq_append<value_type>::append(seq, e) : swig::check<value_type>(e);
          }
        }
      } catch (...) {
        ok = false;
      }
      JL_GC_POP();
      return ok;
    }

    static int asptr(jl_value_t* obj, sequence **seq) {
      if (!obj) return SWIG_ERROR;
      if (jl_is_array(obj)) {
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
      if (descriptor && SWIG_IsOK(SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0))) {
        if (seq) *seq = p;
        return SWIG_OLDOBJ;
      }
      return SWIG_ERROR;
    }
  };

  // A 1-D Array (julia_array_traits element type, else Vector{Any}); 0 if an element fails
  template <class Seq, class T = typename Seq::value_type >
  struct traits_from_stdseq {
    typedef Seq sequence;
    typedef T value_type;
    typedef julia_array_traits<T> array_traits;

    static jl_value_t* from(const sequence& seq) {
      jl_value_t* et = array_traits::eltype();
      jl_value_t* r = 0;
      jl_value_t* e = 0;
      bool ok = true;
      JL_GC_PUSH2(&r, &e);
      try {
        jl_value_t* atype = jl_apply_array_type(et ? et : (jl_value_t*)jl_any_type, 1);
        r = (jl_value_t*)jl_alloc_array_1d(atype, seq.size());
        if (et) {
          array_traits::write(r, seq);
        } else {
          size_t i = 0;
          for (typename sequence::const_iterator it = seq.begin(); ok && it != seq.end(); ++it, ++i) {
            e = swig::from<value_type>(*it);
            if (e) swig_jl_aset(r, i, e);
            else ok = false;
          }
        }
      } catch (...) {
        ok = false;
      }
      if (ok && !et) {
        // Vector{Any} narrowed to the element type when every element has it
        jl_value_t* nt = julia_eltype<value_type>::get();
        if (nt != (jl_value_t*)jl_any_type) r = swig_jl_narrow(r, nt);
      }
      JL_GC_POP();
      return ok ? r : 0;
    }
  };
}
}
