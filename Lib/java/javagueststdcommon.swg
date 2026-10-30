/* -----------------------------------------------------------------------------
 * javagueststdcommon.swg
 *
 * swig::traits over jobject for guest-object mode (asptr / asval / as / check / from)
 * ----------------------------------------------------------------------------- */

%fragment("StdTraits","header",fragment="StdTraitsCommon",fragment="<stdexcept>")
{
namespace swig {
  // A wrapped pointer as a proxy object (owner: the proxy deletes it)
  template <class Type> struct traits_from_ptr {
    static jobject from(Type *val, int owner = 0) {
      return SWIG_NewPointerObj(val, type_info<Type>(), owner);
    }
  };

  template <class Type> struct traits_from {
    static jobject from(const Type& val) {
      return traits_from_ptr<Type>::from(new Type(val), 1);
    }
  };

  template <class Type> struct traits_from<Type *> {
    static jobject from(Type* val) {
      return traits_from_ptr<Type>::from(val, 0);
    }
  };

  template <class Type> struct traits_from<const Type *> {
    static jobject from(const Type* val) {
      return traits_from_ptr<Type>::from(const_cast<Type*>(val), 0);
    }
  };

  template <class Type>
  inline jobject from(const Type& val) {
    return traits_from<Type>::from(val);
  }

  template <class Type>
  inline jobject from_ptr(Type* val, int owner) {
    return traits_from_ptr<Type>::from(val, owner);
  }

  // Wrapped objects only; val==0: check only
  template <class Type>
  struct traits_asptr {
    static int asptr(jobject obj, Type **val) {
      Type *p = 0;
      swig_type_info *descriptor = type_info<Type>();
      int res = obj && descriptor ? SWIG_ConvertPtr(obj, (void**)&p, descriptor, 0) : SWIG_ERROR;
      if (SWIG_IsOK(res) && val) *val = p;
      return res;
    }
  };

  template <class Type>
  inline int asptr(jobject obj, Type **vptr) {
    return traits_asptr<Type>::asptr(obj, vptr);
  }

  template <class Type>
  struct traits_asval {
    static int asval(jobject obj, Type *val) {
      if (val) {
        Type *p = 0;
        int res = traits_asptr<Type>::asptr(obj, &p);
        if (!SWIG_IsOK(res)) return res;
        if (p) {
          typedef typename noconst_traits<Type>::noconst_type noconst_type;
          *(const_cast<noconst_type*>(val)) = *p;
          if (SWIG_IsNewObj(res)) {
            %delete(p);
            res = SWIG_DelNewMask(res);
          }
          return res;
        } else {
          return SWIG_ERROR;
        }
      } else {
        return traits_asptr<Type>::asptr(obj, (Type **)(0));
      }
    }
  };

  template <class Type> struct traits_asval<Type*> {
    static int asval(jobject obj, Type **val) {
      if (val) {
        typedef typename noconst_traits<Type>::noconst_type noconst_type;
        noconst_type *p = 0;
        int res = traits_asptr<noconst_type>::asptr(obj, &p);
        if (SWIG_IsOK(res)) {
          *(const_cast<noconst_type**>(val)) = p;
        }
        return res;
      } else {
        return traits_asptr<Type>::asptr(obj, (Type **)(0));
      }
    }
  };

  template <class Type>
  inline int asval(jobject obj, Type *val) {
    return traits_asval<Type>::asval(obj, val);
  }

  // Conversion failures always throw: there is no valid object to return
  template <class Type>
  struct traits_as<Type, value_category> {
    static Type as(jobject obj, bool /*throw_error*/) {
      Type v;
      int res = obj ? asval(obj, &v) : SWIG_ERROR;
      if (!SWIG_IsOK(res)) throw std::invalid_argument(std::string("expected ") + swig::type_name<Type>());
      return v;
    }
  };

  template <class Type>
  struct traits_as<Type, pointer_category> {
    static Type as(jobject obj, bool /*throw_error*/) {
      Type *v = 0;
      int res = obj ? traits_asptr<Type>::asptr(obj, &v) : SWIG_ERROR;
      if (SWIG_IsOK(res) && v) {
        if (SWIG_IsNewObj(res)) {
          Type r(*v);
          %delete(v);
          return r;
        } else {
          return *v;
        }
      }
      throw std::invalid_argument(std::string("expected ") + swig::type_name<Type>());
    }
  };

  template <class Type>
  struct traits_as<Type*, pointer_category> {
    static Type* as(jobject obj, bool /*throw_error*/) {
      Type *v = 0;
      int res = obj ? traits_asptr<Type>::asptr(obj, &v) : SWIG_ERROR;
      if (SWIG_IsOK(res)) return v;
      throw std::invalid_argument(std::string("expected ") + swig::type_name<Type>());
    }
  };

  template <class Type>
  inline Type as(jobject obj, bool te = false) {
    return traits_as<Type, typename traits<Type>::category>::as(obj, te);
  }

  // null is a Java value: the conversions decide whether they take it
  template <class Type>
  struct traits_check<Type, value_category> {
    static bool check(jobject obj) {
      int res = asval(obj, (Type *)(0));
      return SWIG_IsOK(res) ? true : false;
    }
  };

  template <class Type>
  struct traits_check<Type, pointer_category> {
    static bool check(jobject obj) {
      int res = asptr(obj, (Type **)(0));
      return SWIG_IsOK(res) ? true : false;
    }
  };

  template <class Type>
  inline bool check(jobject obj) {
    return traits_check<Type, typename traits<Type>::category>::check(obj);
  }
}
}

// Traits for a 'primitive' type, such as 'double', with SWIG_AsVal and SWIG_From
%define %traits_ptypen(Type...)
  %fragment(SWIG_Traits_frag(Type),"header",
	    fragment=SWIG_AsVal_frag(Type),
	    fragment=SWIG_From_frag(Type),
	    fragment="StdTraits") {
namespace swig {
  template <> struct traits<Type > {
    typedef value_category category;
    static const char* type_name() { return  #Type; }
  };
  template <>  struct traits_asval<Type > {
    typedef Type value_type;
    static int asval(jobject obj, value_type *val) {
      return SWIG_AsVal(Type)(obj, val);
    }
  };
  template <>  struct traits_from<Type > {
    typedef Type value_type;
    static jobject from(const value_type& val) {
      return SWIG_From(Type)(val);
    }
  };
}
}
%enddef

// Traits for enums, marshalled as int
%define %traits_enum(Type...)
  %fragment("SWIG_Traits_enum_"{Type},"header",
	    fragment=SWIG_AsVal_frag(int),
	    fragment=SWIG_From_frag(int),
	    fragment="StdTraits") {
namespace swig {
  template <>  struct traits_asval<Type > {
    typedef Type value_type;
    static int asval(jobject obj, value_type *val) {
      return SWIG_AsVal(int)(obj, (int *)val);
    }
  };
  template <>  struct traits_from<Type > {
    typedef Type value_type;
    static jobject from(const value_type& val) {
      return SWIG_From(int)((int)val);
    }
  };
}
}
%enddef

// std::string as java.lang.String
%fragment(SWIG_AsVal_frag(std::string), "header") {
SWIGINTERN int SWIG_AsVal_dec(std::string)(jobject obj, std::string *val) {
  if (!SWIG_JavaGuestIsString(obj)) return SWIG_TypeError;
  if (val) *val = SWIG_JavaGuestString(obj);
  return SWIG_OK;
}
}
%fragment(SWIG_From_frag(std::string), "header") {
SWIGINTERNINLINE jobject SWIG_From_dec(std::string)(const std::string& s) {
  return SWIG_JavaGuestNewString(s);
}
}

%include <std/std_common.i>

%apply size_t { std::size_t };
%apply const size_t& { const std::size_t& };

%traits_ptypen(bool);
%traits_ptypen(int);
%traits_ptypen(unsigned int);
%traits_ptypen(long);
%traits_ptypen(unsigned long);
%traits_ptypen(long long);
%traits_ptypen(unsigned long long);
%traits_ptypen(float);
%traits_ptypen(double);
%traits_ptypen(std::string);
