// -----------------------------------------------------------------------------
// rustrun.rs: Rust side of the -rust runtime (guest values, errors, calls)
// -----------------------------------------------------------------------------

/// Dictionary of values of any type: dict!{"a" => 1.0, "b" => &x}
#[macro_export]
macro_rules! dict {
    () => { $crate::rt::Dict::new() };
    ($($k:expr => $v:expr),+ $(,)?) => {{
        let mut d = $crate::rt::Dict::new();
        $(d.insert($k, $v);)+
        d
    }};
}

#[allow(dead_code)]
pub mod rt {
    use std::collections::{BTreeMap, HashMap};
    use std::ffi::c_void;
    use std::os::raw::{c_char, c_int};

    #[repr(C)]
    pub struct SwigRV {
        _private: [u8; 0],
    }

    pub type WrapFn = unsafe extern "C" fn(c_int, *mut *mut SwigRV, c_int, *mut *mut SwigRV) -> c_int;
    pub type InvokeFn = unsafe extern "C" fn(*mut c_void, c_int, *mut *mut SwigRV) -> *mut SwigRV;
    pub type DropFn = unsafe extern "C" fn(*mut c_void);

    pub const NIL: i32 = 0;
    pub const NUM: i32 = 1;
    pub const STR: i32 = 2;
    pub const LIST: i32 = 3;
    pub const DICT: i32 = 4;
    pub const PTR: i32 = 5;
    pub const FUN: i32 = 6;
    pub const F64: i32 = 0;
    pub const I64: i32 = 1;
    pub const BOOL: i32 = 2;

    extern "C" {
        pub fn swig_rv_last_error_code() -> c_int;
        pub fn swig_rv_last_error_msg() -> *const c_char;
        pub fn swig_rv_clear_error();
        pub fn swig_rv_free(v: *mut SwigRV);
        pub fn swig_rv_new_nil() -> *mut SwigRV;
        pub fn swig_rv_new_f64(rows: i64, cols: i64, data: *const f64) -> *mut SwigRV;
        pub fn swig_rv_new_i64(rows: i64, cols: i64, data: *const i64) -> *mut SwigRV;
        pub fn swig_rv_new_bool(rows: i64, cols: i64, data: *const u8) -> *mut SwigRV;
        pub fn swig_rv_new_sparse(rows: i64, cols: i64, colind: *const i64, row: *const i64, data: *const f64) -> *mut SwigRV;
        pub fn swig_rv_new_str(s: *const c_char, n: usize) -> *mut SwigRV;
        pub fn swig_rv_new_list(n: usize) -> *mut SwigRV;
        pub fn swig_rv_list_set(l: *mut SwigRV, i: usize, item: *mut SwigRV);
        pub fn swig_rv_new_dict() -> *mut SwigRV;
        pub fn swig_rv_dict_push(d: *mut SwigRV, key: *const c_char, n: usize, item: *mut SwigRV);
        pub fn swig_rv_new_ptr(ptr: *mut c_void, ty: *mut c_void, own: c_int) -> *mut SwigRV;
        pub fn swig_rv_new_fun(ctx: *mut c_void, invoke: InvokeFn, drop: DropFn) -> *mut SwigRV;
        pub fn swig_rv_kind(v: *const SwigRV) -> c_int;
        pub fn swig_rv_elem(v: *const SwigRV) -> c_int;
        pub fn swig_rv_rows(v: *const SwigRV) -> i64;
        pub fn swig_rv_cols(v: *const SwigRV) -> i64;
        pub fn swig_rv_is_sparse(v: *const SwigRV) -> c_int;
        pub fn swig_rv_f64_data(v: *const SwigRV) -> *const f64;
        pub fn swig_rv_i64_data(v: *const SwigRV) -> *const i64;
        pub fn swig_rv_nnz(v: *const SwigRV) -> usize;
        pub fn swig_rv_colind(v: *const SwigRV) -> *const i64;
        pub fn swig_rv_row(v: *const SwigRV) -> *const i64;
        pub fn swig_rv_str(v: *const SwigRV, n: *mut usize) -> *const c_char;
        pub fn swig_rv_len(v: *const SwigRV) -> usize;
        pub fn swig_rv_item(v: *const SwigRV, i: usize) -> *mut SwigRV;
        pub fn swig_rv_key(v: *const SwigRV, i: usize, n: *mut usize) -> *const c_char;
        pub fn swig_rv_ptr(v: *const SwigRV) -> *mut c_void;
        pub fn swig_rv_type(v: *const SwigRV) -> *mut c_void;
        pub fn swig_rv_type_name(v: *const SwigRV) -> *const c_char;
        pub fn swig_rv_own(v: *const SwigRV) -> c_int;
        pub fn swig_rv_disown(v: *mut SwigRV);
        pub fn swig_rv_cast_ptr(v: *const SwigRV, ty: *mut c_void) -> *mut c_void;
        pub fn swig_rv_type_query(name: *const c_char) -> *mut c_void;
    }

    /// Error raised by a wrapped call
    #[derive(Debug, Clone)]
    pub struct Error {
        pub code: i32,
        pub message: String,
    }

    impl std::fmt::Display for Error {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str(&self.message)
        }
    }

    impl std::error::Error for Error {}

    fn last_error() -> Error {
        unsafe {
            let code = swig_rv_last_error_code();
            let p = swig_rv_last_error_msg();
            let message = if p.is_null() {
                String::new()
            } else {
                std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned()
            };
            Error { code, message }
        }
    }

    /// Owned guest value
    pub struct RV(pub *mut SwigRV);

    impl Drop for RV {
        fn drop(&mut self) {
            if !self.0.is_null() {
                unsafe { swig_rv_free(self.0) }
            }
        }
    }

    impl RV {
        pub fn into_raw(self) -> *mut SwigRV {
            let p = self.0;
            std::mem::forget(self);
            p
        }
        pub fn kind(&self) -> i32 {
            unsafe { swig_rv_kind(self.0) }
        }
        pub fn nil() -> RV {
            RV(unsafe { swig_rv_new_nil() })
        }
        pub fn list(items: Vec<RV>) -> RV {
            unsafe {
                let l = swig_rv_new_list(items.len());
                for (i, it) in items.into_iter().enumerate() {
                    swig_rv_list_set(l, i, it.into_raw());
                }
                RV(l)
            }
        }
        pub fn dict<'a, I: IntoIterator<Item = (&'a str, RV)>>(items: I) -> RV {
            unsafe {
                let d = swig_rv_new_dict();
                for (k, v) in items {
                    swig_rv_dict_push(d, k.as_ptr() as *const c_char, k.len(), v.into_raw());
                }
                RV(d)
            }
        }
        pub fn f64_matrix(rows: usize, cols: usize, colmajor: &[f64]) -> RV {
            RV(unsafe { swig_rv_new_f64(rows as i64, cols as i64, colmajor.as_ptr()) })
        }
        pub fn ptr(ptr: *mut c_void, ty: *mut c_void, own: bool) -> RV {
            RV(unsafe { swig_rv_new_ptr(ptr, ty, own as c_int) })
        }
        pub fn type_desc(&self) -> String {
            match self.kind() {
                NIL => "None".into(),
                NUM => format!("number({}x{})", unsafe { swig_rv_rows(self.0) }, unsafe { swig_rv_cols(self.0) }),
                STR => "str".into(),
                LIST => "list".into(),
                DICT => "dict".into(),
                PTR => unsafe { std::ffi::CStr::from_ptr(swig_rv_type_name(self.0)).to_string_lossy().into_owned() },
                _ => "fn".into(),
            }
        }
    }

    /// Borrowed view, e.g. a list item
    #[derive(Clone, Copy)]
    pub struct RVRef(pub *mut SwigRV);

    impl RVRef {
        pub fn kind(&self) -> i32 {
            unsafe { swig_rv_kind(self.0) }
        }
        pub fn len(&self) -> usize {
            unsafe { swig_rv_len(self.0) }
        }
        pub fn item(&self, i: usize) -> RVRef {
            RVRef(unsafe { swig_rv_item(self.0, i) })
        }
        pub fn key(&self, i: usize) -> String {
            unsafe {
                let mut n = 0usize;
                let p = swig_rv_key(self.0, i, &mut n);
                String::from_utf8_lossy(std::slice::from_raw_parts(p as *const u8, n)).into_owned()
            }
        }
    }

    /// Host value to guest value
    pub trait ToRV {
        fn to_rv(&self) -> RV;
        /// Conversion of a slice of Self; numeric types override it with a dense column
        fn slice_to_rv(s: &[Self]) -> RV
        where
            Self: Sized,
        {
            RV::list(s.iter().map(|e| e.to_rv()).collect())
        }
    }

    impl<T: ToRV + ?Sized> ToRV for &T {
        fn to_rv(&self) -> RV {
            (**self).to_rv()
        }
    }

    impl ToRV for f64 {
        fn to_rv(&self) -> RV {
            RV(unsafe { swig_rv_new_f64(1, 1, self) })
        }
        fn slice_to_rv(s: &[f64]) -> RV {
            RV(unsafe { swig_rv_new_f64(s.len() as i64, 1, s.as_ptr()) })
        }
    }

    macro_rules! int_to_rv {
        ($($t:ty),*) => {$(
            impl ToRV for $t {
                fn to_rv(&self) -> RV {
                    let v = *self as i64;
                    RV(unsafe { swig_rv_new_i64(1, 1, &v) })
                }
                fn slice_to_rv(s: &[$t]) -> RV {
                    let v: Vec<i64> = s.iter().map(|&e| e as i64).collect();
                    RV(unsafe { swig_rv_new_i64(v.len() as i64, 1, v.as_ptr()) })
                }
            }
        )*};
    }
    int_to_rv!(i8, i16, i32, i64, isize, u8, u16, u32, u64, usize);

    impl ToRV for bool {
        fn to_rv(&self) -> RV {
            let v = *self as u8;
            RV(unsafe { swig_rv_new_bool(1, 1, &v) })
        }
        fn slice_to_rv(s: &[bool]) -> RV {
            let v: Vec<u8> = s.iter().map(|&e| e as u8).collect();
            RV(unsafe { swig_rv_new_bool(v.len() as i64, 1, v.as_ptr()) })
        }
    }

    impl ToRV for str {
        fn to_rv(&self) -> RV {
            RV(unsafe { swig_rv_new_str(self.as_ptr() as *const c_char, self.len()) })
        }
    }

    impl ToRV for String {
        fn to_rv(&self) -> RV {
            self.as_str().to_rv()
        }
    }

    impl<T: ToRV> ToRV for [T] {
        fn to_rv(&self) -> RV {
            T::slice_to_rv(self)
        }
    }

    impl<T: ToRV> ToRV for Vec<T> {
        fn to_rv(&self) -> RV {
            T::slice_to_rv(self)
        }
    }

    impl<T: ToRV, const N: usize> ToRV for [T; N] {
        fn to_rv(&self) -> RV {
            T::slice_to_rv(self)
        }
    }

    impl<T: ToRV> ToRV for Option<T> {
        fn to_rv(&self) -> RV {
            match self {
                Some(v) => v.to_rv(),
                None => RV::nil(),
            }
        }
    }

    impl<K: AsRef<str>, V: ToRV> ToRV for HashMap<K, V> {
        fn to_rv(&self) -> RV {
            RV::dict(self.iter().map(|(k, v)| (k.as_ref(), v.to_rv())))
        }
    }

    impl<K: AsRef<str>, V: ToRV> ToRV for BTreeMap<K, V> {
        fn to_rv(&self) -> RV {
            RV::dict(self.iter().map(|(k, v)| (k.as_ref(), v.to_rv())))
        }
    }

    /// Guest value to host value; panics on a kind mismatch
    pub trait FromRV: Sized {
        fn from_rv(v: RVRef) -> Option<Self>;
    }

    pub fn take<T: FromRV>(v: RV) -> T {
        let desc = v.type_desc();
        match T::from_rv(RVRef(v.0)) {
            Some(r) => r,
            None => panic!("cannot convert {} to {}", desc, std::any::type_name::<T>()),
        }
    }

    impl FromRV for RV {
        fn from_rv(v: RVRef) -> Option<Self> {
            // Takes over a pointer's ownership only through the generated proxies
            let _ = v;
            None
        }
    }

    fn num_scalar(v: RVRef) -> Option<(i32, f64, i64)> {
        unsafe {
            if swig_rv_kind(v.0) != NUM || swig_rv_rows(v.0) != 1 || swig_rv_cols(v.0) != 1 || swig_rv_is_sparse(v.0) != 0 {
                return None;
            }
            let e = swig_rv_elem(v.0);
            if e == F64 {
                let x = *swig_rv_f64_data(v.0);
                Some((e, x, x as i64))
            } else {
                let x = *swig_rv_i64_data(v.0);
                Some((e, x as f64, x))
            }
        }
    }

    impl FromRV for f64 {
        fn from_rv(v: RVRef) -> Option<Self> {
            match num_scalar(v) {
                Some((e, x, _)) if e != BOOL => Some(x),
                _ => None,
            }
        }
    }

    macro_rules! int_from_rv {
        ($($t:ty),*) => {$(
            impl FromRV for $t {
                fn from_rv(v: RVRef) -> Option<Self> {
                    match num_scalar(v) {
                        Some((I64, _, i)) => <$t>::try_from(i).ok(),
                        _ => None,
                    }
                }
            }
        )*};
    }
    int_from_rv!(i32, i64, isize, u32, u64, usize);

    impl FromRV for bool {
        fn from_rv(v: RVRef) -> Option<Self> {
            match num_scalar(v) {
                Some((BOOL, _, i)) => Some(i != 0),
                _ => None,
            }
        }
    }

    impl FromRV for String {
        fn from_rv(v: RVRef) -> Option<Self> {
            unsafe {
                if swig_rv_kind(v.0) != STR {
                    return None;
                }
                let mut n = 0usize;
                let p = swig_rv_str(v.0, &mut n);
                Some(String::from_utf8_lossy(std::slice::from_raw_parts(p as *const u8, n)).into_owned())
            }
        }
    }

    impl<T: FromRV> FromRV for Vec<T> {
        fn from_rv(v: RVRef) -> Option<Self> {
            unsafe {
                match swig_rv_kind(v.0) {
                    LIST => (0..v.len()).map(|i| T::from_rv(v.item(i))).collect(),
                    NUM if swig_rv_is_sparse(v.0) == 0 && (swig_rv_rows(v.0) == 1 || swig_rv_cols(v.0) == 1) => {
                        // Dense vector of scalars: convert element-wise through 1x1 values
                        let n = (swig_rv_rows(v.0) * swig_rv_cols(v.0)) as usize;
                        let e = swig_rv_elem(v.0);
                        let mut out = Vec::with_capacity(n);
                        for k in 0..n {
                            let item = if e == F64 {
                                RV(swig_rv_new_f64(1, 1, swig_rv_f64_data(v.0).add(k)))
                            } else if e == I64 {
                                RV(swig_rv_new_i64(1, 1, swig_rv_i64_data(v.0).add(k)))
                            } else {
                                let b = (*swig_rv_i64_data(v.0).add(k)) as u8;
                                RV(swig_rv_new_bool(1, 1, &b))
                            };
                            out.push(T::from_rv(RVRef(item.0))?);
                        }
                        Some(out)
                    }
                    _ => None,
                }
            }
        }
    }

    impl<T: FromRV> FromRV for HashMap<String, T> {
        fn from_rv(v: RVRef) -> Option<Self> {
            if v.kind() != DICT {
                return None;
            }
            (0..v.len()).map(|i| Some((v.key(i), T::from_rv(v.item(i))?))).collect()
        }
    }

    impl<T: FromRV> FromRV for Option<T> {
        fn from_rv(v: RVRef) -> Option<Self> {
            if v.kind() == NIL {
                Some(None)
            } else {
                T::from_rv(v).map(Some)
            }
        }
    }

    impl FromRV for () {
        fn from_rv(v: RVRef) -> Option<Self> {
            if v.kind() == NIL {
                Some(())
            } else {
                None
            }
        }
    }

    /// Dynamically typed value, e.g. a GenericType
    #[derive(Debug, Clone, PartialEq)]
    pub enum Value {
        None,
        Bool(bool),
        Int(i64),
        Float(f64),
        /// Numeric matrix, column-major (rows, cols, data)
        Matrix(usize, usize, Vec<f64>),
        Str(String),
        List(Vec<Value>),
        Dict(BTreeMap<String, Value>),
        /// Wrapped object, by type name
        Object(String),
    }

    impl FromRV for Value {
        fn from_rv(v: RVRef) -> Option<Self> {
            unsafe {
                Some(match v.kind() {
                    NIL => Value::None,
                    NUM => {
                        let (r, c) = (swig_rv_rows(v.0) as usize, swig_rv_cols(v.0) as usize);
                        let e = swig_rv_elem(v.0);
                        if r == 1 && c == 1 && swig_rv_is_sparse(v.0) == 0 {
                            match e {
                                F64 => Value::Float(*swig_rv_f64_data(v.0)),
                                I64 => Value::Int(*swig_rv_i64_data(v.0)),
                                _ => Value::Bool(*swig_rv_i64_data(v.0) != 0),
                            }
                        } else if swig_rv_is_sparse(v.0) == 0 && (r <= 1 || c <= 1) && e != F64 {
                            let d = std::slice::from_raw_parts(swig_rv_i64_data(v.0), r * c);
                            Value::List(d.iter().map(|&x| if e == I64 { Value::Int(x) } else { Value::Bool(x != 0) }).collect())
                        } else {
                            let nnz = swig_rv_nnz(v.0);
                            let mut d = vec![0.0; r * c];
                            if swig_rv_is_sparse(v.0) != 0 {
                                let ci = std::slice::from_raw_parts(swig_rv_colind(v.0), c + 1);
                                let ri = std::slice::from_raw_parts(swig_rv_row(v.0), nnz);
                                let nz = std::slice::from_raw_parts(swig_rv_f64_data(v.0), nnz);
                                for j in 0..c {
                                    for k in ci[j] as usize..ci[j + 1] as usize {
                                        d[ri[k] as usize + j * r] = nz[k];
                                    }
                                }
                            } else if e == F64 {
                                d.copy_from_slice(std::slice::from_raw_parts(swig_rv_f64_data(v.0), r * c));
                            } else {
                                let s = std::slice::from_raw_parts(swig_rv_i64_data(v.0), r * c);
                                for k in 0..r * c {
                                    d[k] = s[k] as f64;
                                }
                            }
                            Value::Matrix(r, c, d)
                        }
                    }
                    STR => Value::Str(String::from_rv(v)?),
                    LIST => Value::List((0..v.len()).map(|i| Value::from_rv(v.item(i))).collect::<Option<_>>()?),
                    DICT => Value::Dict((0..v.len()).map(|i| Some((v.key(i), Value::from_rv(v.item(i))?))).collect::<Option<_>>()?),
                    PTR => Value::Object(std::ffi::CStr::from_ptr(swig_rv_type_name(v.0)).to_string_lossy().into_owned()),
                    _ => return None,
                })
            }
        }
    }

    impl ToRV for Value {
        fn to_rv(&self) -> RV {
            match self {
                Value::None => RV::nil(),
                Value::Bool(b) => b.to_rv(),
                Value::Int(i) => i.to_rv(),
                Value::Float(x) => x.to_rv(),
                Value::Matrix(r, c, d) => RV::f64_matrix(*r, *c, d),
                Value::Str(s) => s.to_rv(),
                Value::List(l) => RV::list(l.iter().map(|e| e.to_rv()).collect()),
                Value::Dict(d) => RV::dict(d.iter().map(|(k, v)| (k.as_str(), v.to_rv()))),
                Value::Object(n) => panic!("cannot pass a {} back by type name", n),
            }
        }
    }

    /// Kinds of argument values: overloads are resolved on these at compile time
    pub mod kind {
        use std::marker::PhantomData;
        pub struct F64;
        pub struct I64;
        pub struct Bool;
        pub struct Str;
        pub struct Dict;
        pub struct Vec<K>(PhantomData<K>);
        pub struct Map<K>(PhantomData<K>);
        pub struct Pair<A, B>(PhantomData<(A, B)>);
    }

    /// Value with a kind
    pub trait HasKind {
        type K;
    }

    impl<T: HasKind + ?Sized> HasKind for &T {
        type K = T::K;
    }

    impl<T: HasKind + ?Sized> HasKind for &mut T {
        type K = T::K;
    }

    macro_rules! has_kind {
        ($k:ident: $($t:ty),*) => {$(
            impl HasKind for $t {
                type K = kind::$k;
            }
        )*};
    }
    has_kind!(F64: f64, f32);
    has_kind!(I64: i8, i16, i32, i64, isize, u8, u16, u32, u64, usize);
    has_kind!(Bool: bool);
    has_kind!(Str: str, String);

    impl<X: HasKind> HasKind for Vec<X> {
        type K = kind::Vec<X::K>;
    }

    impl<X: HasKind> HasKind for [X] {
        type K = kind::Vec<X::K>;
    }

    impl<X: HasKind, const N: usize> HasKind for [X; N] {
        type K = kind::Vec<X::K>;
    }

    impl<S: AsRef<str>, X: HasKind> HasKind for HashMap<S, X> {
        type K = kind::Map<X::K>;
    }

    impl<S: AsRef<str>, X: HasKind> HasKind for BTreeMap<S, X> {
        type K = kind::Map<X::K>;
    }

    impl<A: HasKind, B: HasKind> HasKind for (A, B) {
        type K = kind::Pair<A::K, B::K>;
    }

    impl ToRV for f32 {
        fn to_rv(&self) -> RV {
            (*self as f64).to_rv()
        }
    }

    impl<A: ToRV, B: ToRV> ToRV for (A, B) {
        fn to_rv(&self) -> RV {
            RV::list(vec![self.0.to_rv(), self.1.to_rv()])
        }
    }

    impl<A: FromRV, B: FromRV> FromRV for (A, B) {
        fn from_rv(v: RVRef) -> Option<Self> {
            if v.kind() != LIST || v.len() != 2 {
                return None;
            }
            Some((A::from_rv(v.item(0))?, B::from_rv(v.item(1))?))
        }
    }

    /// Set of overload candidates, bit c for candidate c
    pub struct Mask<const B: u64>;

    /// Candidates accepting both
    pub trait Join<M> {
        type Out;
    }

    /// Dictionary with values of any type, see the dict! macro
    pub struct Dict<'a>(pub Vec<(String, Box<dyn ToRV + 'a>)>);

    impl<'a> Dict<'a> {
        pub fn new() -> Self {
            Dict(Vec::new())
        }
        pub fn insert(&mut self, key: &str, value: impl ToRV + 'a) {
            self.0.push((key.to_string(), Box::new(value)));
        }
    }

    impl Default for Dict<'_> {
        fn default() -> Self {
            Dict::new()
        }
    }

    impl ToRV for Dict<'_> {
        fn to_rv(&self) -> RV {
            RV::dict(self.0.iter().map(|(k, v)| (k.as_str(), v.to_rv())))
        }
    }

    impl HasKind for Dict<'_> {
        type K = kind::Dict;
    }

    /// The first output
    pub fn out1<T: FromRV>(o: Vec<RV>) -> T {
        take(o.into_iter().next().expect("missing output"))
    }

    /// Wrapped C++ object; deletes it on drop when owned
    pub struct Handle {
        pub ptr: *mut c_void,
        pub ty: *mut c_void,
        pub own: bool,
        pub del: Option<WrapFn>,
    }

    impl Handle {
        pub fn to_rv(&self) -> RV {
            RV::ptr(self.ptr, self.ty, false)
        }
        /// Take over the pointer held by v, converting it to type ty
        pub fn from_rv(v: RVRef, ty: *mut c_void, del: Option<WrapFn>) -> Option<Handle> {
            unsafe {
                if swig_rv_kind(v.0) != PTR {
                    return None;
                }
                let p = swig_rv_cast_ptr(v.0, ty);
                if p.is_null() {
                    return None;
                }
                let own = swig_rv_own(v.0) != 0;
                swig_rv_disown(v.0);
                Some(Handle { ptr: p, ty, own, del })
            }
        }
    }

    impl Drop for Handle {
        fn drop(&mut self) {
            if self.own {
                if let Some(del) = self.del {
                    let _ = call(del, vec![RV::ptr(self.ptr, self.ty, true)], 1);
                }
            }
        }
    }

    /// Run the module initialization once
    pub fn init() {
        static ONCE: std::sync::Once = std::sync::Once::new();
        ONCE.call_once(|| unsafe { super::ffi::swig_rust_init() });
    }

    pub fn type_query(name: &str) -> *mut c_void {
        init();
        let c = std::ffi::CString::new(name).unwrap();
        let t = unsafe { swig_rv_type_query(c.as_ptr()) };
        assert!(!t.is_null(), "unknown SWIG type {}", name);
        t
    }

    /// Call a wrapper with converted arguments; returns all outputs
    pub fn call(f: WrapFn, args: Vec<RV>, nout: usize) -> Result<Vec<RV>, Error> {
        init();
        let mut argv: Vec<*mut SwigRV> = args.into_iter().map(|a| a.into_raw()).collect();
        let mut resv: Vec<*mut SwigRV> = vec![std::ptr::null_mut(); nout.max(1)];
        let flag = unsafe {
            swig_rv_clear_error();
            f(resv.len() as c_int, resv.as_mut_ptr(), argv.len() as c_int, argv.as_mut_ptr())
        };
        for a in argv {
            unsafe { swig_rv_free(a) };
        }
        let out: Vec<RV> = resv.into_iter().filter(|p| !p.is_null()).map(RV).collect();
        if flag != 0 {
            return Err(last_error());
        }
        Ok(out)
    }

    /// As call, but panics on error
    pub fn call_or_panic(f: WrapFn, args: Vec<RV>, nout: usize) -> Vec<RV> {
        match call(f, args, nout) {
            Ok(r) => r,
            Err(e) => panic!("{}", e),
        }
    }
}
