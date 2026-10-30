/* -----------------------------------------------------------------------------
 * javagueststl.i
 *
 * STL for guest-object mode (javaguest.swg), in place of stl.i: std::string,
 * std::vector, std::map and std::pair cross as Java values through swig::traits.
 *   std::string  <-> String
 *   std::vector  <-  double[], long[], int[], boolean[], Object[] or java.util.List;
 *                ->  double[] (floating), long[] (integral), boolean[] (bool), an array of
 *                    those for vectors of them, else java.util.List
 *   std::map     <-> java.util.Map
 *   std::pair    <-> java.util.Map.Entry
 * Primitive arrays reach numbers and bools directly (double[] only a floating-point
 * vector, boolean[] only a bool vector); other element types take the boxed values.
 * ----------------------------------------------------------------------------- */

%include <javagueststdcommon.swg>
%include <std_string.i>

%fragment("StdSequenceTraits","header",fragment="StdTraits")
%{
#include <type_traits>
namespace swig {
  // Primitive arrays an element type takes without boxing: 1 double[], 2 long[]/int[], 4 boolean[]
  template <class T, bool = std::is_arithmetic<T>::value>
  struct java_guest_prim {
    static const int kinds = 0;
    template <class Seq, class V> static void copy(Seq*, const std::vector<V>&) {}
  };
  template <class T>
  struct java_guest_prim<T, true> {
    static const int kinds = std::is_same<T, bool>::value ? 4 : std::is_floating_point<T>::value ? 3 : 2;
    template <class Seq, class V> static void copy(Seq* seq, const std::vector<V>& v) {
      for (size_t i = 0; i < v.size(); ++i) seq->insert(seq->end(), static_cast<T>(v[i]));
    }
  };

  // A sequence of T as a primitive array, or an array of them; 0 for neither
  template <class T, bool = std::is_arithmetic<T>::value>
  struct java_guest_array {
    static jclass cls() { return 0; }
    template <class Seq> static jobject from(const Seq&) { return 0; }
  };
  template <class T>
  struct java_guest_array<T, true> {
    static jclass cls() {
      const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(SWIG_JavaGuestEnvChecked());
      return std::is_same<T, bool>::value ? j.booleans : std::is_floating_point<T>::value ? j.doubles : j.longs;
    }
    template <class Seq> static jobject from(const Seq& seq) {
      if (std::is_same<T, bool>::value) return SWIG_JavaGuestNewBooleans(std::vector<bool>(seq.begin(), seq.end()));
      if (std::is_floating_point<T>::value) return SWIG_JavaGuestNewDoubles(std::vector<double>(seq.begin(), seq.end()));
      return SWIG_JavaGuestNewLongs(std::vector<long long>(seq.begin(), seq.end()));
    }
  };
  template <class T, class A>
  struct java_guest_array<std::vector<T, A>, false> {
    static jclass cls() { return 0; }
    template <class Seq> static jobject from(const Seq& seq) {
      jclass row = java_guest_array<T>::cls();
      if (!row) return 0;
      jobject r = SWIG_JavaGuestNewArrayOf(row, (jsize)seq.size());
      jsize i = 0;
      for (typename Seq::const_iterator it = seq.begin(); it != seq.end(); ++it, ++i)
        SWIG_JavaGuestArraySet(r, i, java_guest_array<T>::from(*it));
      return r;
    }
  };

  // Append a converted element; false on failure
  template <class T>
  struct java_guest_append {
    template <class Seq>
    static bool append(Seq* seq, jobject obj) {
      typename Seq::iterator it = seq->insert(seq->end(), T());
      return SWIG_IsOK(swig::asval(obj, &*it));
    }
  };
  template <>
  struct java_guest_append<bool> {
    template <class Seq>
    static bool append(Seq* seq, jobject obj) {
      bool v;
      if (!SWIG_IsOK(swig::asval(obj, &v))) return false;
      seq->insert(seq->end(), v);
      return true;
    }
  };

  template <class Seq, class T = typename Seq::value_type >
  struct traits_asptr_stdseq {
    typedef Seq sequence;
    typedef T value_type;

    // One element (seq==0: check only); obj is released
    static bool element(jobject obj, sequence* seq) {
      bool ok = seq ? java_guest_append<value_type>::append(seq, obj) : swig::check<value_type>(obj);
      SWIG_JavaGuestRelease(obj);
      return ok;
    }

    // Elements of a primitive array of kind k
    template <class V>
    static bool prim(const std::vector<V>& v, int k, sequence* seq) {
      if (java_guest_prim<value_type>::kinds & k) {
        if (seq) java_guest_prim<value_type>::copy(seq, v);
        return true;
      }
      if (std::is_arithmetic<value_type>::value) return false;
      for (size_t i = 0; i < v.size(); ++i) {
        if (!element(swig::from<V>(v[i]), seq)) return false;
      }
      return true;
    }

    static bool fill(jobject obj, sequence* seq) {
      if (SWIG_JavaGuestIsDoubles(obj)) return prim(SWIG_JavaGuestDoubles(obj), 1, seq);
      if (SWIG_JavaGuestIsLongs(obj)) return prim(SWIG_JavaGuestLongs(obj), 2, seq);
      if (SWIG_JavaGuestIsBooleans(obj)) return prim(SWIG_JavaGuestBooleans(obj), 4, seq);
      jint n = SWIG_JavaGuestSequenceSize(obj);
      for (jint i = 0; i < n; ++i) {
        if (!element(SWIG_JavaGuestSequenceGet(obj, i), seq)) return false;
      }
      return true;
    }

    static int asptr(jobject obj, sequence **seq) {
      if (!obj) return SWIG_ERROR;
      if (SWIG_JavaGuestIsDoubles(obj) || SWIG_JavaGuestIsLongs(obj) || SWIG_JavaGuestIsBooleans(obj)
          || SWIG_JavaGuestIsSequence(obj)) {
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

  // A primitive array (or an array of them), else a java.util.List
  template <class Seq, class T = typename Seq::value_type >
  struct traits_from_stdseq {
    static jobject from(const Seq& seq) {
      jobject r = java_guest_array<T>::from(seq);
      if (r) return r;
      r = SWIG_JavaGuestListNew((jint)seq.size());
      for (typename Seq::const_iterator it = seq.begin(); it != seq.end(); ++it)
        SWIG_JavaGuestListAppend(r, swig::from<T>(*it));
      return r;
    }
  };
}
%}

%fragment("StdVectorTraits","header",fragment="StdSequenceTraits")
%{
  namespace swig {
    template <class T, class A>
    struct traits<std::vector<T, A> > {
      typedef pointer_category category;
      static const char* type_name() {
        static std::string name = "std::vector<" + std::string(swig::type_name<T>()) + " >";
        return name.c_str();
      }
    };

    template <class T, class A>
    struct traits_asptr<std::vector<T, A> > {
      static int asptr(jobject obj, std::vector<T, A> **vec) {
        return traits_asptr_stdseq<std::vector<T, A> >::asptr(obj, vec);
      }
    };

    template <class T, class A>
    struct traits_from<std::vector<T, A> > {
      static jobject from(const std::vector<T, A>& vec) {
        return traits_from_stdseq<std::vector<T, A> >::from(vec);
      }
    };
  }
%}

%fragment("StdMapTraits","header",fragment="StdTraits")
%{
  namespace swig {
    template <class K, class T, class C, class A>
    struct traits<std::map<K, T, C, A> > {
      typedef pointer_category category;
      static const char* type_name() {
        static std::string name = "std::map<" + std::string(swig::type_name<K>()) + ","
          + std::string(swig::type_name<T>()) + " >";
        return name.c_str();
      }
    };

    template <class K, class T, class C, class A>
    struct traits_asptr<std::map<K, T, C, A> > {
      typedef std::map<K, T, C, A> map_type;

      // Every key and value of a java.util.Map (m==0: check only)
      static bool fill(jobject obj, map_type* m) {
        JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
        jobjectArray keys = SWIG_JavaGuestMapKeys(obj);
        jint n = jenv->GetArrayLength(keys);
        bool ok = true;
        for (jint i = 0; ok && i < n; ++i) {
          jobject k = jenv->GetObjectArrayElement(keys, i);
          K key;
          ok = SWIG_IsOK(swig::asval(k, m ? &key : (K*)0));
          if (ok) {
            jobject v = SWIG_JavaGuestMapGet(obj, k);
            ok = m ? SWIG_IsOK(swig::asval(v, &(*m)[key])) : swig::check<T>(v);
            SWIG_JavaGuestRelease(v);
          }
          SWIG_JavaGuestRelease(k);
        }
        SWIG_JavaGuestRelease(keys);
        return ok;
      }

      static int asptr(jobject obj, map_type **val) {
        if (!obj) return SWIG_ERROR;
        if (SWIG_JavaGuestIsMap(obj)) {
          if (!val) return fill(obj, 0) ? SWIG_OK : SWIG_ERROR;
          map_type* p = new map_type();
          if (!fill(obj, p)) {
            delete p;
            return SWIG_ERROR;
          }
          *val = p;
          return SWIG_NEWOBJ;
        }
        map_type *p = 0;
        swig_type_info *descriptor = swig::type_info<map_type>();
        if (descriptor && SWIG_IsOK(SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0)) && p) {
          if (val) *val = p;
          return SWIG_OLDOBJ;
        }
        return SWIG_ERROR;
      }
    };

    // A java.util.Map in key order
    template <class K, class T, class C, class A>
    struct traits_from<std::map<K, T, C, A> > {
      static jobject from(const std::map<K, T, C, A>& map) {
        jobject r = SWIG_JavaGuestMapNew();
        for (typename std::map<K, T, C, A>::const_iterator it = map.begin(); it != map.end(); ++it)
          SWIG_JavaGuestMapPut(r, swig::from(it->first), swig::from(it->second));
        return r;
      }
    };
  }
%}

%fragment("StdPairTraits","header",fragment="StdTraits")
%{
  namespace swig {
    template <class T, class U>
    struct traits<std::pair<T, U> > {
      typedef pointer_category category;
      static const char* type_name() {
        static std::string name = "std::pair<" + std::string(swig::type_name<T>()) + ","
          + std::string(swig::type_name<U>()) + " >";
        return name.c_str();
      }
    };

    template <class T, class U>
    struct traits_asptr_stdpair {
      typedef std::pair<T, U> value_type;

      // Key and value of a java.util.Map.Entry (val==0: check only)
      static bool fill(jobject obj, value_type* val) {
        jobject a = SWIG_JavaGuestEntryGet(obj, true);
        jobject b = SWIG_JavaGuestEntryGet(obj, false);
        bool ok = val ? SWIG_IsOK(swig::asval(a, &val->first)) && SWIG_IsOK(swig::asval(b, &val->second))
          : swig::check<T>(a) && swig::check<U>(b);
        SWIG_JavaGuestRelease(a);
        SWIG_JavaGuestRelease(b);
        return ok;
      }

      static int asptr(jobject obj, value_type **val) {
        if (!obj) return SWIG_ERROR;
        if (SWIG_JavaGuestIsEntry(obj)) {
          if (!val) return fill(obj, 0) ? SWIG_OK : SWIG_ERROR;
          value_type* p = new value_type();
          if (!fill(obj, p)) {
            delete p;
            return SWIG_ERROR;
          }
          *val = p;
          return SWIG_NEWOBJ;
        }
        value_type *p = 0;
        swig_type_info *descriptor = swig::type_info<value_type>();
        if (descriptor && SWIG_IsOK(SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0)) && p) {
          if (val) *val = p;
          return SWIG_OLDOBJ;
        }
        return SWIG_ERROR;
      }
    };

    template <class T, class U>
    struct traits_asptr<std::pair<T, U> > : traits_asptr_stdpair<T, U> {};

    template <class T, class U>
    struct traits_from<std::pair<T, U> > {
      static jobject from(const std::pair<T, U>& val) {
        return SWIG_JavaGuestNewEntry(swig::from(val.first), swig::from(val.second));
      }
    };
  }
%}

// Declarations only: values cross through the traits above
namespace std {
  template <typename T> class vector {};
  template <typename K, typename T> class map {};
  template <typename T1, typename T2> struct pair {
    T1 first;
    T2 second;
  };
}
