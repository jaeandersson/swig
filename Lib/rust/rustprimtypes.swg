/* ------------------------------------------------------------
 * Primitive Types
 * ------------------------------------------------------------ */

// boolean

%fragment(SWIG_From_frag(bool),"header") {
SWIGINTERNINLINE SwigRV*
SWIG_From_dec(bool)(bool value) {
  SwigRV *v = SWIG_Rust_NewNum(SWIG_RV_BOOL, 1, 1);
  v->i64[0] = value ? 1 : 0;
  return v;
}
}

%fragment(SWIG_AsVal_frag(bool),"header") {
SWIGINTERN int
SWIG_AsVal_dec(bool)(SwigRV *v, bool *val) {
  if (!SWIG_Rust_IsScalar(v) || v->elem != SWIG_RV_BOOL) return SWIG_TypeError;
  if (val) *val = v->i64[0] != 0;
  return SWIG_OK;
}
}

// long long (all integer types go through it)

%fragment(SWIG_From_frag(long long),"header") {
SWIGINTERNINLINE SwigRV*
SWIG_From_dec(long long)(long long value) {
  SwigRV *v = SWIG_Rust_NewNum(SWIG_RV_I64, 1, 1);
  v->i64[0] = value;
  return v;
}
}

%fragment(SWIG_AsVal_frag(long long),"header") {
SWIGINTERN int
SWIG_AsVal_dec(long long)(SwigRV *v, long long *val) {
  if (!SWIG_Rust_IsScalar(v) || v->elem != SWIG_RV_I64) return SWIG_TypeError;
  if (val) *val = v->i64[0];
  return SWIG_OK;
}
}

%fragment(SWIG_From_frag(long),"header", fragment=SWIG_From_frag(long long)) {
SWIGINTERNINLINE SwigRV* SWIG_From_dec(long)(long value) { return SWIG_From(long long)(value); }
}

%fragment(SWIG_AsVal_frag(long),"header", fragment=SWIG_AsVal_frag(long long)) {
SWIGINTERN int
SWIG_AsVal_dec(long)(SwigRV *v, long *val) {
  long long w;
  int res = SWIG_AsVal(long long)(v, &w);
  if (!SWIG_IsOK(res)) return res;
  if (w < LONG_MIN || w > LONG_MAX) return SWIG_OverflowError;
  if (val) *val = (long)w;
  return SWIG_OK;
}
}

%fragment(SWIG_From_frag(unsigned long long),"header", fragment=SWIG_From_frag(long long)) {
SWIGINTERNINLINE SwigRV* SWIG_From_dec(unsigned long long)(unsigned long long value) { return SWIG_From(long long)((long long)value); }
}

%fragment(SWIG_AsVal_frag(unsigned long long),"header", fragment=SWIG_AsVal_frag(long long)) {
SWIGINTERN int
SWIG_AsVal_dec(unsigned long long)(SwigRV *v, unsigned long long *val) {
  long long w;
  int res = SWIG_AsVal(long long)(v, &w);
  if (!SWIG_IsOK(res)) return res;
  if (w < 0) return SWIG_OverflowError;
  if (val) *val = (unsigned long long)w;
  return SWIG_OK;
}
}

%fragment(SWIG_From_frag(unsigned long),"header", fragment=SWIG_From_frag(long long)) {
SWIGINTERNINLINE SwigRV* SWIG_From_dec(unsigned long)(unsigned long value) { return SWIG_From(long long)((long long)value); }
}

%fragment(SWIG_AsVal_frag(unsigned long),"header", fragment=SWIG_AsVal_frag(unsigned long long)) {
SWIGINTERN int
SWIG_AsVal_dec(unsigned long)(SwigRV *v, unsigned long *val) {
  unsigned long long w;
  int res = SWIG_AsVal(unsigned long long)(v, &w);
  if (!SWIG_IsOK(res)) return res;
  if (w > ULONG_MAX) return SWIG_OverflowError;
  if (val) *val = (unsigned long)w;
  return SWIG_OK;
}
}

// double: any numeric scalar except bool

%fragment(SWIG_From_frag(double),"header") {
SWIGINTERNINLINE SwigRV*
SWIG_From_dec(double)(double value) {
  SwigRV *v = SWIG_Rust_NewNum(SWIG_RV_F64, 1, 1);
  v->f64[0] = value;
  return v;
}
}

%fragment(SWIG_AsVal_frag(double),"header") {
SWIGINTERN int
SWIG_AsVal_dec(double)(SwigRV *v, double *val) {
  if (!SWIG_Rust_IsScalar(v) || v->elem == SWIG_RV_BOOL) return SWIG_TypeError;
  if (val) *val = SWIG_Rust_ScalarF64(v);
  return SWIG_OK;
}
}

// strings

%fragment("SWIG_AsCharPtrAndSize","header") {
SWIGINTERN int
SWIG_AsCharPtrAndSize(SwigRV *v, char** cptr, size_t* psize, int *alloc) {
  if (!v || v->kind != SWIG_RV_STR) return SWIG_TypeError;
  if (alloc) {
    if (cptr) {
      *cptr = %new_array(v->str.size() + 1, char);
      memcpy(*cptr, v->str.c_str(), v->str.size() + 1);
    }
    *alloc = SWIG_NEWOBJ;
  } else if (cptr) {
    *cptr = const_cast<char*>(v->str.c_str());
  }
  if (psize) *psize = v->str.size() + 1;
  return SWIG_OK;
}
}

%fragment("SWIG_FromCharPtrAndSize","header",fragment="SWIG_pchar_descriptor") {
SWIGINTERNINLINE SwigRV*
SWIG_FromCharPtrAndSize(const char* carray, size_t size) {
  return SWIG_Rust_NewStr(carray ? carray : "", carray ? size : 0);
}
}
