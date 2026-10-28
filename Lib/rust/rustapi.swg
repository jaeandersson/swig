/* -----------------------------------------------------------------------------
 * rustapi.swg
 *
 * extern "C" API through which the generated Rust code builds, inspects and
 * frees SwigRV guest values, plus the error state of the last call.
 * ----------------------------------------------------------------------------- */

extern "C" {
SWIGEXPORT int swig_rv_last_error_code() { return SWIG_lasterror_code; }
SWIGEXPORT const char* swig_rv_last_error_msg() { return SWIG_lasterror_msg.c_str(); }
SWIGEXPORT void swig_rv_clear_error() { SWIG_lasterror_code = 0; SWIG_lasterror_msg.clear(); }

SWIGEXPORT void swig_rv_free(SwigRV *v) { delete v; }

SWIGEXPORT SwigRV* swig_rv_new_nil() { return new SwigRV(); }
SWIGEXPORT SwigRV* swig_rv_new_f64(long long rows, long long cols, const double *data) {
  SwigRV *v = SWIG_Rust_NewNum(SWIG_RV_F64, rows, cols);
  if (data && rows * cols != 0) memcpy(&v->f64[0], data, rows * cols * sizeof(double));
  return v;
}
SWIGEXPORT SwigRV* swig_rv_new_i64(long long rows, long long cols, const long long *data) {
  SwigRV *v = SWIG_Rust_NewNum(SWIG_RV_I64, rows, cols);
  if (data && rows * cols != 0) memcpy(&v->i64[0], data, rows * cols * sizeof(long long));
  return v;
}
SWIGEXPORT SwigRV* swig_rv_new_bool(long long rows, long long cols, const unsigned char *data) {
  SwigRV *v = SWIG_Rust_NewNum(SWIG_RV_BOOL, rows, cols);
  for (long long k = 0; k < rows * cols; ++k) v->i64[k] = data[k] ? 1 : 0;
  return v;
}
SWIGEXPORT SwigRV* swig_rv_new_sparse(long long rows, long long cols, const long long *colind, const long long *row, const double *data) {
  SwigRV *v = new SwigRV();
  v->kind = SWIG_RV_NUM;
  v->elem = SWIG_RV_F64;
  v->rows = rows;
  v->cols = cols;
  v->sparse = true;
  v->colind.assign(colind, colind + cols + 1);
  long long nnz = v->colind[cols];
  v->row.assign(row, row + nnz);
  v->f64.assign(data, data + nnz);
  return v;
}
SWIGEXPORT SwigRV* swig_rv_new_str(const char *s, size_t n) { return SWIG_Rust_NewStr(s, n); }
/* Director results: run the C++ implementation, or fail with a message */
SWIGEXPORT SwigRV* swig_rv_new_base() { SwigRV *v = new SwigRV(); v->kind = SWIG_RV_BASE; return v; }
SWIGEXPORT SwigRV* swig_rv_new_err(const char *s, size_t n) { SwigRV *v = SWIG_Rust_NewStr(s, n); v->kind = SWIG_RV_ERR; return v; }
SWIGEXPORT SwigRV* swig_rv_new_list(size_t n) { return SWIG_Rust_NewList(n); }
/* Takes ownership of item */
SWIGEXPORT void swig_rv_list_set(SwigRV *l, size_t i, SwigRV *item) { delete l->items[i]; l->items[i] = item; }
SWIGEXPORT SwigRV* swig_rv_new_dict() { return SWIG_Rust_NewDict(); }
/* Takes ownership of item */
SWIGEXPORT void swig_rv_dict_push(SwigRV *d, const char *key, size_t n, SwigRV *item) {
  d->keys.push_back(std::string(key, n));
  d->items.push_back(item);
}
SWIGEXPORT SwigRV* swig_rv_new_ptr(void *ptr, swig_type_info *type, int own) { return SWIG_Rust_NewPointerObj(ptr, type, own ? SWIG_POINTER_OWN : 0); }
SWIGEXPORT SwigRV* swig_rv_new_fun(void *ctx, swig_rv_invoke_t invoke, swig_rv_drop_t drop) {
  SwigRV *v = new SwigRV();
  v->kind = SWIG_RV_FUN;
  v->ctx = ctx;
  v->invoke = invoke;
  v->drop = drop;
  return v;
}

SWIGEXPORT int swig_rv_kind(const SwigRV *v) { return v ? v->kind : SWIG_RV_NIL; }
SWIGEXPORT int swig_rv_elem(const SwigRV *v) { return v->elem; }
SWIGEXPORT long long swig_rv_rows(const SwigRV *v) { return v->rows; }
SWIGEXPORT long long swig_rv_cols(const SwigRV *v) { return v->cols; }
SWIGEXPORT int swig_rv_is_sparse(const SwigRV *v) { return v->sparse ? 1 : 0; }
SWIGEXPORT const double* swig_rv_f64_data(const SwigRV *v) { return v->f64.empty() ? 0 : &v->f64[0]; }
SWIGEXPORT const long long* swig_rv_i64_data(const SwigRV *v) { return v->i64.empty() ? 0 : &v->i64[0]; }
SWIGEXPORT size_t swig_rv_nnz(const SwigRV *v) { return v->sparse ? v->row.size() : (size_t)v->numel(); }
SWIGEXPORT const long long* swig_rv_colind(const SwigRV *v) { return v->colind.empty() ? 0 : &v->colind[0]; }
SWIGEXPORT const long long* swig_rv_row(const SwigRV *v) { return v->row.empty() ? 0 : &v->row[0]; }
SWIGEXPORT const char* swig_rv_str(const SwigRV *v, size_t *n) { *n = v->str.size(); return v->str.data(); }
SWIGEXPORT size_t swig_rv_len(const SwigRV *v) { return v->items.size(); }
/* Borrowed */
SWIGEXPORT SwigRV* swig_rv_item(const SwigRV *v, size_t i) { return v->items[i]; }
SWIGEXPORT const char* swig_rv_key(const SwigRV *v, size_t i, size_t *n) { *n = v->keys[i].size(); return v->keys[i].data(); }
SWIGEXPORT void* swig_rv_ptr(const SwigRV *v) { return v->ptr; }
SWIGEXPORT swig_type_info* swig_rv_type(const SwigRV *v) { return v->type; }
SWIGEXPORT const char* swig_rv_type_name(const SwigRV *v) { return v->type ? v->type->name : ""; }
SWIGEXPORT int swig_rv_own(const SwigRV *v) { return v->own; }
/* Transfers ownership of the pointee to the caller */
SWIGEXPORT void swig_rv_disown(SwigRV *v) { v->own = 0; }
/* The pointer converted to type, or 0 if v does not hold a compatible pointer */
SWIGEXPORT void* swig_rv_cast_ptr(SwigRV *v, swig_type_info *type) {
  void *p = 0;
  if (!v || v->kind != SWIG_RV_PTR) return 0;
  if (!SWIG_IsOK(SWIG_Rust_ConvertPtrAndOwn(v, &p, type, 0, 0))) return 0;
  return p;
}
/* Lookup by mangled name (_p_Foo) or by C++ name (Foo *) */
SWIGEXPORT swig_type_info* swig_rv_type_query(const char *name) {
  swig_module_info *module = SWIG_GetModule(0);
  swig_type_info *t = SWIG_MangledTypeQueryModule(module, module, name);
  return t ? t : SWIG_TypeQuery(name);
}
}
