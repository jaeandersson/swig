// Sequences: from a JS Array, typed array or wrapped sequence; to a JS Array of swig::from(element)

%include <std_common.i>

/* JS class of a wrapped sequence type, registered by %wasm_vec; 0 if none */
%fragment("WasmJSClassName","header") {
namespace swig {
  template <class Seq> struct wasm_js_class_name {
    static const char* get() { return 0; }
  };
}
}

%fragment("StdSequenceTraits","header",fragment="StdTraits",fragment="WasmJSClassName",fragment="SWIG_WASMJS_Number")
{
%#include <type_traits>
namespace swig {
  /* Element-wise conversion of a JS Array or typed array; specialize to change element policy */
  template <class Seq, class T = typename Seq::value_type>
  struct JSArrayProtocol {
    static bool assign(const emscripten::val& arr, Seq* seq) {
      unsigned n = arr["length"].as<unsigned>();
      try {
        for (unsigned i = 0; i < n; ++i) {
          emscripten::val el = arr[i];
          seq->insert(seq->end(), swig::as<T>(el.as_handle()));
        }
      } catch (std::exception&) {
        return false;
      }
      return true;
    }
    static bool check(const emscripten::val& arr) {
      unsigned n = arr["length"].as<unsigned>();
      for (unsigned i = 0; i < n; ++i) {
        emscripten::val el = arr[i];
        if (!swig::check<T>(el.as_handle())) return false;
      }
      return true;
    }
  };

  /* A typed array (not a DataView); `number`: one of Number elements */
  inline bool wasm_js_is_typed_array(const emscripten::val& v, bool* number) {
    if (!emscripten::val::global("ArrayBuffer").call<bool>("isView", v)) return false;
    std::string c = v["constructor"]["name"].as<std::string>();
    *number = c != "BigInt64Array" && c != "BigUint64Array";
    return c != "DataView";
  }

  /* Bulk conversion of a typed array of Numbers to arithmetic (not bool) elements */
  template <class T, bool = std::is_arithmetic<T>::value && !std::is_same<T, bool>::value>
  struct JSTypedArray {
    static const bool bulk = false;
    template <class Seq> static bool assign(const emscripten::val&, Seq*) { return false; }
  };
  template <class T>
  struct JSTypedArray<T, true> {
    static const bool bulk = true;
    /* seq==0: check only */
    template <class Seq> static bool assign(const emscripten::val& v, Seq* seq) {
      std::vector<double> d = emscripten::convertJSArrayToNumberVector<double>(v);
      for (size_t i = 0; i < d.size(); ++i) {
        if (!SWIG_WASMJS_NumberFits<T>(d[i])) return false;
      }
      if (seq) {
        for (size_t i = 0; i < d.size(); ++i) seq->insert(seq->end(), static_cast<T>(d[i]));
      }
      return true;
    }
  };

  template <class Seq, class T = typename Seq::value_type >
  struct traits_asptr_stdseq {
    typedef Seq sequence;
    typedef T value_type;

    static int asptr(EM_VAL obj, sequence **seq) {
      if (!obj) return SWIG_ERROR;
      emscripten::val v = SWIG_WASMJS_Borrow(obj);
      if (SWIG_WASMJS_IsWrapped(v)) {
        void *p = 0;
        swig_type_info *descriptor = swig::type_info<sequence>();
        if (descriptor) {
          if (!SWIG_IsOK(SWIG_ConvertPtr(obj, &p, descriptor, 0))) return SWIG_ERROR;
        } else {
          const char* nm = wasm_js_class_name<sequence>::get();
          if (!nm || v["constructor"]["name"].as<std::string>() != nm) return SWIG_ERROR;
          p = reinterpret_cast<void*>(v["_ptr"].as<uintptr_t>());
        }
        if (seq) *seq = static_cast<sequence*>(p);
        return SWIG_OLDOBJ;
      }
      bool number = false;
      if (!v.isArray() && !wasm_js_is_typed_array(v, &number)) return SWIG_ERROR;
      bool typed = number && JSTypedArray<value_type>::bulk;
      if (!seq) {
        bool ok = typed ? JSTypedArray<value_type>::assign(v, static_cast<sequence*>(0))
                        : JSArrayProtocol<sequence, value_type>::check(v);
        return ok ? SWIG_OK : SWIG_ERROR;
      }
      sequence *pseq = new sequence();
      bool ok = typed ? JSTypedArray<value_type>::assign(v, pseq)
                      : JSArrayProtocol<sequence, value_type>::assign(v, pseq);
      if (!ok) {
        delete pseq;
        return SWIG_ERROR;
      }
      *seq = pseq;
      return SWIG_NEWOBJ;
    }
  };

  template <class Seq, class T = typename Seq::value_type >
  struct traits_from_stdseq {
    typedef Seq sequence;
    typedef T value_type;
    typedef typename Seq::size_type size_type;
    typedef typename sequence::const_iterator const_iterator;

    static EM_VAL from(const sequence& seq) {
      emscripten::val arr = emscripten::val::array();
      for (const_iterator it = seq.begin(); it != seq.end(); ++it) {
        EM_VAL e = swig::from<value_type>(*it);
        if (!e) return 0;
        arr.call<void>("push", emscripten::val::take_ownership(e));
      }
      return arr.release_ownership();
    }
  };
}
}
