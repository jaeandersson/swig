// Minimal std::vector declaration for wasm_js.
//
// Routes std::vector<T> across the wasm boundary as an opaque void* to a
// C++ vector built on the JS side.  Named templates
// (`%template(MXVector) std::vector<casadi::MX>;` etc.) emit a JS proxy
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
    // template typedefs (T=casadi_int -> T=long long) before typemap
    // matching, so `const casadi_int&` becomes `const long long&` which
    // has no registered typemap.  By-value `T` matches the value-form
    // typemap which DOES route through casadi.i's `(in) xType`.
    void push_back(T x);
    T at(unsigned long i) const;
  };
}
