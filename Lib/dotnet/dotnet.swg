/* -----------------------------------------------------------------------------
 * dotnet.swg
 *
 * Typemap library for the -dotnet target (UTL based, values cross as SwigRV*).
 *
 * The C# surface is shaped by two typemaps on the bare C++ type:
 *   cstype     -- C# type of a parameter (and of a return, unless csout is set)
 *   cstype:out -- C# type of a return value, when it differs from the parameter
 * Containers (std::vector, std::map<std::string,...>, std::pair) and wrapped
 * classes are mapped by the module itself.  "object" accepts anything.
 * ----------------------------------------------------------------------------- */

%include <typemaps/swigmacros.swg>
%include <typemaps/fragments.swg>
%include <dotnetruntime.swg>
%include <dotnettypemaps.swg>

%define %docstring %feature("docstring") %enddef

%typemap(cstype) double, float "double"
%typemap(cstype) bool "bool"
%typemap(cstype) signed char, unsigned char, short, unsigned short, int, unsigned int, long, unsigned long, long long, unsigned long long, size_t, std::size_t "long"
%typemap(cstype) std::string, char "string"
%typemap(cstype) void "void"

/* C# code added to the proxy class of the enclosing %extend */
#define %proxycode %insert("proxycode")
