/* -----------------------------------------------------------------------------
 * rust.swg
 *
 * Typemap library for the -rust target (UTL based, values cross as SwigRV*).
 * ----------------------------------------------------------------------------- */

%include <typemaps/swigmacros.swg>
%include <typemaps/fragments.swg>
%include <rustruntime.swg>
%include <rusttypemaps.swg>
%include <rustkw.swg>

%define %docstring %feature("docstring") %enddef

/* Rust types: rstype is returned, rsaccept lists the Rust types accepted ("Any": anything) */
%typemap(rstype) double, float "f64"
%typemap(rsaccept) double, float "f64 | i64"
%typemap(rstype) bool "bool"
%typemap(rsaccept) bool "bool"
%typemap(rstype) signed char, unsigned char, short, unsigned short, int, unsigned int, long, unsigned long, long long, unsigned long long, size_t, std::size_t "i64"
%typemap(rsaccept) signed char, unsigned char, short, unsigned short, int, unsigned int, long, unsigned long, long long, unsigned long long, size_t, std::size_t "i64"
%typemap(rstype) std::string, char "String"
%typemap(rsaccept) std::string, char "String"
%typemap(rstype) void "()"
