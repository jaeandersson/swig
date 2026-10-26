/* -----------------------------------------------------------------------------
 * javaguest.swg
 *
 * Guest-object mode for -java: typemaps may receive and return Java objects
 * (jobject) and convert them in C++, as the dynamic targets do with PyObject*.
 * Provides the SWIG type table with the Java proxy class as clientdata,
 * SWIG_ConvertPtr / SWIG_NewPointerObj on proxies, SWIG_AsVal / SWIG_From on
 * boxed values, JNI helpers for String, List and Map, and a catch-all in every
 * wrapper that turns C++ exceptions into Java exceptions.
 *
 * Usage: %include <javaguest.swg> before any declaration.
 * ----------------------------------------------------------------------------- */

%pragma(java) guestobjects="1"

/* Unchecked conversion of guest values for javaout / javadirectorin typemaps */
%pragma(java) jniclasscode=%{
  @SuppressWarnings("unchecked")
  static <T> T swigCast(Object obj) {
    return (T) obj;
  }
%}

%include <typemaps/swigmacros.swg>
%include <typemaps/fragments.swg>

%insert(runtime) "swigrun.swg";
%insert(runtime) "swigerrors.swg";

%insert(runtime) %{
#include <string>
#include <stdexcept>

/* Java class for C++ exceptions (slash-separated binary name) */
#ifndef SWIG_JAVA_GUEST_EXCEPTION
#define SWIG_JAVA_GUEST_EXCEPTION "java/lang/RuntimeException"
#endif

/* single-module process: no cross-module type sharing */
#define SWIG_GetModule(clientdata) 0
#define SWIG_SetModule(clientdata, pointer) (void)(pointer)

namespace Swig {
  /* Conversion failure carrying a SWIG error code */
  class JavaGuestError : public std::runtime_error {
  public:
    int code;
    JavaGuestError(int code, const std::string& msg) : std::runtime_error(msg), code(code) {}
  };
  /* A Java exception is pending and must reach the caller unchanged */
  class JavaGuestPending {};
}

SWIGRUNTIME JavaVM *&SWIG_JavaGuestVM() {
  static JavaVM *vm = 0;
  return vm;
}

SWIGRUNTIME void SWIG_JavaGuestSetEnv(JNIEnv *jenv) {
  if (!SWIG_JavaGuestVM()) jenv->GetJavaVM(&SWIG_JavaGuestVM());
}

/* JNIEnv of the calling thread, 0 if the thread is not attached */
SWIGRUNTIME JNIEnv *SWIG_JavaGuestEnv() {
  JNIEnv *jenv = 0;
  JavaVM *vm = SWIG_JavaGuestVM();
  if (!vm || vm->GetEnv(reinterpret_cast<void **>(&jenv), JNI_VERSION_1_6) != JNI_OK) return 0;
  return jenv;
}

SWIGRUNTIME JNIEnv *SWIG_JavaGuestEnvChecked() {
  JNIEnv *jenv = SWIG_JavaGuestEnv();
  if (!jenv) throw Swig::JavaGuestError(SWIG_RuntimeError, "thread not attached to the JVM");
  return jenv;
}

/* Propagate a pending Java exception */
SWIGRUNTIME void SWIG_JavaGuestCheck(JNIEnv *jenv) {
  if (jenv->ExceptionCheck()) throw Swig::JavaGuestPending();
}

SWIGRUNTIME void SWIG_JavaGuestThrow(JNIEnv *jenv, int code, const char *msg) {
  if (jenv->ExceptionCheck()) return;
  const char *name = SWIG_JAVA_GUEST_EXCEPTION;
  if (code == SWIG_TypeError || code == SWIG_ValueError || code == SWIG_NullReferenceError)
    name = "java/lang/IllegalArgumentException";
  else if (code == SWIG_IndexError)
    name = "java/lang/IndexOutOfBoundsException";
  else if (code == SWIG_MemoryError)
    name = "java/lang/OutOfMemoryError";
  jclass cls = jenv->FindClass(name);
  if (cls) jenv->ThrowNew(cls, msg);
}

#define SWIG_JAVA_GUEST_CATCH(nullreturn) \
  catch (Swig::JavaGuestPending&) { return nullreturn; } \
  catch (Swig::JavaGuestError& e) { SWIG_JavaGuestThrow(jenv, e.code, e.what()); return nullreturn; } \
  catch (std::exception& e) { SWIG_JavaGuestThrow(jenv, SWIG_RuntimeError, e.what()); return nullreturn; } \
  catch (...) { SWIG_JavaGuestThrow(jenv, SWIG_UnknownError, "unknown C++ exception"); return nullreturn; }

#define SWIG_exception_fail(code, msg) do { throw Swig::JavaGuestError(code, msg); } while (0)
#ifndef SWIG_exception
#define SWIG_exception(code, msg) do { throw Swig::JavaGuestError(code, msg); } while (0)
#endif
#define SWIG_fail throw Swig::JavaGuestError(SWIG_RuntimeError, "conversion failed")
#define SWIG_Error(code, msg) throw Swig::JavaGuestError(code, msg)

/* Java proxy class of a swig_type_info (its clientdata) */
typedef struct {
  const char *name;
  jclass cls;
  jmethodID ctor;
  jfieldID cptr;
} swig_java_guest_class;

SWIGRUNTIME void SWIG_JavaGuestRegister(JNIEnv *jenv, swig_type_info *ti, swig_java_guest_class *c) {
  if (!ti) return;
  jclass cls = jenv->FindClass(c->name);
  if (!cls) { jenv->ExceptionClear(); return; }
  c->cls = static_cast<jclass>(jenv->NewGlobalRef(cls));
  jenv->DeleteLocalRef(cls);
  c->cptr = jenv->GetFieldID(c->cls, "swigCPtr", "J");
  if (!c->cptr) jenv->ExceptionClear();
  c->ctor = jenv->GetMethodID(c->cls, "<init>", "(JZ)V");
  if (!c->ctor) jenv->ExceptionClear();
  SWIG_TypeClientData(ti, c);
}

/* Proxy -> C++ pointer; each proxy class stores a pointer of its own type */
SWIGRUNTIME int SWIG_JavaGuestConvertPtr(jobject obj, void **ptr, swig_type_info *ty, int flags) {
  if (!obj) {
    if (flags & SWIG_POINTER_NO_NULL) return SWIG_NullReferenceError;
    if (ptr) *ptr = 0;
    return SWIG_OK;
  }
  JNIEnv *jenv = SWIG_JavaGuestEnv();
  if (!jenv || !ty) return SWIG_ERROR;
  swig_java_guest_class *c = static_cast<swig_java_guest_class *>(ty->clientdata);
  if (!c || !c->cls || !c->cptr) return SWIG_ERROR;
  if (!jenv->IsInstanceOf(obj, c->cls)) return SWIG_ERROR;
  jlong p = jenv->GetLongField(obj, c->cptr);
  if (!p) return SWIG_NullReferenceError;
  if (ptr) *ptr = reinterpret_cast<void *>(p);
  return SWIG_OK;
}
#define SWIG_ConvertPtr(obj, pptr, type, flags) SWIG_JavaGuestConvertPtr(obj, pptr, type, flags)

/* C++ pointer -> new proxy (owning with SWIG_POINTER_OWN); null is a Java value, so failures throw */
SWIGRUNTIME jobject SWIG_JavaGuestNewPointerObj(void *ptr, swig_type_info *ty, int flags) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  swig_java_guest_class *c = ty ? static_cast<swig_java_guest_class *>(ty->clientdata) : 0;
  if (!c || !c->cls || !c->ctor)
    throw Swig::JavaGuestError(SWIG_TypeError, std::string("no Java proxy class for ") + (ty ? SWIG_TypePrettyName(ty) : "unknown type"));
  jobject r = jenv->NewObject(c->cls, c->ctor, reinterpret_cast<jlong>(ptr), (jboolean)((flags & SWIG_POINTER_OWN) != 0));
  SWIG_JavaGuestCheck(jenv);
  return r;
}
#define SWIG_NewPointerObj(ptr, type, flags) SWIG_JavaGuestNewPointerObj(ptr, type, flags)

/* Cached JDK classes and methods */
typedef struct {
  jclass Boolean, Number, Integer, Long, Short, Byte, Double, Float, String, List, Map, ArrayList, HashMap;
  jclass doubles, longs, ints, booleans, objects, Entry, SimpleEntry;
  jmethodID booleanValue, longValue, doubleValue, Boolean_valueOf, Long_valueOf, Double_valueOf;
  jmethodID List_size, List_get, ArrayList_init, ArrayList_add;
  jmethodID Map_keySet, Map_get, HashMap_init, Map_put, Set_toArray;
  jmethodID String_getBytes, String_init;
  jmethodID Entry_getKey, Entry_getValue, SimpleEntry_init;
  jstring utf8;
} swig_java_guest_jdk;

SWIGRUNTIME jclass SWIG_JavaGuestClass(JNIEnv *jenv, const char *name) {
  jclass c = jenv->FindClass(name);
  if (!c) throw Swig::JavaGuestPending();
  jclass g = static_cast<jclass>(jenv->NewGlobalRef(c));
  jenv->DeleteLocalRef(c);
  return g;
}

SWIGRUNTIME swig_java_guest_jdk SWIG_JavaGuestJDKInit(JNIEnv *jenv) {
  swig_java_guest_jdk j;
  {
    j.Boolean = SWIG_JavaGuestClass(jenv, "java/lang/Boolean");
    j.Number = SWIG_JavaGuestClass(jenv, "java/lang/Number");
    j.Integer = SWIG_JavaGuestClass(jenv, "java/lang/Integer");
    j.Long = SWIG_JavaGuestClass(jenv, "java/lang/Long");
    j.Short = SWIG_JavaGuestClass(jenv, "java/lang/Short");
    j.Byte = SWIG_JavaGuestClass(jenv, "java/lang/Byte");
    j.Double = SWIG_JavaGuestClass(jenv, "java/lang/Double");
    j.Float = SWIG_JavaGuestClass(jenv, "java/lang/Float");
    j.String = SWIG_JavaGuestClass(jenv, "java/lang/String");
    j.List = SWIG_JavaGuestClass(jenv, "java/util/List");
    j.Map = SWIG_JavaGuestClass(jenv, "java/util/Map");
    j.ArrayList = SWIG_JavaGuestClass(jenv, "java/util/ArrayList");
    j.HashMap = SWIG_JavaGuestClass(jenv, "java/util/LinkedHashMap");
    j.doubles = SWIG_JavaGuestClass(jenv, "[D");
    j.longs = SWIG_JavaGuestClass(jenv, "[J");
    j.ints = SWIG_JavaGuestClass(jenv, "[I");
    j.booleans = SWIG_JavaGuestClass(jenv, "[Z");
    j.objects = SWIG_JavaGuestClass(jenv, "[Ljava/lang/Object;");
    j.Entry = SWIG_JavaGuestClass(jenv, "java/util/Map$Entry");
    j.SimpleEntry = SWIG_JavaGuestClass(jenv, "java/util/AbstractMap$SimpleImmutableEntry");
    jclass set = SWIG_JavaGuestClass(jenv, "java/util/Set");
    j.booleanValue = jenv->GetMethodID(j.Boolean, "booleanValue", "()Z");
    j.longValue = jenv->GetMethodID(j.Number, "longValue", "()J");
    j.doubleValue = jenv->GetMethodID(j.Number, "doubleValue", "()D");
    j.Boolean_valueOf = jenv->GetStaticMethodID(j.Boolean, "valueOf", "(Z)Ljava/lang/Boolean;");
    j.Long_valueOf = jenv->GetStaticMethodID(j.Long, "valueOf", "(J)Ljava/lang/Long;");
    j.Double_valueOf = jenv->GetStaticMethodID(j.Double, "valueOf", "(D)Ljava/lang/Double;");
    j.List_size = jenv->GetMethodID(j.List, "size", "()I");
    j.List_get = jenv->GetMethodID(j.List, "get", "(I)Ljava/lang/Object;");
    j.ArrayList_init = jenv->GetMethodID(j.ArrayList, "<init>", "(I)V");
    j.ArrayList_add = jenv->GetMethodID(j.ArrayList, "add", "(Ljava/lang/Object;)Z");
    j.Map_keySet = jenv->GetMethodID(j.Map, "keySet", "()Ljava/util/Set;");
    j.Map_get = jenv->GetMethodID(j.Map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    j.HashMap_init = jenv->GetMethodID(j.HashMap, "<init>", "()V");
    j.Map_put = jenv->GetMethodID(j.Map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    j.Set_toArray = jenv->GetMethodID(set, "toArray", "()[Ljava/lang/Object;");
    j.String_getBytes = jenv->GetMethodID(j.String, "getBytes", "(Ljava/lang/String;)[B");
    j.String_init = jenv->GetMethodID(j.String, "<init>", "([BLjava/lang/String;)V");
    j.Entry_getKey = jenv->GetMethodID(j.Entry, "getKey", "()Ljava/lang/Object;");
    j.Entry_getValue = jenv->GetMethodID(j.Entry, "getValue", "()Ljava/lang/Object;");
    j.SimpleEntry_init = jenv->GetMethodID(j.SimpleEntry, "<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V");
    jstring u = jenv->NewStringUTF("UTF-8");
    j.utf8 = static_cast<jstring>(jenv->NewGlobalRef(u));
    jenv->DeleteLocalRef(u);
    SWIG_JavaGuestCheck(jenv);
  }
  return j;
}

/* Thread-safe one-time lookup */
SWIGRUNTIME const swig_java_guest_jdk &SWIG_JavaGuestJDK(JNIEnv *jenv) {
  static const swig_java_guest_jdk j = SWIG_JavaGuestJDKInit(jenv);
  return j;
}

SWIGRUNTIME bool SWIG_JavaGuestIsInstance(jobject obj, jclass cls) {
  return obj && SWIG_JavaGuestEnvChecked()->IsInstanceOf(obj, cls);
}

/* java.lang.String <-> UTF-8 std::string */
SWIGRUNTIME bool SWIG_JavaGuestIsString(jobject obj) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  return obj && jenv->IsInstanceOf(obj, SWIG_JavaGuestJDK(jenv).String);
}
SWIGRUNTIME std::string SWIG_JavaGuestString(jobject obj) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jbyteArray b = static_cast<jbyteArray>(jenv->CallObjectMethod(obj, SWIG_JavaGuestJDK(jenv).String_getBytes, SWIG_JavaGuestJDK(jenv).utf8));
  SWIG_JavaGuestCheck(jenv);
  jsize n = jenv->GetArrayLength(b);
  std::string r(n, '\0');
  if (n) jenv->GetByteArrayRegion(b, 0, n, reinterpret_cast<jbyte *>(&r[0]));
  jenv->DeleteLocalRef(b);
  return r;
}
SWIGRUNTIME jobject SWIG_JavaGuestNewString(const std::string &s) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jbyteArray b = jenv->NewByteArray(static_cast<jsize>(s.size()));
  if (!s.empty()) jenv->SetByteArrayRegion(b, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte *>(s.data()));
  jobject r = jenv->NewObject(SWIG_JavaGuestJDK(jenv).String, SWIG_JavaGuestJDK(jenv).String_init, b, SWIG_JavaGuestJDK(jenv).utf8);
  jenv->DeleteLocalRef(b);
  SWIG_JavaGuestCheck(jenv);
  return r;
}

/* java.util.List */
SWIGRUNTIME bool SWIG_JavaGuestIsList(jobject obj) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  return obj && jenv->IsInstanceOf(obj, SWIG_JavaGuestJDK(jenv).List);
}
SWIGRUNTIME jint SWIG_JavaGuestListSize(jobject list) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jint n = jenv->CallIntMethod(list, SWIG_JavaGuestJDK(jenv).List_size);
  SWIG_JavaGuestCheck(jenv);
  return n;
}
/* Local reference: release with SWIG_JavaGuestRelease */
SWIGRUNTIME jobject SWIG_JavaGuestListGet(jobject list, jint i) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jobject r = jenv->CallObjectMethod(list, SWIG_JavaGuestJDK(jenv).List_get, i);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
SWIGRUNTIME jobject SWIG_JavaGuestListNew(jint capacity) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jobject r = jenv->NewObject(SWIG_JavaGuestJDK(jenv).ArrayList, SWIG_JavaGuestJDK(jenv).ArrayList_init, capacity);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
/* Appends and releases item */
SWIGRUNTIME void SWIG_JavaGuestListAppend(jobject list, jobject item) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jenv->CallBooleanMethod(list, SWIG_JavaGuestJDK(jenv).ArrayList_add, item);
  if (item) jenv->DeleteLocalRef(item);
  SWIG_JavaGuestCheck(jenv);
}

/* java.util.Map (LinkedHashMap: keeps C++ order) */
SWIGRUNTIME bool SWIG_JavaGuestIsMap(jobject obj) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  return obj && jenv->IsInstanceOf(obj, SWIG_JavaGuestJDK(jenv).Map);
}
/* Keys as a local Object[] */
SWIGRUNTIME jobjectArray SWIG_JavaGuestMapKeys(jobject map) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jobject ks = jenv->CallObjectMethod(map, SWIG_JavaGuestJDK(jenv).Map_keySet);
  SWIG_JavaGuestCheck(jenv);
  jobjectArray r = static_cast<jobjectArray>(jenv->CallObjectMethod(ks, SWIG_JavaGuestJDK(jenv).Set_toArray));
  jenv->DeleteLocalRef(ks);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
SWIGRUNTIME jobject SWIG_JavaGuestMapGet(jobject map, jobject key) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jobject r = jenv->CallObjectMethod(map, SWIG_JavaGuestJDK(jenv).Map_get, key);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
SWIGRUNTIME jobject SWIG_JavaGuestMapNew() {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jobject r = jenv->NewObject(SWIG_JavaGuestJDK(jenv).HashMap, SWIG_JavaGuestJDK(jenv).HashMap_init);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
/* Puts and releases key and value */
SWIGRUNTIME void SWIG_JavaGuestMapPut(jobject map, jobject key, jobject value) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jobject old = jenv->CallObjectMethod(map, SWIG_JavaGuestJDK(jenv).Map_put, key, value);
  if (old) jenv->DeleteLocalRef(old);
  if (key) jenv->DeleteLocalRef(key);
  if (value) jenv->DeleteLocalRef(value);
  SWIG_JavaGuestCheck(jenv);
}

/* %append_output target: the only output, or appended to the List created by the wrapper */
SWIGRUNTIME jobject SWIG_JavaGuestAppendOutput(jobject result, jobject obj) {
  if (!result) return obj;
  SWIG_JavaGuestListAppend(result, obj);
  return result;
}

/* Primitive arrays <-> std::vector: double[], long[] or int[], boolean[] */
#include <vector>
SWIGRUNTIME bool SWIG_JavaGuestIsDoubles(jobject obj) { return SWIG_JavaGuestIsInstance(obj, SWIG_JavaGuestJDK(SWIG_JavaGuestEnvChecked()).doubles); }
SWIGRUNTIME bool SWIG_JavaGuestIsLongs(jobject obj) {
  const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(SWIG_JavaGuestEnvChecked());
  return SWIG_JavaGuestIsInstance(obj, j.longs) || SWIG_JavaGuestIsInstance(obj, j.ints);
}
SWIGRUNTIME bool SWIG_JavaGuestIsBooleans(jobject obj) { return SWIG_JavaGuestIsInstance(obj, SWIG_JavaGuestJDK(SWIG_JavaGuestEnvChecked()).booleans); }
SWIGRUNTIME std::vector<double> SWIG_JavaGuestDoubles(jobject obj) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jdoubleArray a = static_cast<jdoubleArray>(obj);
  std::vector<double> r(jenv->GetArrayLength(a));
  if (!r.empty()) jenv->GetDoubleArrayRegion(a, 0, (jsize)r.size(), &r[0]);
  return r;
}
SWIGRUNTIME std::vector<long long> SWIG_JavaGuestLongs(jobject obj) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  if (SWIG_JavaGuestIsInstance(obj, SWIG_JavaGuestJDK(jenv).ints)) {
    jintArray a = static_cast<jintArray>(obj);
    std::vector<jint> t(jenv->GetArrayLength(a));
    if (!t.empty()) jenv->GetIntArrayRegion(a, 0, (jsize)t.size(), &t[0]);
    return std::vector<long long>(t.begin(), t.end());
  }
  jlongArray a = static_cast<jlongArray>(obj);
  std::vector<jlong> t(jenv->GetArrayLength(a));
  if (!t.empty()) jenv->GetLongArrayRegion(a, 0, (jsize)t.size(), &t[0]);
  return std::vector<long long>(t.begin(), t.end());
}
SWIGRUNTIME std::vector<bool> SWIG_JavaGuestBooleans(jobject obj) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jbooleanArray a = static_cast<jbooleanArray>(obj);
  std::vector<jboolean> t(jenv->GetArrayLength(a));
  if (!t.empty()) jenv->GetBooleanArrayRegion(a, 0, (jsize)t.size(), &t[0]);
  return std::vector<bool>(t.begin(), t.end());
}
SWIGRUNTIME jobject SWIG_JavaGuestNewDoubles(const std::vector<double> &v) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jdoubleArray r = jenv->NewDoubleArray((jsize)v.size());
  if (!v.empty()) jenv->SetDoubleArrayRegion(r, 0, (jsize)v.size(), &v[0]);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
SWIGRUNTIME jobject SWIG_JavaGuestNewLongs(const std::vector<long long> &v) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  std::vector<jlong> t(v.begin(), v.end());
  jlongArray r = jenv->NewLongArray((jsize)t.size());
  if (!t.empty()) jenv->SetLongArrayRegion(r, 0, (jsize)t.size(), &t[0]);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
SWIGRUNTIME jobject SWIG_JavaGuestNewBooleans(const std::vector<bool> &v) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  std::vector<jboolean> t(v.begin(), v.end());
  jbooleanArray r = jenv->NewBooleanArray((jsize)t.size());
  if (!t.empty()) jenv->SetBooleanArrayRegion(r, 0, (jsize)t.size(), &t[0]);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
/* Array of n elements of class element (e.g. double[][] from the double[] class) */
SWIGRUNTIME jobject SWIG_JavaGuestNewArrayOf(jclass element, jsize n) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jobjectArray r = jenv->NewObjectArray(n, element, 0);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
/* Sets and releases element i */
SWIGRUNTIME void SWIG_JavaGuestArraySet(jobject array, jsize i, jobject value) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  jenv->SetObjectArrayElement(static_cast<jobjectArray>(array), i, value);
  if (value) jenv->DeleteLocalRef(value);
  SWIG_JavaGuestCheck(jenv);
}

/* Sequences: Object[] (including arrays of arrays) or java.util.List */
SWIGRUNTIME bool SWIG_JavaGuestIsSequence(jobject obj) {
  const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(SWIG_JavaGuestEnvChecked());
  return SWIG_JavaGuestIsInstance(obj, j.objects) || SWIG_JavaGuestIsInstance(obj, j.List);
}
SWIGRUNTIME jint SWIG_JavaGuestSequenceSize(jobject obj) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  if (jenv->IsInstanceOf(obj, SWIG_JavaGuestJDK(jenv).objects)) return jenv->GetArrayLength(static_cast<jobjectArray>(obj));
  return SWIG_JavaGuestListSize(obj);
}
/* Local reference */
SWIGRUNTIME jobject SWIG_JavaGuestSequenceGet(jobject obj, jint i) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  if (jenv->IsInstanceOf(obj, SWIG_JavaGuestJDK(jenv).objects)) {
    jobject r = jenv->GetObjectArrayElement(static_cast<jobjectArray>(obj), i);
    SWIG_JavaGuestCheck(jenv);
    return r;
  }
  return SWIG_JavaGuestListGet(obj, i);
}

/* java.util.Map.Entry, e.g. for pairs */
SWIGRUNTIME bool SWIG_JavaGuestIsEntry(jobject obj) { return SWIG_JavaGuestIsInstance(obj, SWIG_JavaGuestJDK(SWIG_JavaGuestEnvChecked()).Entry); }
/* Local reference to the key or the value */
SWIGRUNTIME jobject SWIG_JavaGuestEntryGet(jobject obj, bool key) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(jenv);
  jobject r = jenv->CallObjectMethod(obj, key ? j.Entry_getKey : j.Entry_getValue);
  SWIG_JavaGuestCheck(jenv);
  return r;
}
/* Releases key and value */
SWIGRUNTIME jobject SWIG_JavaGuestNewEntry(jobject key, jobject value) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(jenv);
  jobject r = jenv->NewObject(j.SimpleEntry, j.SimpleEntry_init, key, value);
  if (key) jenv->DeleteLocalRef(key);
  if (value) jenv->DeleteLocalRef(value);
  SWIG_JavaGuestCheck(jenv);
  return r;
}

SWIGRUNTIME void SWIG_JavaGuestRelease(jobject obj) {
  if (obj) SWIG_JavaGuestEnvChecked()->DeleteLocalRef(obj);
}
%}

/* Primitive SWIG_AsVal / SWIG_From on boxed values: only Boolean is a bool,
   integral targets take integral boxes only, floating targets any Number */
%fragment("SWIG_JavaGuestNumber", "header") {
SWIGINTERN int SWIG_JavaGuestAsLong(jobject obj, long long *v) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(jenv);
  if (!obj || !(jenv->IsInstanceOf(obj, j.Long) || jenv->IsInstanceOf(obj, j.Integer) ||
                jenv->IsInstanceOf(obj, j.Short) || jenv->IsInstanceOf(obj, j.Byte))) return SWIG_TypeError;
  if (v) *v = jenv->CallLongMethod(obj, j.longValue);
  SWIG_JavaGuestCheck(jenv);
  return SWIG_OK;
}
SWIGINTERN int SWIG_JavaGuestAsDouble(jobject obj, double *v) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(jenv);
  if (!obj || !jenv->IsInstanceOf(obj, j.Number)) return SWIG_TypeError;
  if (v) *v = jenv->CallDoubleMethod(obj, j.doubleValue);
  SWIG_JavaGuestCheck(jenv);
  return SWIG_OK;
}
}

%fragment(SWIG_AsVal_frag(bool), "header", fragment="SWIG_JavaGuestNumber") {
SWIGINTERN int SWIG_AsVal_dec(bool)(jobject obj, bool *val) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(jenv);
  if (!obj || !jenv->IsInstanceOf(obj, j.Boolean)) return SWIG_TypeError;
  if (val) *val = jenv->CallBooleanMethod(obj, j.booleanValue) != 0;
  SWIG_JavaGuestCheck(jenv);
  return SWIG_OK;
}
}

%define %java_guest_asval_integral(T)
%fragment(SWIG_AsVal_frag(T), "header", fragment="SWIG_JavaGuestNumber") {
SWIGINTERN int SWIG_AsVal_dec(T)(jobject obj, T *val) {
  long long v;
  int res = SWIG_JavaGuestAsLong(obj, &v);
  if (!SWIG_IsOK(res)) return res;
  if (val) *val = static_cast< T >(v);
  return SWIG_OK;
}
}
%enddef

%define %java_guest_asval_floating(T)
%fragment(SWIG_AsVal_frag(T), "header", fragment="SWIG_JavaGuestNumber") {
SWIGINTERN int SWIG_AsVal_dec(T)(jobject obj, T *val) {
  double v;
  int res = SWIG_JavaGuestAsDouble(obj, &v);
  if (!SWIG_IsOK(res)) return res;
  if (val) *val = static_cast< T >(v);
  return SWIG_OK;
}
}
%enddef

%define %java_guest_from(T, CALL)
%fragment(SWIG_From_frag(T), "header") {
SWIGINTERNINLINE jobject SWIG_From_dec(T)(T value) {
  JNIEnv *jenv = SWIG_JavaGuestEnvChecked();
  const swig_java_guest_jdk &j = SWIG_JavaGuestJDK(jenv);
  jobject r = CALL;
  SWIG_JavaGuestCheck(jenv);
  return r;
}
}
%enddef

%java_guest_asval_integral(int)
%java_guest_asval_integral(long)
%java_guest_asval_integral(long long)
%java_guest_asval_integral(unsigned int)
%java_guest_asval_integral(unsigned long)
%java_guest_asval_integral(unsigned long long)
%java_guest_asval_floating(double)
%java_guest_asval_floating(float)

%java_guest_from(bool, jenv->CallStaticObjectMethod(j.Boolean, j.Boolean_valueOf, (jboolean)value))
%java_guest_from(int, jenv->CallStaticObjectMethod(j.Long, j.Long_valueOf, (jlong)value))
%java_guest_from(long, jenv->CallStaticObjectMethod(j.Long, j.Long_valueOf, (jlong)value))
%java_guest_from(long long, jenv->CallStaticObjectMethod(j.Long, j.Long_valueOf, (jlong)value))
%java_guest_from(unsigned int, jenv->CallStaticObjectMethod(j.Long, j.Long_valueOf, (jlong)value))
%java_guest_from(unsigned long, jenv->CallStaticObjectMethod(j.Long, j.Long_valueOf, (jlong)value))
%java_guest_from(unsigned long long, jenv->CallStaticObjectMethod(j.Long, j.Long_valueOf, (jlong)value))
%java_guest_from(double, jenv->CallStaticObjectMethod(j.Double, j.Double_valueOf, (jdouble)value))
%java_guest_from(float, jenv->CallStaticObjectMethod(j.Double, j.Double_valueOf, (jdouble)value))

#define %dirout_fail(code, msg) SWIG_exception_fail(code, msg)
#define %append_output(obj) $result = SWIG_JavaGuestAppendOutput($result, obj)

/* Module init: type table and proxy registry, then %init code; closed by the generator */
%insert(init) "swiginit.swg";
%insert(init) %{
SWIGINTERN void SWIG_JavaGuestInit(JNIEnv *jenv) {
  SWIG_JavaGuestSetEnv(jenv);
  SWIG_InitializeModule(0);
  SWIG_JAVA_GUEST_REGISTER_TYPES;
%}
