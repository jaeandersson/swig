// Minimal std::pair declaration for wasm_js — opaque pointer convention.
namespace std {
  template <typename T1, typename T2> struct pair {
    T1 first;
    T2 second;
  };
}
