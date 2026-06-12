// Minimal std::map declaration for wasm_js. Routed across the wasm
// boundary as an opaque void* pointer (e.g. casadi::Dict; see the Dict
// opaque-builder shims in casadi.i). SWIG needs only the template name
// to parse %template() std::map<K, V>;
namespace std {
  template <typename Key, typename T> class map {};
}
