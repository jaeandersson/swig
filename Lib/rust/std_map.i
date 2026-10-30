// std::map <-> dict (keys cross as strings)

%include <std_common.i>
%include <ruststdcommon.swg>

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

      // Every key and value of a dict (m==0: check only)
      static bool fill(SwigRV* obj, map_type* m) {
        SwigRV k;
        k.kind = SWIG_RV_STR;
        for (size_t i = 0; i < obj->items.size(); ++i) {
          k.str = obj->keys[i];
          K key;
          if (!SWIG_IsOK(swig::asval(&k, m ? &key : (K*)0))) return false;
          if (m ? !SWIG_IsOK(swig::asval(obj->items[i], &(*m)[key])) : !swig::check<T>(obj->items[i])) return false;
        }
        return true;
      }

      static int asptr(SwigRV* obj, map_type **val) {
        if (!obj) return SWIG_ERROR;
        if (obj->kind == SWIG_RV_DICT) {
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

    // 0 if a key does not return as a string or a value fails
    template <class K, class T, class C, class A>
    struct traits_from<std::map<K, T, C, A> > {
      typedef std::map<K, T, C, A> map_type;
      static SwigRV* from(const map_type& map) {
        SwigRV* d = SWIG_Rust_NewDict();
        for (typename map_type::const_iterator it = map.begin(); it != map.end(); ++it) {
          SwigRV* k = swig::from(it->first);
          SwigRV* v = k && k->kind == SWIG_RV_STR ? swig::from(it->second) : 0;
          if (v) d->keys.push_back(k->str);
          delete k;
          if (!v) {
            delete d;
            return 0;
          }
          d->items.push_back(v);
        }
        return d;
      }
    };
  }
%}

/* Declaration only: values cross through the traits above */
namespace std {
  template <class K, class T> class map {};
}
