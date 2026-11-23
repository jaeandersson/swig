// -----------------------------------------------------------------------------
// csrun.cs: C# side of the -dotnet runtime (guest values, errors, calls,
// proxy lifetime, director upcalls).  Spliced into the generated .cs inside
// the module namespace; SwigNative.Lib names the wrapper library and
// SwigProxies.Register() the proxy factories, both generated.
// -----------------------------------------------------------------------------

/// <summary>Error raised by a wrapped C++ call</summary>
public class SwigException : System.Exception {
  /// <summary>SWIG error code (SWIG_TypeError, SWIG_RuntimeError, ...)</summary>
  public int Code { get; }
  public SwigException(int code, string message) : base(message) { Code = code; }
  /// <summary>A runtime error (SWIG_RuntimeError)</summary>
  public SwigException(string message) : this(-3, message) {}
}

/// <summary>Base of the proxy classes: a C++ object, owned or borrowed</summary>
public abstract class SwigObject : System.IDisposable {
  internal System.IntPtr swigPtr;
  internal bool swigOwn;

  protected SwigObject(SwigRT.Owned o) {
    swigPtr = o.Ptr;
    swigOwn = o.Own;
  }

  /// <summary>swig_type_info* of the proxy class</summary>
  internal abstract System.IntPtr SwigTypeInfo { get; }
  /// <summary>Delete wrapper of the proxy class, or null when C++ has none</summary>
  internal abstract SwigRT.WrapFn SwigDeleter { get; }

  /// <summary>Whether this proxy refers to a C++ object</summary>
  public bool IsValid => swigPtr != System.IntPtr.Zero;

  /// <summary>Deletes the C++ object now, if this proxy owns it</summary>
  public void Dispose() {
    System.IntPtr p = swigPtr;
    swigPtr = System.IntPtr.Zero;
    if (p != System.IntPtr.Zero && swigOwn && SwigDeleter != null) SwigRT.Delete(SwigDeleter, p, SwigTypeInfo);
    System.GC.SuppressFinalize(this);
  }

  // Finalizers only queue the delete: it runs on an application thread, one at a time
  ~SwigObject() {
    if (swigPtr != System.IntPtr.Zero && swigOwn && SwigDeleter != null) SwigRT.QueueDelete(SwigDeleter, swigPtr, SwigTypeInfo);
  }

  internal System.IntPtr SwigPtrChecked() {
    if (swigPtr == System.IntPtr.Zero) throw new System.ObjectDisposedException(GetType().Name);
    return swigPtr;
  }
}

/// <summary>Runtime of the generated bindings; not part of the public API</summary>
public static class SwigRT {
  internal const int NIL = 0, NUM = 1, STR = 2, LIST = 3, DICT = 4, PTR = 5, FUN = 6, BASE = 7, ERR = 8;
  internal const int F64 = 0, I64 = 1, BOOL = 2;

  [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
  public delegate int WrapFn(int resc, [System.Runtime.InteropServices.In, System.Runtime.InteropServices.Out] System.IntPtr[] resv, int argc, [System.Runtime.InteropServices.In] System.IntPtr[] argv);
  [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
  internal delegate System.IntPtr InvokeFn(System.IntPtr ctx, int argc, System.IntPtr argv);
  [System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
  internal delegate void DropFn(System.IntPtr ctx);

  const string L = SwigNative.Lib;
  const System.Runtime.InteropServices.CallingConvention C = System.Runtime.InteropServices.CallingConvention.Cdecl;
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern void swig_dotnet_init();
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern int swig_rv_last_error_code();
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_last_error_msg();
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern void swig_rv_clear_error();
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern void swig_rv_free(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_nil();
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_f64(long rows, long cols, double[] data);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_i64(long rows, long cols, long[] data);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_bool(long rows, long cols, byte[] data);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_str(byte[] s, System.UIntPtr n);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_base();
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_err(byte[] s, System.UIntPtr n);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_list(System.UIntPtr n);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern void swig_rv_list_set(System.IntPtr l, System.UIntPtr i, System.IntPtr item);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_dict();
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern void swig_rv_dict_push(System.IntPtr d, byte[] key, System.UIntPtr n, System.IntPtr item);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_ptr(System.IntPtr ptr, System.IntPtr type, int own);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_new_fun(System.IntPtr ctx, InvokeFn invoke, DropFn drop);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern int swig_rv_kind(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern int swig_rv_elem(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern long swig_rv_rows(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern long swig_rv_cols(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern int swig_rv_is_sparse(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern int swig_rv_is_seq(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_f64_data(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_i64_data(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.UIntPtr swig_rv_nnz(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_colind(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_row(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_str(System.IntPtr v, out System.UIntPtr n);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.UIntPtr swig_rv_len(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_item(System.IntPtr v, System.UIntPtr i);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_key(System.IntPtr v, System.UIntPtr i, out System.UIntPtr n);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_ptr(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_type(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_type_name(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern int swig_rv_own(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern void swig_rv_disown(System.IntPtr v);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_cast_ptr(System.IntPtr v, System.IntPtr type);
  [System.Runtime.InteropServices.DllImport(L, CallingConvention = C)] static extern System.IntPtr swig_rv_type_query([System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.LPUTF8Str)] string name);

  /// <summary>A C++ object handed to a proxy constructor</summary>
  public readonly struct Owned {
    public readonly System.IntPtr Ptr;
    public readonly bool Own;
    internal Owned(System.IntPtr ptr, bool own) { Ptr = ptr; Own = own; }
    /// <summary>No object yet: the constructor body fills it in</summary>
    public static readonly Owned Pending = new Owned(System.IntPtr.Zero, false);
  }

  // ---- initialization, proxy factories, type lookup ----

  static readonly object initLock = new object();
  static volatile bool initialized;
  static readonly System.Collections.Generic.Dictionary<string, System.Func<Owned, SwigObject>> factories =
    new System.Collections.Generic.Dictionary<string, System.Func<Owned, SwigObject>>();
  static readonly System.Collections.Concurrent.ConcurrentDictionary<System.Type, System.Func<Owned, SwigObject>> ctorCache =
    new System.Collections.Concurrent.ConcurrentDictionary<System.Type, System.Func<Owned, SwigObject>>();

  internal static void Init() {
    if (initialized) return;
    lock (initLock) {
      if (initialized) return;
      swig_dotnet_init();
      SwigProxies.Register();
      initialized = true;
    }
  }

  internal static void Register(string mangled, System.Func<Owned, SwigObject> factory) { factories[mangled] = factory; }

  /// <summary>swig_type_info* by mangled name (_p_Foo)</summary>
  public static System.IntPtr TypeQuery(string mangled) {
    Init();
    System.IntPtr t = swig_rv_type_query(mangled);
    if (t == System.IntPtr.Zero) throw new System.InvalidOperationException("unknown SWIG type " + mangled);
    return t;
  }

  // ---- errors ----

  static SwigException LastError() {
    System.IntPtr p = swig_rv_last_error_msg();
    string msg = p == System.IntPtr.Zero ? "" : System.Runtime.InteropServices.Marshal.PtrToStringUTF8(p);
    return new SwigException(swig_rv_last_error_code(), msg);
  }

  // ---- deletes: immediate (Dispose) or queued (finalizers) ----

  static readonly object deleteLock = new object();
  static readonly System.Collections.Concurrent.ConcurrentQueue<(WrapFn, System.IntPtr, System.IntPtr)> deleteQueue =
    new System.Collections.Concurrent.ConcurrentQueue<(WrapFn, System.IntPtr, System.IntPtr)>();

  internal static void QueueDelete(WrapFn del, System.IntPtr ptr, System.IntPtr type) { deleteQueue.Enqueue((del, ptr, type)); }

  internal static void Delete(WrapFn del, System.IntPtr ptr, System.IntPtr type) {
    lock (deleteLock) RawDelete(del, ptr, type);
  }

  static void RawDelete(WrapFn del, System.IntPtr ptr, System.IntPtr type) {
    System.IntPtr[] argv = { swig_rv_new_ptr(ptr, type, 1) };
    System.IntPtr[] resv = new System.IntPtr[1];
    swig_rv_clear_error();
    del(1, resv, 1, argv);
    swig_rv_free(argv[0]);
    if (resv[0] != System.IntPtr.Zero) swig_rv_free(resv[0]);
  }

  /// <summary>Runs the deletes queued by finalizers; called before every wrapped call</summary>
  public static void RunQueuedDeletes() {
    if (deleteQueue.IsEmpty) return;
    if (!System.Threading.Monitor.TryEnter(deleteLock)) return;
    try {
      while (deleteQueue.TryDequeue(out var d)) RawDelete(d.Item1, d.Item2, d.Item3);
    } finally {
      System.Threading.Monitor.Exit(deleteLock);
    }
  }

  // ---- calls ----

  /// <summary>Calls a wrapper; returns its outputs (each to be taken with Take)</summary>
  public static System.IntPtr[] Call(WrapFn f, int nout, params object[] args) {
    Init();
    RunQueuedDeletes();
    System.IntPtr[] argv = new System.IntPtr[args.Length];
    try {
      for (int i = 0; i < args.Length; ++i) argv[i] = ToRV(args[i]);
      System.IntPtr[] resv = new System.IntPtr[nout < 1 ? 1 : nout];
      swig_rv_clear_error();
      int flag = f(resv.Length, resv, argv.Length, argv);
      System.GC.KeepAlive(args);
      if (flag != 0) {
        Release(resv);
        throw LastError();
      }
      return resv;
    } finally {
      foreach (System.IntPtr a in argv) if (a != System.IntPtr.Zero) swig_rv_free(a);
    }
  }

  /// <summary>Output i converted to T; frees it</summary>
  public static T Take<T>(System.IntPtr[] r, int i) {
    System.IntPtr v = r[i];
    r[i] = System.IntPtr.Zero;
    try {
      return (T)FromRV(v, typeof(T));
    } finally {
      if (v != System.IntPtr.Zero) {
        ReleaseOwned(v);
        swig_rv_free(v);
      }
    }
  }

  /// <summary>Frees the outputs not taken</summary>
  public static void Release(System.IntPtr[] r) {
    for (int i = 0; i < r.Length; ++i) {
      if (r[i] == System.IntPtr.Zero) continue;
      ReleaseOwned(r[i]);
      swig_rv_free(r[i]);
      r[i] = System.IntPtr.Zero;
    }
  }

  /// <summary>Call returning nothing</summary>
  public static void CallVoid(WrapFn f, int nout, params object[] args) { Release(Call(f, nout, args)); }

  /// <summary>Call returning its first output as T</summary>
  public static T Call1<T>(WrapFn f, int nout, params object[] args) {
    System.IntPtr[] r = Call(f, nout, args);
    try {
      return Take<T>(r, 0);
    } finally {
      Release(r);
    }
  }

  /// <summary>Constructor body: call the constructor wrapper, then adopt its object</summary>
  public static void Construct(SwigObject self, WrapFn f, params object[] args) {
    System.IntPtr[] r = Call(f, 1, args);
    try {
      System.IntPtr v = r[0];
      if (swig_rv_kind(v) != PTR) throw new SwigException(-3, "constructor of " + self.GetType().Name + " returned no object");
      System.IntPtr p = swig_rv_cast_ptr(v, self.SwigTypeInfo);
      if (p == System.IntPtr.Zero) p = swig_rv_ptr(v);
      self.swigPtr = p;
      self.swigOwn = swig_rv_own(v) != 0;
      swig_rv_disown(v);
    } finally {
      Release(r);
    }
  }

  /// <summary>Deletes objects a value still owns: nobody took them over</summary>
  static void ReleaseOwned(System.IntPtr v) {
    switch (swig_rv_kind(v)) {
      case PTR:
        if (swig_rv_own(v) != 0) {
          SwigObject o = AdoptPtr(v, typeof(SwigObject));
          if (o != null) o.Dispose();
        }
        break;
      case LIST:
      case DICT:
        ulong n = (ulong)swig_rv_len(v);
        for (ulong i = 0; i < n; ++i) ReleaseOwned(swig_rv_item(v, (System.UIntPtr)i));
        break;
    }
  }

  // ---- host value -> guest value ----

  static byte[] Utf8(string s) { return System.Text.Encoding.UTF8.GetBytes(s); }

  static System.IntPtr NewStr(string s) {
    byte[] b = Utf8(s);
    return swig_rv_new_str(b, (System.UIntPtr)b.Length);
  }

  /// <summary>Marker argument of director constructors: the object whose overrides C++ calls</summary>
  public sealed class DirectorSelf {
    internal readonly SwigObject Self;
    internal readonly System.Type Declaring;
    public DirectorSelf(SwigObject self, System.Type declaring) { Self = self; Declaring = declaring; }
  }

  /// <summary>Converts a C# value to a new guest value</summary>
  public static System.IntPtr ToRV(object o) {
    switch (o) {
      case null: return swig_rv_new_nil();
      case SwigObject s: return swig_rv_new_ptr(s.SwigPtrChecked(), s.SwigTypeInfo, 0);
      case DirectorSelf d: return d.Self.GetType() == d.Declaring ? swig_rv_new_nil() : NewDirector(d.Self, d.Declaring);
      case string s: return NewStr(s);
      case char c: return NewStr(c.ToString());
      case bool b: return swig_rv_new_bool(1, 1, new byte[] { (byte)(b ? 1 : 0) });
      case double x: return swig_rv_new_f64(1, 1, new[] { x });
      case float x: return swig_rv_new_f64(1, 1, new[] { (double)x });
      case decimal x: return swig_rv_new_f64(1, 1, new[] { (double)x });
      case System.Enum e: return swig_rv_new_i64(1, 1, new[] { System.Convert.ToInt64(e) });
      case sbyte _: case byte _: case short _: case ushort _: case int _: case uint _: case long _:
        return swig_rv_new_i64(1, 1, new[] { System.Convert.ToInt64(o) });
      case ulong x: return swig_rv_new_i64(1, 1, new[] { (long)x });
      case double[] a: return swig_rv_new_f64(a.Length, 1, a);
      case float[] a: return swig_rv_new_f64(a.Length, 1, System.Array.ConvertAll(a, x => (double)x));
      case long[] a: return swig_rv_new_i64(a.Length, 1, a);
      case int[] a: return swig_rv_new_i64(a.Length, 1, System.Array.ConvertAll(a, x => (long)x));
      case bool[] a: return swig_rv_new_bool(a.Length, 1, System.Array.ConvertAll(a, x => (byte)(x ? 1 : 0)));
      case double[,] m: return swig_rv_new_f64(m.GetLength(0), m.GetLength(1), ColMajor(m, x => x));
      case float[,] m: return swig_rv_new_f64(m.GetLength(0), m.GetLength(1), ColMajor(m, x => (double)x));
      case long[,] m: return swig_rv_new_i64(m.GetLength(0), m.GetLength(1), ColMajor(m, x => x));
      case int[,] m: return swig_rv_new_i64(m.GetLength(0), m.GetLength(1), ColMajor(m, x => (long)x));
      case bool[,] m: return swig_rv_new_bool(m.GetLength(0), m.GetLength(1), ColMajor(m, x => (byte)(x ? 1 : 0)));
      case System.Collections.IDictionary d: {
        System.IntPtr r = swig_rv_new_dict();
        foreach (System.Collections.DictionaryEntry e in d) {
          byte[] k = Utf8(System.Convert.ToString(e.Key, System.Globalization.CultureInfo.InvariantCulture));
          swig_rv_dict_push(r, k, (System.UIntPtr)k.Length, ToRV(e.Value));
        }
        return r;
      }
      case System.Runtime.CompilerServices.ITuple t: {
        System.IntPtr r = swig_rv_new_list((System.UIntPtr)t.Length);
        for (int i = 0; i < t.Length; ++i) swig_rv_list_set(r, (System.UIntPtr)i, ToRV(t[i]));
        return r;
      }
      case System.Collections.Generic.IEnumerable<double> e: { double[] a = System.Linq.Enumerable.ToArray(e); return swig_rv_new_f64(a.Length, 1, a); }
      case System.Collections.Generic.IEnumerable<long> e: { long[] a = System.Linq.Enumerable.ToArray(e); return swig_rv_new_i64(a.Length, 1, a); }
      case System.Collections.Generic.IEnumerable<int> e: { long[] a = System.Linq.Enumerable.ToArray(System.Linq.Enumerable.Select(e, x => (long)x)); return swig_rv_new_i64(a.Length, 1, a); }
      case System.Collections.Generic.IEnumerable<bool> e: { byte[] a = System.Linq.Enumerable.ToArray(System.Linq.Enumerable.Select(e, x => (byte)(x ? 1 : 0))); return swig_rv_new_bool(a.Length, 1, a); }
      case System.Collections.IEnumerable e: {
        var items = new System.Collections.Generic.List<object>();
        foreach (object x in e) items.Add(x);
        System.IntPtr r = swig_rv_new_list((System.UIntPtr)items.Count);
        for (int i = 0; i < items.Count; ++i) swig_rv_list_set(r, (System.UIntPtr)i, ToRV(items[i]));
        return r;
      }
    }
    throw new System.ArgumentException("cannot pass a " + o.GetType().FullName + " to C++");
  }

  static R[] ColMajor<T, R>(T[,] m, System.Func<T, R> f) {
    int nr = m.GetLength(0), nc = m.GetLength(1);
    R[] r = new R[nr * nc];
    for (int j = 0; j < nc; ++j) for (int i = 0; i < nr; ++i) r[i + j * nr] = f(m[i, j]);
    return r;
  }

  // ---- guest value -> host value ----

  static string Str(System.IntPtr v) {
    System.IntPtr p = swig_rv_str(v, out System.UIntPtr n);
    return Utf8String(p, (int)(ulong)n);
  }

  static string Utf8String(System.IntPtr p, int n) {
    if (n == 0) return "";
    byte[] b = new byte[n];
    System.Runtime.InteropServices.Marshal.Copy(p, b, 0, n);
    return System.Text.Encoding.UTF8.GetString(b);
  }

  static string Key(System.IntPtr v, ulong i) {
    System.IntPtr p = swig_rv_key(v, (System.UIntPtr)i, out System.UIntPtr n);
    return Utf8String(p, (int)(ulong)n);
  }

  /// <summary>Dense column-major values of a numeric value, as double</summary>
  static double[] DenseF64(System.IntPtr v, out long rows, out long cols) {
    rows = swig_rv_rows(v);
    cols = swig_rv_cols(v);
    long numel = rows * cols;
    double[] r = new double[numel];
    bool f64 = swig_rv_elem(v) == F64;
    if (swig_rv_is_sparse(v) != 0) {
      long nnz = (long)(ulong)swig_rv_nnz(v);
      long[] colind = new long[cols + 1], row = new long[nnz];
      System.Runtime.InteropServices.Marshal.Copy(swig_rv_colind(v), colind, 0, (int)(cols + 1));
      if (nnz > 0) System.Runtime.InteropServices.Marshal.Copy(swig_rv_row(v), row, 0, (int)nnz);
      double[] nz = Nonzeros(v, nnz, f64);
      for (long c = 0; c < cols; ++c) for (long k = colind[c]; k < colind[c + 1]; ++k) r[row[k] + c * rows] = nz[k];
    } else {
      r = Nonzeros(v, numel, f64);
    }
    return r;
  }

  static double[] Nonzeros(System.IntPtr v, long n, bool f64) {
    double[] r = new double[n];
    if (n == 0) return r;
    if (f64) {
      System.Runtime.InteropServices.Marshal.Copy(swig_rv_f64_data(v), r, 0, (int)n);
    } else {
      long[] t = new long[n];
      System.Runtime.InteropServices.Marshal.Copy(swig_rv_i64_data(v), t, 0, (int)n);
      for (long k = 0; k < n; ++k) r[k] = t[k];
    }
    return r;
  }

  static bool IsScalar(System.IntPtr v) {
    return swig_rv_kind(v) == NUM && swig_rv_rows(v) == 1 && swig_rv_cols(v) == 1 && swig_rv_is_sparse(v) == 0;
  }

  /// <summary>The proxy of a pointer value, taking over its ownership; null if not a pointer</summary>
  static SwigObject AdoptPtr(System.IntPtr v, System.Type t) {
    if (swig_rv_kind(v) != PTR) return null;
    string name = System.Runtime.InteropServices.Marshal.PtrToStringUTF8(swig_rv_type_name(v));
    bool own = swig_rv_own(v) != 0;
    if (factories.TryGetValue(name, out var make)) {
      SwigObject o = make(new Owned(swig_rv_ptr(v), own));
      if (t.IsInstanceOfType(o)) {
        swig_rv_disown(v);
        return o;
      }
      o.swigOwn = false;
      System.GC.SuppressFinalize(o);
    }
    // The pointer as the requested class (an upcast SWIG knows)
    if (t == typeof(SwigObject) || t.IsAbstract && !HasOwnedCtor(t)) return null;
    var ctor = ctorCache.GetOrAdd(t, OwnedCtor);
    SwigObject probe = ctor(Owned.Pending);
    System.GC.SuppressFinalize(probe);
    System.IntPtr p = swig_rv_cast_ptr(v, probe.SwigTypeInfo);
    if (p == System.IntPtr.Zero) return null;
    SwigObject r = ctor(new Owned(p, own && p == swig_rv_ptr(v)));
    if (r.swigOwn) swig_rv_disown(v);
    return r;
  }

  static bool HasOwnedCtor(System.Type t) {
    return t.GetConstructor(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic,
                            null, new[] { typeof(Owned) }, null) != null;
  }

  static System.Func<Owned, SwigObject> OwnedCtor(System.Type t) {
    var c = t.GetConstructor(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic,
                             null, new[] { typeof(Owned) }, null);
    if (c == null) throw new System.InvalidOperationException(t.Name + " has no proxy constructor");
    return o => (SwigObject)c.Invoke(new object[] { o });
  }

  static System.Exception Mismatch(System.IntPtr v, System.Type t) {
    return new System.InvalidCastException("cannot convert " + Describe(v) + " to " + t.Name);
  }

  static string Describe(System.IntPtr v) {
    switch (swig_rv_kind(v)) {
      case NIL: return "null";
      case NUM: return "number(" + swig_rv_rows(v) + "x" + swig_rv_cols(v) + ")";
      case STR: return "string";
      case LIST: return "list";
      case DICT: return "dict";
      case PTR: return System.Runtime.InteropServices.Marshal.PtrToStringUTF8(swig_rv_type_name(v));
    }
    return "function";
  }

  /// <summary>Converts a guest value to a C# value of type t; pointers are taken over</summary>
  public static object FromRV(System.IntPtr v, System.Type t) {
    if (t == typeof(void)) return null;
    int kind = swig_rv_kind(v);
    if (t == typeof(object)) return Generic(v);
    if (kind == NIL && !t.IsValueType) return null;
    if (t == typeof(double)) {
      if (!IsScalar(v) || swig_rv_elem(v) == BOOL) throw Mismatch(v, t);
      return swig_rv_elem(v) == F64 ? Nonzeros(v, 1, true)[0] : Nonzeros(v, 1, false)[0];
    }
    if (t == typeof(long) || t == typeof(int)) {
      if (!IsScalar(v) || swig_rv_elem(v) == F64) throw Mismatch(v, t);
      long[] x = new long[1];
      System.Runtime.InteropServices.Marshal.Copy(swig_rv_i64_data(v), x, 0, 1);
      return t == typeof(int) ? (object)(int)x[0] : x[0];
    }
    if (t == typeof(bool)) {
      if (!IsScalar(v)) throw Mismatch(v, t);
      return Nonzeros(v, 1, swig_rv_elem(v) == F64)[0] != 0;
    }
    if (t == typeof(string)) {
      if (kind != STR) throw Mismatch(v, t);
      return Str(v);
    }
    if (typeof(SwigObject).IsAssignableFrom(t)) {
      SwigObject o = AdoptPtr(v, t);
      if (o == null) throw Mismatch(v, t);
      return o;
    }
    if (t == typeof(double[])) {
      if (kind == NUM) return DenseF64(v, out _, out _);
      if (kind == LIST) return System.Array.ConvertAll(ListItems(v, typeof(double)), x => (double)x);
      throw Mismatch(v, t);
    }
    if (t == typeof(double[,])) {
      if (kind != NUM) throw Mismatch(v, t);
      double[] d = DenseF64(v, out long nr, out long nc);
      double[,] m = new double[nr, nc];
      for (long j = 0; j < nc; ++j) for (long i = 0; i < nr; ++i) m[i, j] = d[i + j * nr];
      return m;
    }
    if (t.IsGenericType) {
      System.Type g = t.GetGenericTypeDefinition();
      System.Type[] a = t.GetGenericArguments();
      if (g == typeof(System.Collections.Generic.List<>) || g == typeof(System.Collections.Generic.IList<>)
          || g == typeof(System.Collections.Generic.IReadOnlyList<>) || g == typeof(System.Collections.Generic.IEnumerable<>)) {
        var list = (System.Collections.IList)System.Activator.CreateInstance(typeof(System.Collections.Generic.List<>).MakeGenericType(a[0]));
        if (kind == NUM) {
          foreach (double x in DenseF64(v, out _, out _)) list.Add(NumAs(x, a[0], v));
        } else if (kind == LIST) {
          foreach (object x in ListItems(v, a[0])) list.Add(x);
        } else {
          throw Mismatch(v, t);
        }
        return list;
      }
      if (g == typeof(System.Collections.Generic.Dictionary<,>) || g == typeof(System.Collections.Generic.IDictionary<,>)
          || g == typeof(System.Collections.Generic.IReadOnlyDictionary<,>)) {
        if (kind != DICT) throw Mismatch(v, t);
        var dict = (System.Collections.IDictionary)System.Activator.CreateInstance(typeof(System.Collections.Generic.Dictionary<,>).MakeGenericType(a));
        ulong n = (ulong)swig_rv_len(v);
        for (ulong i = 0; i < n; ++i) dict[Key(v, i)] = FromRV(swig_rv_item(v, (System.UIntPtr)i), a[1]);
        return dict;
      }
      if (g == typeof(System.ValueTuple<,>)) {
        if (kind == LIST && (ulong)swig_rv_len(v) == 2) {
          object x = FromRV(swig_rv_item(v, (System.UIntPtr)0), a[0]), y = FromRV(swig_rv_item(v, (System.UIntPtr)1), a[1]);
          return System.Activator.CreateInstance(t, x, y);
        }
        if (kind == NUM && swig_rv_rows(v) * swig_rv_cols(v) == 2) {
          double[] d = DenseF64(v, out _, out _);
          return System.Activator.CreateInstance(t, NumAs(d[0], a[0], v), NumAs(d[1], a[1], v));
        }
        throw Mismatch(v, t);
      }
    }
    throw Mismatch(v, t);
  }

  static object NumAs(double x, System.Type t, System.IntPtr v) {
    if (t == typeof(double) || t == typeof(object)) return t == typeof(object) && swig_rv_elem(v) != F64 ? (swig_rv_elem(v) == BOOL ? (object)(x != 0) : (long)x) : x;
    if (t == typeof(long)) return (long)x;
    if (t == typeof(int)) return (int)x;
    if (t == typeof(bool)) return x != 0;
    throw Mismatch(v, t);
  }

  static object[] ListItems(System.IntPtr v, System.Type t) {
    ulong n = (ulong)swig_rv_len(v);
    object[] r = new object[n];
    for (ulong i = 0; i < n; ++i) r[i] = FromRV(swig_rv_item(v, (System.UIntPtr)i), t);
    return r;
  }

  /// <summary>Natural C# value of a guest value</summary>
  static object Generic(System.IntPtr v) {
    switch (swig_rv_kind(v)) {
      case NIL: return null;
      case NUM: {
        int elem = swig_rv_elem(v);
        double[] d = DenseF64(v, out long nr, out long nc);
        bool seq = swig_rv_is_seq(v) != 0;
        if (!seq && nr == 1 && nc == 1 && swig_rv_is_sparse(v) == 0) return NumAs(d[0], typeof(object), v);
        if (nc == 1) {
          if (elem == F64) return d;
          if (elem == I64) return System.Array.ConvertAll(d, x => (long)x);
          return System.Array.ConvertAll(d, x => x != 0);
        }
        double[,] m = new double[nr, nc];
        for (long j = 0; j < nc; ++j) for (long i = 0; i < nr; ++i) m[i, j] = d[i + j * nr];
        return m;
      }
      case STR: return Str(v);
      case LIST: return new System.Collections.Generic.List<object>(ListItems(v, typeof(object)));
      case DICT: {
        var d = new System.Collections.Generic.Dictionary<string, object>();
        ulong n = (ulong)swig_rv_len(v);
        for (ulong i = 0; i < n; ++i) d[Key(v, i)] = Generic(swig_rv_item(v, (System.UIntPtr)i));
        return d;
      }
      case PTR: return AdoptPtr(v, typeof(SwigObject));
    }
    return null;
  }

  // ---- directors: C++ virtual methods call C# overrides ----

  // Kept alive for the whole process: C++ holds pointers to them
  static readonly InvokeFn directorInvoke = DirectorInvoke;
  static readonly DropFn directorDrop = DirectorDrop;
  [System.ThreadStatic] static System.Collections.Generic.List<object> directorKeep;
  static readonly System.Collections.Concurrent.ConcurrentDictionary<(System.Type, string, int), System.Reflection.MethodInfo> overrides =
    new System.Collections.Concurrent.ConcurrentDictionary<(System.Type, string, int), System.Reflection.MethodInfo>();

  // A weak handle: as in Python, the C# object must be kept alive while C++ uses it
  static System.IntPtr NewDirector(SwigObject self, System.Type declaring) {
    var box = new DirectorBox(self, declaring);
    System.IntPtr ctx = System.Runtime.InteropServices.GCHandle.ToIntPtr(System.Runtime.InteropServices.GCHandle.Alloc(box));
    return swig_rv_new_fun(ctx, directorInvoke, directorDrop);
  }

  sealed class DirectorBox {
    readonly System.WeakReference<SwigObject> self;
    internal readonly System.Type Declaring;
    internal DirectorBox(SwigObject s, System.Type d) { self = new System.WeakReference<SwigObject>(s); Declaring = d; }
    internal SwigObject Self => self.TryGetTarget(out var s) ? s : null;
  }

  static void DirectorDrop(System.IntPtr ctx) { System.Runtime.InteropServices.GCHandle.FromIntPtr(ctx).Free(); }

  static System.IntPtr NewErr(string msg) {
    byte[] b = Utf8(msg);
    return swig_rv_new_err(b, (System.UIntPtr)b.Length);
  }

  /// <summary>The override of a director method by runtime type, or null to run the C++ implementation</summary>
  static System.Reflection.MethodInfo Override(System.Type rt, System.Type declaring, string name, int argc) {
    return overrides.GetOrAdd((rt, name, argc), k => {
      foreach (var m in rt.GetMethods(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic)) {
        if (m.Name != name || m.GetParameters().Length != argc || !m.IsVirtual) continue;
        if (m.GetBaseDefinition().DeclaringType != declaring) continue;
        return m.DeclaringType == declaring ? null : m;
      }
      return null;
    });
  }

  static System.IntPtr DirectorInvoke(System.IntPtr ctx, int argc, System.IntPtr argvp) {
    if (argc == 0) {
      if (directorKeep != null && directorKeep.Count > 0) directorKeep.RemoveAt(directorKeep.Count - 1);
      return System.IntPtr.Zero;
    }
    if (directorKeep == null) directorKeep = new System.Collections.Generic.List<object>();
    System.IntPtr[] argv = new System.IntPtr[argc];
    System.Runtime.InteropServices.Marshal.Copy(argvp, argv, 0, argc);
    string method = Str(argv[0]);
    try {
      var box = (DirectorBox)System.Runtime.InteropServices.GCHandle.FromIntPtr(ctx).Target;
      SwigObject self = box.Self;
      if (self == null) {
        directorKeep.Add(null);
        return NewErr("the C# object implementing " + box.Declaring.Name + "." + method + " was garbage collected; keep a reference to it");
      }
      var m = Override(self.GetType(), box.Declaring, method, argc - 1);
      if (m == null) {
        directorKeep.Add(null);
        return swig_rv_new_base();
      }
      var ps = m.GetParameters();
      object[] args = new object[argc - 1];
      for (int i = 0; i < args.Length; ++i) args[i] = FromRVBorrowed(argv[i + 1], ps[i].ParameterType);
      object r = m.Invoke(self, args);
      directorKeep.Add(r);
      return ToRV(r);
    } catch (System.Exception e) {
      if (e is System.Reflection.TargetInvocationException && e.InnerException != null) e = e.InnerException;
      directorKeep.Add(null);
      // Nothing may escape into C++: also not from an exception's own Message
      string msg;
      try {
        msg = e.Message;
      } catch {
        msg = "(message unavailable)";
      }
      return NewErr("C# " + method + "() threw " + e.GetType().Name + ": " + msg);
    }
  }

  /// <summary>Director argument: owned copies are taken over, borrowed pointers stay borrowed</summary>
  static object FromRVBorrowed(System.IntPtr v, System.Type t) {
    return FromRV(v, t);
  }
}
