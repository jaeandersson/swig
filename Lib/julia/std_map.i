// std::map <-> Julia AbstractDict (returned as a Dict{Any,Any})

%include <std_common.i>
%include <juliacontainer.swg>

%fragment("StdMapTraits","header",fragment="StdTraits",fragment="SwigJlRuntime",fragment="JuliaArrayTraits")
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

    inline bool julia_is_dict(jl_value_t* obj) {
      jl_value_t* adt = jl_get_global(jl_base_module, jl_symbol("AbstractDict"));
      return adt && jl_isa(obj, adt);
    }

    template <class K, class T, class C, class A>
    struct traits_asptr<std::map<K, T, C, A> > {
      typedef std::map<K, T, C, A> map_type;

      // Every key and value of a Julia dict (m==0: check only)
      static bool fill(jl_value_t* obj, map_type* m) {
        jl_value_t *keys = 0, *k = 0, *v = 0;
        bool ok = true;
        JL_GC_PUSH3(&keys, &k, &v);
        try {
          keys = jl_call1(jl_get_function(jl_base_module, "keys"), obj);
          if (keys) keys = jl_call1(jl_get_function(jl_base_module, "collect"), keys);
          ok = keys && jl_is_array(keys);
          size_t n = ok ? swig_jl_array_len(keys) : 0;
          for (size_t i = 0; ok && i < n; ++i) {
            k = swig_jl_aref(keys, i);
            K key;
            if (!SWIG_IsOK(swig::asval(k, m ? &key : (K*)0))) {
              ok = false;
              break;
            }
            v = jl_call2(jl_get_function(jl_base_module, "getindex"), obj, k);
            if (!v) {
              ok = false;
            } else if (m) {
              ok = SWIG_IsOK(swig::asval(v, &(*m)[key]));
            } else {
              ok = swig::check<T>(v);
            }
          }
        } catch (...) {
          ok = false;
        }
        JL_GC_POP();
        return ok;
      }

      static int asptr(jl_value_t* obj, map_type **val) {
        if (!obj) return SWIG_ERROR;
        if (julia_is_dict(obj)) {
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
        if (descriptor && SWIG_IsOK(SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0))) {
          if (val) *val = p;
          return SWIG_OLDOBJ;
        }
        return SWIG_ERROR;
      }
    };

    template <class K, class T, class C, class A>
    struct traits_from<std::map<K, T, C, A> > {
      typedef std::map<K, T, C, A> map_type;
      static jl_value_t* from(const map_type& map) {
        jl_value_t *d = 0, *k = 0, *v = 0;
        bool ok = true;
        JL_GC_PUSH3(&d, &k, &v);
        try {
          jl_value_t* setindex = jl_get_function(jl_base_module, "setindex!");
          d = jl_call0(jl_get_function(jl_base_module, "Dict"));
          ok = d != 0;
          for (typename map_type::const_iterator it = map.begin(); ok && it != map.end(); ++it) {
            k = swig::from(it->first);
            v = k ? swig::from(it->second) : 0;
            ok = v && jl_call3(setindex, d, v, k);
          }
        } catch (...) {
          ok = false;
        }
        if (ok) {
          // Dict{Any,Any} narrowed to the key and value types when every entry has them
          k = jl_call2(jl_get_function(jl_base_module, "convert"), julia_eltype<map_type>::get(), d);
          if (k) d = k; else jl_exception_clear();
        }
        JL_GC_POP();
        return ok ? d : 0;
      }
    };

    template <class K, class T, class C, class A>
    struct julia_eltype<std::map<K, T, C, A> > {
      static jl_value_t* get() {
        return jl_apply_type2(jl_get_global(jl_base_module, jl_symbol("Dict")), julia_eltype<K>::get(), julia_eltype<T>::get());
      }
    };
  }
%}

namespace std {
  template <typename Key, typename T> class map {};
}
