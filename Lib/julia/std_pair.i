// std::pair <-> Julia 2-tuple

%include <std_common.i>

%fragment("StdPairTraits","header",fragment="StdTraits",fragment="SwigJlRuntime")
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
    struct traits_asptr<std::pair<T, U> > {
      typedef std::pair<T, U> value_type;

      // Both elements of a 2-tuple (val==0: check only)
      static bool fill(jl_value_t* obj, value_type* val) {
        jl_value_t *a = 0, *b = 0;
        bool ok = true;
        JL_GC_PUSH2(&a, &b);
        try {
          a = jl_fieldref(obj, 0);
          b = jl_fieldref(obj, 1);
          if (val) {
            ok = SWIG_IsOK(swig::asval(a, &val->first)) && SWIG_IsOK(swig::asval(b, &val->second));
          } else {
            ok = swig::check<T>(a) && swig::check<U>(b);
          }
        } catch (...) {
          ok = false;
        }
        JL_GC_POP();
        return ok;
      }

      static int asptr(jl_value_t* obj, value_type **val) {
        if (!obj) return SWIG_ERROR;
        if (jl_is_tuple(obj)) {
          if (jl_nfields(obj) != 2) return SWIG_ERROR;
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
        if (descriptor && SWIG_IsOK(SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0))) {
          if (val) *val = p;
          return SWIG_OLDOBJ;
        }
        return SWIG_ERROR;
      }
    };

    template <class T, class U>
    struct traits_from<std::pair<T, U> > {
      static jl_value_t* from(const std::pair<T, U>& val) {
        jl_value_t *a = 0, *b = 0, *r = 0;
        JL_GC_PUSH2(&a, &b);
        try {
          a = swig::from(val.first);
          b = a ? swig::from(val.second) : 0;
          if (b) r = swig_jl_make_tuple2(a, b);
        } catch (...) {
          r = 0;
        }
        JL_GC_POP();
        return r;
      }
    };
  }
%}

namespace std {
  template <typename T1, typename T2> struct pair {
    T1 first;
    T2 second;
  };
}
