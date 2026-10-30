// std::vector: from a JS Array, typed array or wrapped vector; to a JS Array, or the %wasm_vec class

%include <wasm_jscontainer.swg>

%fragment("StdVectorTraits","header",fragment="StdSequenceTraits")
%{
  namespace swig {
    template <class T, class A>
    struct traits<std::vector<T, A> > {
      typedef pointer_category category;
      static const char* type_name() {
        static std::string n = "std::vector< " + std::string(swig::type_name<T>()) + " >";
        return n.c_str();
      }
    };

    template <class T, class A>
    struct traits_asptr<std::vector<T, A> >  {
      static int asptr(EM_VAL obj, std::vector<T, A> **vec) {
        return traits_asptr_stdseq<std::vector<T, A> >::asptr(obj, vec);
      }
    };

    template <class T, class A>
    struct traits_from<std::vector<T, A> > {
      static EM_VAL from(const std::vector<T, A>& vec) {
        const char* nm = wasm_js_class_name<std::vector<T, A> >::get();
        if (nm) return SWIG_WASMJS_WrapByName(new std::vector<T, A>(vec), nm);
        return traits_from_stdseq<std::vector<T, A> >::from(vec);
      }
    };
  }
%}

// Class declaration for named %template instantiations.
//
// Routes std::vector<T> across the wasm boundary as an opaque void* to a
// C++ vector built on the JS side.  Named templates
// (`%template(FooVector) std::vector<Foo>;`) emit a JS proxy
// class with this interior; primitive instantiations (vector<int>,
// vector<double>) should stay anonymous because they don't share the
// boxed-pointer ABI -- callers convert them at the typemap level.
//
// Subset exposed: ctor, size, empty, clear, push_back, at (read-only).
// Write-by-index is deliberately omitted to dodge the `%extend` codegen
// gap in wasm_js.cxx (calls self->method() instead of substituting
// the extend body).  Build vectors via push_back; for write access,
// rebuild a fresh vector.

namespace std {
  template <typename T> class vector {
  public:
    %fragment(SWIG_Traits_frag(std::vector< T >), "header",
              fragment=SWIG_Traits_frag(T), fragment="StdVectorTraits") {}
    typedef T value_type;
    typedef T const_reference;   // by value -- skirts the `const T&`
                                 // ctype gap (no typemap for reference
                                 // forms of typedef-resolved primitives)

    vector();

    // Use `unsigned long` directly: `size_t` isn't a SWIG-known typedef
    // (no <stddef.h> in our parse path), and `vector<T>::size_type` is
    // a dependent name SWIG can't resolve.  Either route would box the
    // primitive as void* via the T_USER fallback.  On wasm32, `unsigned
    // long` is 32-bit (i32 over the wasm ABI); JS receives Number.
    unsigned long size() const;
    bool empty() const;
    void clear();
    // push_back takes T by value rather than `const T&`: SWIG resolves
    // template typedefs (e.g. T=my_int -> T=long long) before typemap
    // matching, so `const my_int&` becomes `const long long&` which
    // has no registered typemap.  By-value `T` matches the value-form
    // typemap of the user's element type.
    void push_back(T x);
    T at(unsigned long i) const;
  };
}
