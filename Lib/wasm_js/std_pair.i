// std::pair: from/to a JS Array of exactly two elements, or from a wrapped pair

%include <std_common.i>

%fragment("StdPairTraits","header",fragment="StdTraits")
{
  namespace swig {
    template <class T, class U>
    struct traits<std::pair<T, U> > {
      typedef pointer_category category;
      static const char* type_name() {
        static std::string n = "std::pair< " + std::string(swig::type_name<T>()) + ","
          + std::string(swig::type_name<U>()) + " >";
        return n.c_str();
      }
    };

    template <class T, class U>
    struct traits_asptr<std::pair<T, U> > {
      typedef std::pair<T, U> value_type;
      static int asptr(EM_VAL obj, value_type **val) {
        if (!obj) return SWIG_ERROR;
        emscripten::val v = SWIG_WASMJS_Borrow(obj);
        if (!v.isArray()) {
          swig_type_info *descriptor = swig::type_info<value_type>();
          void *p = 0;
          if (!descriptor || !SWIG_IsOK(SWIG_ConvertPtr(obj, &p, descriptor, 0))) return SWIG_ERROR;
          if (val) *val = static_cast<value_type*>(p);
          return SWIG_OLDOBJ;
        }
        if (v["length"].as<unsigned>() != 2) return SWIG_ERROR;
        emscripten::val first = v[0];
        emscripten::val second = v[1];
        if (!val) {
          return swig::check<T>(first.as_handle()) && swig::check<U>(second.as_handle())
            ? SWIG_OK : SWIG_ERROR;
        }
        value_type *p = new value_type();
        if (!SWIG_IsOK(swig::asval(first.as_handle(), &p->first))
            || !SWIG_IsOK(swig::asval(second.as_handle(), &p->second))) {
          delete p;
          return SWIG_ERROR;
        }
        *val = p;
        return SWIG_NEWOBJ;
      }
    };

    template <class T, class U>
    struct traits_from<std::pair<T, U> > {
      static EM_VAL from(const std::pair<T, U>& val) {
        EM_VAL first = swig::from(val.first);
        if (!first) return 0;
        EM_VAL second = swig::from(val.second);
        if (!second) {
          emscripten::val::take_ownership(first);
          return 0;
        }
        emscripten::val arr = emscripten::val::array();
        arr.call<void>("push", emscripten::val::take_ownership(first));
        arr.call<void>("push", emscripten::val::take_ownership(second));
        return arr.release_ownership();
      }
    };
  }
}

namespace std {
  template <typename T1, typename T2> struct pair {
    %fragment(SWIG_Traits_frag(std::pair< T1, T2 >), "header",
              fragment=SWIG_Traits_frag(T1), fragment=SWIG_Traits_frag(T2), fragment="StdPairTraits") {}
    T1 first;
    T2 second;
  };
}
