// swig::traits over EM_VAL handles: inputs are borrowed, swig::from returns an owned handle or 0, failures never set the error slot

%fragment("StdTraits","header",fragment="StdTraitsCommon")
{
namespace swig {
  /* Traits that provide the from method */
  template <class Type> struct traits_from_ptr {
    static EM_VAL from(Type *val, int owner = 0) {
      return SWIG_NewPointerObj(val, type_info<Type>(), owner);
    }
  };

  template <class Type> struct traits_from {
    static EM_VAL from(const Type& val) {
      return traits_from_ptr<Type>::from(new Type(val), 1);
    }
  };

  template <class Type> struct traits_from<Type *> {
    static EM_VAL from(Type* val) {
      return traits_from_ptr<Type>::from(val, 0);
    }
  };

  template <class Type> struct traits_from<const Type *> {
    static EM_VAL from(const Type* val) {
      return traits_from_ptr<Type>::from(const_cast<Type*>(val), 0);
    }
  };

  template <class Type>
  inline EM_VAL from(const Type& val) {
    return traits_from<Type>::from(val);
  }

  template <class Type>
  inline EM_VAL from_ptr(Type* val, int owner) {
    return traits_from_ptr<Type>::from(val, owner);
  }

  /* asptr/asval/as/check; a null output pointer means check only */
  template <class Type>
  struct traits_asptr {
    static int asptr(EM_VAL obj, Type **val) {
      swig_type_info *descriptor = type_info<Type>();
      if (!obj || !descriptor) return SWIG_ERROR;
      void *p = 0;
      int res = SWIG_ConvertPtr(obj, &p, descriptor, 0);
      if (SWIG_IsOK(res) && val) *val = static_cast<Type*>(p);
      return res;
    }
  };

  template <class Type>
  inline int asptr(EM_VAL obj, Type **vptr) {
    return traits_asptr<Type>::asptr(obj, vptr);
  }

  template <class Type>
  struct traits_asval {
    static int asval(EM_VAL obj, Type *val) {
      if (!val) return traits_asptr<Type>::asptr(obj, (Type **)(0));
      Type *p = 0;
      int res = traits_asptr<Type>::asptr(obj, &p);
      if (!SWIG_IsOK(res)) return res;
      if (!p) return SWIG_ERROR;
      typedef typename noconst_traits<Type>::noconst_type noconst_type;
      *(const_cast<noconst_type*>(val)) = *p;
      if (SWIG_IsNewObj(res)) {
        delete p;
        res = SWIG_DelNewMask(res);
      }
      return res;
    }
  };

  template <class Type> struct traits_asval<Type*> {
    static int asval(EM_VAL obj, Type **val) {
      typedef typename noconst_traits<Type>::noconst_type noconst_type;
      if (!val) return traits_asptr<noconst_type>::asptr(obj, (noconst_type **)(0));
      noconst_type *p = 0;
      int res = traits_asptr<noconst_type>::asptr(obj, &p);
      if (SWIG_IsOK(res)) *(const_cast<noconst_type**>(val)) = p;
      return res;
    }
  };

  template <class Type>
  inline int asval(EM_VAL obj, Type *val) {
    return traits_asval<Type>::asval(obj, val);
  }

  /* Conversion failures throw: there is no valid object to return */
  template <class Type>
  struct traits_as<Type, value_category> {
    static Type as(EM_VAL obj) {
      Type v;
      int res = obj ? asval(obj, &v) : SWIG_ERROR;
      if (!SWIG_IsOK(res)) throw std::invalid_argument("bad type");
      return v;
    }
  };

  template <class Type>
  struct traits_as<Type, pointer_category> {
    static Type as(EM_VAL obj) {
      Type *v = 0;
      int res = obj ? traits_asptr<Type>::asptr(obj, &v) : SWIG_ERROR;
      if (!SWIG_IsOK(res) || !v) throw std::invalid_argument("bad type");
      if (SWIG_IsNewObj(res)) {
        Type r(*v);
        delete v;
        return r;
      }
      return *v;
    }
  };

  template <class Type>
  struct traits_as<Type*, pointer_category> {
    static Type* as(EM_VAL obj) {
      Type *v = 0;
      int res = obj ? traits_asptr<Type>::asptr(obj, &v) : SWIG_ERROR;
      if (!SWIG_IsOK(res)) throw std::invalid_argument("bad type");
      return v;
    }
  };

  template <class Type>
  inline Type as(EM_VAL obj, bool = false) {
    return traits_as<Type, typename traits<Type>::category>::as(obj);
  }

  template <class Type>
  struct traits_check<Type, value_category> {
    static bool check(EM_VAL obj) {
      return obj && SWIG_IsOK(asval(obj, (Type *)(0)));
    }
  };

  template <class Type>
  struct traits_check<Type, pointer_category> {
    static bool check(EM_VAL obj) {
      return obj && SWIG_IsOK(asptr(obj, (Type **)(0)));
    }
  };

  template <class Type>
  inline bool check(EM_VAL obj) {
    return traits_check<Type, typename traits<Type>::category>::check(obj);
  }
}
}

/* std::string <-> JS string; a wrapped std::string is copied */
%fragment(SWIG_Traits_frag(std::string), "header", fragment="StdTraits") {
namespace swig {
  template <> struct traits<std::string > {
    typedef value_category category;
    static const char* type_name() { return "std::string"; }
  };
  template <> struct traits_asval<std::string > {
    static int asval(EM_VAL obj, std::string *val) {
      if (!obj) return SWIG_ERROR;
      emscripten::val v = SWIG_WASMJS_Borrow(obj);
      if (v.typeOf().as<std::string>() == "string") {
        if (val) *val = v.as<std::string>();
        return SWIG_OK;
      }
      if (!SWIG_WASMJS_IsWrapped(v)) return SWIG_ERROR;
      swig_type_info *descriptor = SWIG_TypeQuery("std::string *");
      void *p = 0;
      if (!descriptor || !SWIG_IsOK(SWIG_ConvertPtr(obj, &p, descriptor, 0))) return SWIG_ERROR;
      if (val) *val = *static_cast<std::string*>(p);
      return SWIG_OK;
    }
  };
  template <> struct traits_from<std::string > {
    static EM_VAL from(const std::string& val) {
      return emscripten::val(val).release_ownership();
    }
  };
}
}
