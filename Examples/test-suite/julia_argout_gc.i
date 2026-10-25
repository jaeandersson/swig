/* Output arguments are converted one after the other, and each conversion
   may collect: the result and the outputs converted so far must stay rooted. */
%module julia_argout_gc

%typemap(in, numinputs=0) double* OUTPUT (double tmp) "$1 = &tmp;"
/* a conversion that collects, deterministically */
%typemap(argout) double* OUTPUT %{ jl_gc_collect(JL_GC_FULL); %append_output(jl_box_float64(*$1)); %}

%apply double* OUTPUT { double* a, double* b };

/* container outputs: the backend synthesises the conversion */
%typemap(in, numinputs=0) std::vector<double>& OUTVEC (std::vector<double> tmp) "$1 = &tmp;"
%typemap(argout) std::vector<double>& OUTVEC "/* synthesised */"
%apply std::vector<double>& OUTVEC { std::vector<double>& v1, std::vector<double>& v2, std::vector<double>& v3 };

%include <std_vector.i>
%inline %{
#include <vector>
#include <stdexcept>
/* void return: outputs form the result tuple */
void split3(int n, std::vector<double>& v1, std::vector<double>& v2, double* a, double* b, std::vector<double>& v3) {
  v1.assign(n, 1.0); v2.assign(n, 2.0); v3.assign(n, 3.0); *a = n; *b = 2 * n;
}
/* value return: the result is converted before the outputs */
std::vector<double> ret_split(int n, std::vector<double>& v1, double* a, std::vector<double>& v2) {
  v1.assign(n, 1.0); v2.assign(n, 2.0); *a = n; return std::vector<double>(n, 7.0);
}
/* throws with the output frame live: the frame must still be popped */
void split_fail(std::vector<double>& v1, double* a) { throw std::runtime_error("split_fail"); }
%}
