// std::pair <-> 2-element list

%include <std_common.i>
%include <ruststdcommon.swg>

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
    struct traits_asptr<std::pair<T, U> > {
      typedef std::pair<T, U> value_type;

      static int asptr(SwigRV* obj, value_type **val) {
        if (!obj) return SWIG_ERROR;
        if (obj->kind == SWIG_RV_LIST) {
          if (obj->items.size() != 2) return SWIG_ERROR;
          if (!val) return swig::check<T>(obj->items[0]) && swig::check<U>(obj->items[1]) ? SWIG_OK : SWIG_ERROR;
          value_type* p = new value_type();
          if (!SWIG_IsOK(swig::asval(obj->items[0], &p->first)) || !SWIG_IsOK(swig::asval(obj->items[1], &p->second))) {
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
    struct traits_from<std::pair<T, U> > {
      static SwigRV* from(const std::pair<T, U>& val) {
        SwigRV* r = SWIG_Rust_NewList(2);
        r->items[0] = swig::from(val.first);
        r->items[1] = swig::from(val.second);
        if (r->items[0] && r->items[1]) return r;
        delete r;
        return 0;
      }
    };
  }
%}

/* Declaration only: values cross through the traits above */
namespace std {
  template <class T1, class T2> struct pair {};
}
