/* ------------------------------------------------------------
 * Primitive Types
 * ------------------------------------------------------------ */


// boolean

%fragment(SWIG_From_frag(bool),"header") {
SWIGINTERNINLINE mxArray*
  SWIG_From_dec(bool)(bool value)
{
  return mxCreateLogicalScalar(value);
}
}

%fragment(SWIG_AsVal_frag(bool),"header",
	  fragment=SWIG_AsVal_frag(long)) {
SWIGINTERN int
SWIG_AsVal_dec(bool)(mxArray* pm, bool *val)
{
   if(!mxIsLogicalScalar(pm)) return SWIG_TypeError;
   if (val) *val = mxIsLogicalScalarTrue(pm);
   return SWIG_OK;
}
}

// Integral scalars: real numeric or logical scalar holding an integral value

%fragment("SWIG_Matlab_AsIntegral","header") {
SWIGINTERN int SWIG_Matlab_IsRealScalar(mxArray* pm)
{
  return pm && mxGetNumberOfElements(pm)==1 && !mxIsComplex(pm)
    && (mxIsNumeric(pm) || mxIsLogical(pm));
}

SWIGINTERN int SWIG_Matlab_AsLongLong(mxArray* pm, long long* val)
{
  double v;
  if (!SWIG_Matlab_IsRealScalar(pm)) return SWIG_TypeError;
  switch (mxGetClassID(pm)) {
  case mxINT64_CLASS:
    if (val) *val = (long long)(*(int64_T*)mxGetData(pm));
    return SWIG_OK;
  case mxUINT64_CLASS:
    {
      uint64_T u = *(uint64_T*)mxGetData(pm);
      if (u > (uint64_T)LLONG_MAX) return SWIG_OverflowError;
      if (val) *val = (long long)u;
      return SWIG_OK;
    }
  default:
    v = mxGetScalar(pm);
    if (v != floor(v)) return SWIG_TypeError;
    if (v < -9223372036854775808.0 || v >= 9223372036854775808.0) return SWIG_OverflowError;
    if (val) *val = (long long)v;
    return SWIG_OK;
  }
}

SWIGINTERN int SWIG_Matlab_AsUnsignedLongLong(mxArray* pm, unsigned long long* val)
{
  double v;
  if (!SWIG_Matlab_IsRealScalar(pm)) return SWIG_TypeError;
  switch (mxGetClassID(pm)) {
  case mxINT64_CLASS:
    {
      int64_T i = *(int64_T*)mxGetData(pm);
      if (i < 0) return SWIG_OverflowError;
      if (val) *val = (unsigned long long)i;
      return SWIG_OK;
    }
  case mxUINT64_CLASS:
    if (val) *val = (unsigned long long)(*(uint64_T*)mxGetData(pm));
    return SWIG_OK;
  default:
    v = mxGetScalar(pm);
    if (v != floor(v)) return SWIG_TypeError;
    if (v < 0 || v >= 18446744073709551616.0) return SWIG_OverflowError;
    if (val) *val = (unsigned long long)v;
    return SWIG_OK;
  }
}
}

// Integers are returned as double, like MATLAB's own size, numel and find

// long

%fragment(SWIG_From_frag(long),"header") {
SWIGINTERNINLINE mxArray* SWIG_From_dec(long)(long value)
{
  return mxCreateDoubleScalar((double)value);
}
}

%fragment(SWIG_AsVal_frag(long),"header",fragment="SWIG_Matlab_AsIntegral") {
SWIGINTERN int SWIG_AsVal_dec(long)(mxArray* pm, long* val)
{
  long long v;
  int res = SWIG_Matlab_AsLongLong(pm, &v);
  if (!SWIG_IsOK(res)) return res;
  if (v < LONG_MIN || v > LONG_MAX) return SWIG_OverflowError;
  if (val) *val = (long)v;
  return SWIG_OK;
}
}

// unsigned long

%fragment(SWIG_From_frag(unsigned long),"header") {
SWIGINTERNINLINE mxArray* SWIG_From_dec(unsigned long)(unsigned long value)
{
  return mxCreateDoubleScalar((double)value);
}
}

%fragment(SWIG_AsVal_frag(unsigned long),"header",fragment="SWIG_Matlab_AsIntegral") {
SWIGINTERN int SWIG_AsVal_dec(unsigned long)(mxArray* pm, unsigned long* val)
{
  unsigned long long v;
  int res = SWIG_Matlab_AsUnsignedLongLong(pm, &v);
  if (!SWIG_IsOK(res)) return res;
  if (v > ULONG_MAX) return SWIG_OverflowError;
  if (val) *val = (unsigned long)v;
  return SWIG_OK;
}
}

// long long

%fragment(SWIG_From_frag(long long),"header") {
SWIGINTERNINLINE mxArray* SWIG_From_dec(long long)(long long value)
{
  return mxCreateDoubleScalar((double)value);
}
}

%fragment(SWIG_AsVal_frag(long long),"header",fragment="SWIG_Matlab_AsIntegral") {
SWIGINTERN int SWIG_AsVal_dec(long long)(mxArray* pm, long long* val)
{
  return SWIG_Matlab_AsLongLong(pm, val);
}
}

// unsigned long long

%fragment(SWIG_From_frag(unsigned long long),"header") {
SWIGINTERNINLINE mxArray* SWIG_From_dec(unsigned long long)(unsigned long long value)
{
  return mxCreateDoubleScalar((double)value);
}
}

%fragment(SWIG_AsVal_frag(unsigned long long),"header",fragment="SWIG_Matlab_AsIntegral") {
SWIGINTERN int SWIG_AsVal_dec(unsigned long long)(mxArray* pm, unsigned long long* val)
{
  return SWIG_Matlab_AsUnsignedLongLong(pm, val);
}
}

// double: any real numeric or logical scalar

%fragment(SWIG_From_frag(double),"header") {
SWIGINTERNINLINE mxArray* SWIG_From_dec(double)(double value)
{
  return mxCreateDoubleScalar(value);
}
}

%fragment(SWIG_AsVal_frag(double),"header",fragment="SWIG_Matlab_AsIntegral") {
SWIGINTERN int SWIG_AsVal_dec(double)(mxArray* pm, double* val)
{
  if (!SWIG_Matlab_IsRealScalar(pm)) return SWIG_TypeError;
  if (val) *val = mxGetScalar(pm);
  return SWIG_OK;
}
}

// const char* (strings): 1xN or empty char array of any length, or a scalar MATLAB string

%fragment("SWIG_AsCharPtrAndSize","header") {
SWIGINTERN int
SWIG_AsCharPtrAndSize(mxArray* pm, char** cptr, size_t* psize, int *alloc)
{
  static char *buf = 0;
  static size_t bufsize = 0;
  char *str;
  size_t len;
  if (!pm) return SWIG_TypeError;
  if (mxIsClass(pm, "string")) {
    mxArray *c = 0;
    int res;
    if (mxGetNumberOfElements(pm) != 1) return SWIG_TypeError;
    if (SWIG_Matlab_CallInterp(1, &c, 1, &pm, "char") || !c) return SWIG_TypeError;
    res = mxIsChar(c) ? SWIG_AsCharPtrAndSize(c, cptr, psize, alloc) : SWIG_TypeError;
    mxDestroyArray(c);
    return res;
  }
  if (!mxIsChar(pm) || mxGetNumberOfDimensions(pm) != 2
      || (mxGetNumberOfElements(pm) != 0 && mxGetM(pm) != 1)) return SWIG_TypeError;
  if (!cptr && !psize) return SWIG_OK;
  str = mxArrayToString(pm);
  if (!str) return SWIG_TypeError;
  len = strlen(str);
  if (alloc) {
    if (cptr) *cptr = %new_copy_array(str, len + 1, char);
    *alloc = SWIG_NEWOBJ;
  } else if (cptr) {
    /* Borrowed pointer requested: valid until the next such call */
    if (bufsize < len + 1) {
      char *nbuf = (char*)realloc(buf, len + 1);
      if (!nbuf) {
        mxFree(str);
        return SWIG_MemoryError;
      }
      buf = nbuf;
      bufsize = len + 1;
    }
    memcpy(buf, str, len + 1);
    *cptr = buf;
  }
  if (psize) *psize = len + 1;
  mxFree(str);
  return SWIG_OK;
}
}

%fragment("SWIG_FromCharPtrAndSize","header",fragment="SWIG_pchar_descriptor") {
SWIGINTERNINLINE mxArray*
SWIG_FromCharPtrAndSize(const char* carray, size_t size)
{
   mwSize dims[2] = {1,(mwSize)size};
   mxArray* ret = mxCreateCharArray((mwSize)2,dims);
   if(!ret) SWIG_Matlab_ErrorId("SWIG:FromCharPtrAndSize","mxCreateCharArray failed");
   mxChar *ret_data = (mxChar *)mxGetData(ret);
   size_t i;
   for(i=0; i<size; ++i) ret_data[i] = (unsigned char)carray[i];
   return ret;  
}
}


