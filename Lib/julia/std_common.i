%include <std/std_except.i>
%include <juliastdcommon.swg>

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
    static int asval(jl_value_t* obj, value_type *val) {
      return SWIG_AsVal(Type)(obj, val);
    }
  };
  template <>  struct traits_from<Type > {
    typedef Type value_type;
    static jl_value_t* from(const value_type& val) {
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
    static int asval(jl_value_t* obj, value_type *val) {
      return SWIG_AsVal(int)(obj, (int *)val);
    }
  };
  template <>  struct traits_from<Type > {
    typedef Type value_type;
    static jl_value_t* from(const value_type& val) {
      return SWIG_From(int)((int)val);
    }
  };
}
}
%enddef

%include <std/std_common.i>

// Traits for the primitive types with SWIG_AsVal/SWIG_From in juliaprimtypes.swg
%traits_ptypen(bool);
%traits_ptypen(signed char);
%traits_ptypen(unsigned char);
%traits_ptypen(short);
%traits_ptypen(unsigned short);
%traits_ptypen(int);
%traits_ptypen(unsigned int);
%traits_ptypen(long);
%traits_ptypen(unsigned long);
%traits_ptypen(long long);
%traits_ptypen(unsigned long long);
%traits_ptypen(float);
%traits_ptypen(double);
%traits_ptypen(std::string);
