// std::map: string keys from/to a plain JS object (own enumerable properties); other keys only wrapped

%include <wasm_jscontainer.swg>

%fragment("StdMapTraits","header",fragment="StdSequenceTraits")
{
  namespace swig {
    template <class K, class T, class C, class A>
    struct traits<std::map<K, T, C, A> > {
      typedef pointer_category category;
      static const char* type_name() {
        static std::string n = "std::map< " + std::string(swig::type_name<K>()) + ","
          + std::string(swig::type_name<T>()) + " >";
        return n.c_str();
      }
    };

    template <class K, class T, class C, class A>
    struct traits_asptr<std::map<K, T, C, A> > {
      typedef std::map<K, T, C, A> map_type;
      static int asptr(EM_VAL obj, map_type **val) {
        swig_type_info *descriptor = swig::type_info<map_type>();
        if (!obj || !descriptor) return SWIG_ERROR;
        void *p = 0;
        int res = SWIG_ConvertPtr(obj, &p, descriptor, 0);
        if (SWIG_IsOK(res) && val) *val = static_cast<map_type*>(p);
        return res;
      }
    };

    template <class K, class T, class C, class A>
    struct traits_from<std::map<K, T, C, A> > {
      typedef std::map<K, T, C, A> map_type;
      static EM_VAL from(const map_type& map) {
        swig_type_info *descriptor = swig::type_info<map_type>();
        if (!descriptor || !descriptor->clientdata) return 0;
        return SWIG_NewPointerObj(new map_type(map), descriptor, SWIG_POINTER_OWN);
      }
    };

    /* A plain object: not null, not an array or typed array, not a wrapped object */
    inline bool wasm_js_is_plain_object(const emscripten::val& v) {
      return v.typeOf().as<std::string>() == "object" && !v.isNull() && !v.isArray()
        && !v.hasOwnProperty("_ptr") && !emscripten::val::global("ArrayBuffer").call<bool>("isView", v);
    }

    template <class T, class C, class A>
    struct traits_asptr<std::map<std::string, T, C, A> > {
      typedef std::map<std::string, T, C, A> map_type;
      static int asptr(EM_VAL obj, map_type **val) {
        if (!obj) return SWIG_ERROR;
        emscripten::val v = SWIG_WASMJS_Borrow(obj);
        if (!wasm_js_is_plain_object(v)) {
          swig_type_info *descriptor = swig::type_info<map_type>();
          void *p = 0;
          if (!descriptor || !SWIG_IsOK(SWIG_ConvertPtr(obj, &p, descriptor, 0))) return SWIG_ERROR;
          if (val) *val = static_cast<map_type*>(p);
          return SWIG_OLDOBJ;
        }
        emscripten::val keys = emscripten::val::global("Object").call<emscripten::val>("keys", v);
        unsigned n = keys["length"].as<unsigned>();
        if (!val) {
          for (unsigned i = 0; i < n; ++i) {
            emscripten::val e = v[keys[i]];
            if (!swig::check<T>(e.as_handle())) return SWIG_ERROR;
          }
          return SWIG_OK;
        }
        map_type *p = new map_type();
        try {
          for (unsigned i = 0; i < n; ++i) {
            emscripten::val k = keys[i];
            emscripten::val e = v[k];
            (*p)[k.as<std::string>()] = swig::as<T>(e.as_handle());
          }
        } catch (std::exception&) {
          delete p;
          return SWIG_ERROR;
        }
        *val = p;
        return SWIG_NEWOBJ;
      }
    };

    template <class T, class C, class A>
    struct traits_from<std::map<std::string, T, C, A> > {
      typedef std::map<std::string, T, C, A> map_type;
      static EM_VAL from(const map_type& map) {
        emscripten::val obj = emscripten::val::object();
        for (typename map_type::const_iterator it = map.begin(); it != map.end(); ++it) {
          EM_VAL e = swig::from<T>(it->second);
          if (!e) return 0;
          obj.set(it->first, emscripten::val::take_ownership(e));
        }
        return obj.release_ownership();
      }
    };
  }
}

namespace std {
  template <typename Key, typename T> class map {
    %fragment(SWIG_Traits_frag(std::map< Key, T >), "header",
              fragment=SWIG_Traits_frag(Key), fragment=SWIG_Traits_frag(T), fragment="StdMapTraits") {}
  };
}
