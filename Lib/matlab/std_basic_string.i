#if !defined(SWIG_STD_STRING) 
#define SWIG_STD_BASIC_STRING
#define SWIG_STD_MODERN_STL

%include <matlabcontainer.swg>

#define %swig_basic_string(Type...)  %swig_sequence_methods_val(Type)


%fragment(SWIG_AsPtr_frag(std::basic_string<char>),"header",
	  fragment="SWIG_AsCharPtrAndSize") {
SWIGINTERN int
SWIG_AsPtr(std::basic_string<char>)(mxArray* obj, std::string **val)
{
  char* buf = 0;
  size_t size = 0;
  int alloc = SWIG_OLDOBJ;
  if (SWIG_IsOK((SWIG_AsCharPtrAndSize(obj, &buf, &size, &alloc))) && buf) {
    if (val) *val = new std::string(buf, size - 1);
    if (alloc == SWIG_NEWOBJ) %delete_array(buf);
    return SWIG_NEWOBJ;
  }
  return SWIG_ERROR;
}
}

%fragment(SWIG_From_frag(std::basic_string<char>),"header",
	  fragment="SWIG_FromCharPtrAndSize") {
SWIGINTERNINLINE mxArray*
  SWIG_From(std::basic_string<char>)(const std::string& s)
  {
    return SWIG_FromCharPtrAndSize(s.data(), s.size());
  }
}

%ignore std::basic_string::operator +=;

%include <std/std_basic_string.i>
%typemaps_asptrfromn(%checkcode(STRING), std::basic_string<char>);

#endif


#if !defined(SWIG_STD_WSTRING)
// TODO
#endif
