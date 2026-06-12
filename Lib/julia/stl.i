// Minimal STL bindings for wasm_js. We use the opaque-pointer convention
// — std::vector / std::map / std::pair cross the wasm boundary as void*
// to a C++ container built JS-side. SWIG only needs the template names
// so %template() std::vector<X>; instantiations parse cleanly.
%include "std_vector.i"
%include "std_map.i"
%include "std_pair.i"
