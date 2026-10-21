%include <wasm_jsstdcommon.swg>

// Traits for a primitive type with SWIG_AsVal and SWIG_From
%define %traits_ptypen(Type...)
  %fragment(SWIG_Traits_frag(Type),"header",
	    fragment=SWIG_AsVal_frag(Type),
	    fragment=SWIG_From_frag(Type),
	    fragment="StdTraits") {
namespace swig {
  template <> struct traits< Type > {
    typedef value_category category;
    static const char* type_name() { return  #Type; }
  };
  template <> struct traits_asval< Type > {
    typedef Type value_type;
    static int asval(EM_VAL obj, value_type *val) {
      return SWIG_AsVal(Type)(obj, val);
    }
  };
  template <> struct traits_from< Type > {
    typedef Type value_type;
    static EM_VAL from(const value_type& val) {
      return SWIG_From(Type)(val);
    }
  };
}
}
%enddef

%include <std/std_common.i>

%traits_ptypen(bool)
%traits_ptypen(int)
%traits_ptypen(long)
%traits_ptypen(long long)
%traits_ptypen(double)
%traits_ptypen(float)
%traits_ptypen(unsigned int)
%traits_ptypen(unsigned long)
%traits_ptypen(unsigned long long)
