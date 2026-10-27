/* -----------------------------------------------------------------------------
 * rustrun.swg
 *
 * Guest value for the -rust target: every argument and result crosses the
 * C ABI as a SwigRV*, a variant owned by this runtime and built/inspected by
 * the generated Rust code through the extern "C" swig_rv_* API below.
 * ----------------------------------------------------------------------------- */

/* Last error (thread-local: wrappers may run on several Rust threads) */
static thread_local int SWIG_lasterror_code = 0;
static thread_local std::string SWIG_lasterror_msg;

SWIGRUNTIME void SWIG_Error(int code, const char *msg) {
  SWIG_lasterror_code = code;
  SWIG_lasterror_msg = msg ? msg : "";
}

SWIGRUNTIME void SWIG_Error_Format(int code, const char *format, ...) {
  va_list arg, arg_copy;
  va_start(arg, format);
  va_copy(arg_copy, arg);
  int n = vsnprintf(NULL, 0, format, arg_copy);
  va_end(arg_copy);
  std::string s(n > 0 ? n : 0, '\0');
  if (n > 0) vsnprintf(&s[0], n + 1, format, arg);
  va_end(arg);
  SWIG_lasterror_code = code;
  SWIG_lasterror_msg = s;
}

SWIGRUNTIME const char* SWIG_ErrorType(int code) {
  switch (code) {
  case SWIG_MemoryError: return "MemoryError";
  case SWIG_IOError: return "IOError";
  case SWIG_RuntimeError: return "RuntimeError";
  case SWIG_IndexError: return "IndexError";
  case SWIG_TypeError: return "TypeError";
  case SWIG_DivisionByZero: return "DivisionByZero";
  case SWIG_OverflowError: return "OverflowError";
  case SWIG_SyntaxError: return "SyntaxError";
  case SWIG_ValueError: return "ValueError";
  case SWIG_SystemError: return "SystemError";
  case SWIG_AttributeError: return "AttributeError";
  }
  return "UnknownError";
}

SWIGRUNTIME int SWIG_check_num_args(const char *func_name, int num_args, int max_args, int min_args, int varargs) {
  if (num_args > max_args && !varargs) {
    SWIG_Error_Format(SWIG_RuntimeError, "function %s takes at most %i arguments", func_name, max_args);
  } else if (num_args < min_args) {
    SWIG_Error_Format(SWIG_RuntimeError, "function %s requires at least %i arguments", func_name, min_args);
  } else {
    return 1;
  }
  return 0;
}

#define SWIG_fail goto fail

/* Value kinds and numeric element types */
enum { SWIG_RV_NIL = 0, SWIG_RV_NUM = 1, SWIG_RV_STR = 2, SWIG_RV_LIST = 3, SWIG_RV_DICT = 4, SWIG_RV_PTR = 5, SWIG_RV_FUN = 6 };
enum { SWIG_RV_F64 = 0, SWIG_RV_I64 = 1, SWIG_RV_BOOL = 2 };

/* Host callable: invoke(ctx, argc, argv) returns a new value or 0 on error; drop(ctx) releases it */
typedef struct SwigRV* (*swig_rv_invoke_t)(void *ctx, int argc, struct SwigRV **argv);
typedef void (*swig_rv_drop_t)(void *ctx);

struct SwigRV {
  int kind;
  /* SWIG_RV_NUM: column-major dense data, or CCS when sparse */
  int elem;
  long long rows, cols;
  std::vector<double> f64;
  std::vector<long long> i64;
  bool sparse;
  std::vector<long long> colind, row;
  /* SWIG_RV_STR */
  std::string str;
  /* SWIG_RV_LIST and SWIG_RV_DICT (keys only for dicts) */
  std::vector<SwigRV*> items;
  std::vector<std::string> keys;
  /* SWIG_RV_PTR */
  void *ptr;
  swig_type_info *type;
  int own;
  /* SWIG_RV_FUN */
  void *ctx;
  swig_rv_invoke_t invoke;
  swig_rv_drop_t drop;
  SwigRV() : kind(SWIG_RV_NIL), elem(SWIG_RV_F64), rows(0), cols(0), sparse(false),
             ptr(0), type(0), own(0), ctx(0), invoke(0), drop(0) {}
  ~SwigRV() {
    for (size_t i = 0; i < items.size(); ++i) delete items[i];
    if (drop) drop(ctx);
  }
  long long numel() const { return rows * cols; }
};

/* Wrapper argument/result vectors; resc/resv as in the MATLAB gateway */
typedef SwigRV* SWIG_RV_Object;

SWIGRUNTIME int SWIG_Rust_IsNum(const SwigRV *v) { return v && v->kind == SWIG_RV_NUM; }
SWIGRUNTIME int SWIG_Rust_IsScalar(const SwigRV *v) { return SWIG_Rust_IsNum(v) && v->rows == 1 && v->cols == 1 && !v->sparse; }
SWIGRUNTIME double SWIG_Rust_ScalarF64(const SwigRV *v) {
  return v->elem == SWIG_RV_F64 ? v->f64[0] : (double)v->i64[0];
}

SWIGRUNTIME SwigRV* SWIG_Rust_NewNum(int elem, long long rows, long long cols) {
  SwigRV *v = new SwigRV();
  v->kind = SWIG_RV_NUM;
  v->elem = elem;
  v->rows = rows;
  v->cols = cols;
  if (elem == SWIG_RV_F64) v->f64.resize(rows * cols); else v->i64.resize(rows * cols);
  return v;
}

SWIGRUNTIME SwigRV* SWIG_Rust_NewStr(const char *s, size_t n) {
  SwigRV *v = new SwigRV();
  v->kind = SWIG_RV_STR;
  v->str.assign(s, n);
  return v;
}

SWIGRUNTIME SwigRV* SWIG_Rust_NewList(size_t n) {
  SwigRV *v = new SwigRV();
  v->kind = SWIG_RV_LIST;
  v->items.resize(n, 0);
  return v;
}

SWIGRUNTIME SwigRV* SWIG_Rust_NewDict() {
  SwigRV *v = new SwigRV();
  v->kind = SWIG_RV_DICT;
  return v;
}

/* Pointers */
static swig_module_info *saved_swig_module = 0;
SWIGRUNTIME swig_module_info *SWIG_Rust_GetModule(void *SWIGUNUSEDPARM(clientdata)) { return saved_swig_module; }
SWIGRUNTIME void SWIG_Rust_SetModule(swig_module_info *pointer) { saved_swig_module = pointer; }

SWIGRUNTIME SwigRV* SWIG_Rust_NewPointerObj(void *ptr, swig_type_info *type, int flags) {
  SwigRV *v = new SwigRV();
  if (!ptr) return v;
  v->kind = SWIG_RV_PTR;
  v->ptr = ptr;
  v->type = type;
  v->own = (flags & SWIG_POINTER_OWN) ? 1 : 0;
  return v;
}

SWIGRUNTIME int SWIG_Rust_ConvertPtrAndOwn(SwigRV *v, void **ptr, swig_type_info *type, int flags, int *own) {
  if (!v) return SWIG_ERROR;
  if (v->kind == SWIG_RV_NIL) {
    if (ptr) *ptr = 0;
    return (flags & SWIG_POINTER_NO_NULL) ? SWIG_NullReferenceError : SWIG_OK;
  }
  if (v->kind != SWIG_RV_PTR) return SWIG_ERROR;
  swig_cast_info *tc = 0;
  if (type && v->type != type) {
    tc = SWIG_TypeCheckStruct(v->type, type);
    if (!tc) return SWIG_ERROR;
  }
  if (own) *own = v->own;
  if (flags & SWIG_POINTER_DISOWN) v->own = 0;
  if (!ptr) return SWIG_OK;
  *ptr = v->ptr;
  if (tc) {
    int newmemory = 0;
    *ptr = SWIG_TypeCast(tc, *ptr, &newmemory);
    if (newmemory == SWIG_CAST_NEW_MEMORY) {
      if (!own) return SWIG_ERROR;
      *own = *own | SWIG_CAST_NEW_MEMORY;
    }
  }
  return SWIG_OK;
}

#define SWIG_ConvertPtr(obj, pptr, type, flags)         SWIG_Rust_ConvertPtrAndOwn(obj, pptr, type, flags, 0)
#define SWIG_ConvertPtrAndOwn(obj,pptr,type,flags,own)  SWIG_Rust_ConvertPtrAndOwn(obj, pptr, type, flags, own)
#define SWIG_NewPointerObj(ptr, type, flags)            SWIG_Rust_NewPointerObj(ptr, type, flags)
#define swig_owntype                                    int
#define SWIG_ConvertFunctionPtr(obj, pptr, type)        SWIG_ConvertPtr(obj, pptr, type, 0)
#define SWIG_NewFunctionPtrObj(ptr, type)               SWIG_NewPointerObj(ptr, type, 0)
#define SWIG_ConvertPacked(obj, ptr, sz, ty)            SWIG_ERROR
#define SWIG_NewPackedObj(ptr, sz, type)                SWIG_Rust_NewPointerObj(0, 0, 0)
#define SWIG_ConvertMember(obj, ptr, sz, ty)            SWIG_ERROR
#define SWIG_NewMemberObj(ptr, sz, type)                SWIG_Rust_NewPointerObj(0, 0, 0)
#define SWIG_GetModule(clientdata)                      SWIG_Rust_GetModule(clientdata)
#define SWIG_SetModule(clientdata, pointer)             SWIG_Rust_SetModule(pointer)
#define SWIG_MODULE_CLIENTDATA_TYPE void*
#define SWIG_POINTER_EXCEPTION  0
#define SWIG_arg_fail(arg)      0

/* Describe the argument kinds for "You have:" messages */
SWIGRUNTIME std::string SWIG_Rust_TypeDesc(const SwigRV *v) {
  if (!v) return "null";
  switch (v->kind) {
  case SWIG_RV_NIL: return "None";
  case SWIG_RV_NUM: {
    const char *e = v->elem == SWIG_RV_F64 ? "f64" : v->elem == SWIG_RV_I64 ? "i64" : "bool";
    if (v->rows == 1 && v->cols == 1 && !v->sparse) return e;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%s(%lldx%lld)", v->sparse ? "sparse " : "", e, v->rows, v->cols);
    return buf;
  }
  case SWIG_RV_STR: return "str";
  case SWIG_RV_LIST: {
    std::string s = "[";
    for (size_t i = 0; i < v->items.size(); ++i) s += (i ? "," : "") + SWIG_Rust_TypeDesc(v->items[i]);
    return s + "]";
  }
  case SWIG_RV_DICT: {
    std::string s = "{";
    for (size_t i = 0; i < v->items.size(); ++i) s += (i ? "," : "") + v->keys[i] + ":" + SWIG_Rust_TypeDesc(v->items[i]);
    return s + "}";
  }
  case SWIG_RV_PTR: return v->type ? (v->type->str ? v->type->str : v->type->name) : "ptr";
  case SWIG_RV_FUN: return "fn";
  }
  return "?";
}

SWIGRUNTIME std::string SWIG_Rust_ArgsTypeDesc(int argc, SwigRV *argv[]) {
  std::string s;
  for (int i = 0; i < argc; ++i) s += (i ? "," : "") + SWIG_Rust_TypeDesc(argv[i]);
  return s;
}

#ifdef __cplusplus
#include <streambuf>
#include <iostream>
#endif
