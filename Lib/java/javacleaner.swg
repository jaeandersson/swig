/* -----------------------------------------------------------------------------
 * javacleaner.swg
 *
 * Proxy lifetime through java.lang.ref.Cleaner (Java 9+) instead of the
 * deprecated finalize(): an owning proxy registers a cleaning action that
 * deletes the C++ object once the proxy is unreachable ($jnidestructor).  The action captures
 * only a cell holding the pointer, so it does not keep the proxy alive; giving
 * up ownership empties the cell.  delete() and close() (AutoCloseable) run the
 * action at most once.  Cleaning actions are skipped once the JVM shuts down.
 * The cleaner thread only queues deletes; they run on the next application thread
 * that creates a proxy, so destructors never run concurrently with native calls.
 *
 * Usage: %include <javacleaner.swg> before the wrapped classes.
 * ----------------------------------------------------------------------------- */

%pragma(java) jniclasscode=%{
  private static volatile Thread swigCleanerThread;
  private final static java.lang.ref.Cleaner SWIG_CLEANER = java.lang.ref.Cleaner.create(r -> {
    Thread t = new Thread(r, "SWIG-Cleaner");
    t.setDaemon(true);
    swigCleanerThread = t;
    return t;
  });
  /* Deletes of unreachable proxies, run on the next application thread that creates a proxy */
  private final static java.util.concurrent.ConcurrentLinkedQueue<Runnable> SWIG_PENDING = new java.util.concurrent.ConcurrentLinkedQueue<>();
  private static volatile boolean swigShutdown = false;
  static {
    Runtime.getRuntime().addShutdownHook(new Thread(() -> swigShutdown = true));
  }
  static void swigDrain() {
    for (Runnable r; (r = SWIG_PENDING.poll()) != null;) r.run();
  }
  /* Registers a cleaning action for obj; cell[0] holds the pointer to delete, 0 for none */
  static java.lang.ref.Cleaner.Cleanable swigRegister(Object obj, long[] cell, java.util.function.LongConsumer delete) {
    swigDrain();
    return SWIG_CLEANER.register(obj, () -> {
      long p = cell[0];
      cell[0] = 0;
      if (p == 0 || delete == null || swigShutdown) return;
      if (Thread.currentThread() == swigCleanerThread) {
        SWIG_PENDING.add(() -> { if (!swigShutdown) delete.accept(p); });
      } else {
        delete.accept(p);
      }
    });
  }
%}

%typemap(javafinalize) SWIGTYPE ""
%typemap(javainterfaces) SWIGTYPE "AutoCloseable"

%define SWIG_JAVACLEANER_BODY(PTRCTOR_VISIBILITY, CPTR_VISIBILITY, TYPE...)
%typemap(javabody) TYPE %{
  private transient long swigCPtr;
  protected transient boolean swigCMemOwn;
  private transient long[] swigCell;
  private transient java.lang.ref.Cleaner.Cleanable swigCleanable;

  @SuppressWarnings("this-escape")
  PTRCTOR_VISIBILITY $javaclassname(long cPtr, boolean cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = cPtr;
    if (cMemoryOwn)
      swigArm();
  }

  private void swigArm() {
    if (swigCleanable == null) {
      swigCell = new long[] { swigCPtr };
      swigCleanable = $imclassname.swigRegister(this, swigCell, $jnidestructor);
    } else {
      swigCell[0] = swigCPtr;
    }
  }

  private void swigDisarm() {
    if (swigCell != null)
      swigCell[0] = 0;
  }

  CPTR_VISIBILITY static long getCPtr($javaclassname obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }

  CPTR_VISIBILITY static long swigRelease($javaclassname obj) {
    long ptr = 0;
    if (obj != null) {
      if (!obj.swigCMemOwn)
        throw new RuntimeException("Cannot release ownership as memory is not owned");
      ptr = obj.swigCPtr;
      obj.swigCMemOwn = false;
      obj.delete();
    }
    return ptr;
  }

  @Override
  public void close() {
    delete();
  }
%}

%typemap(javabody_derived) TYPE %{
  private transient long swigCPtr;
  private transient long[] swigCell;
  private transient java.lang.ref.Cleaner.Cleanable swigCleanable;

  @SuppressWarnings("this-escape")
  PTRCTOR_VISIBILITY $javaclassname(long cPtr, boolean cMemoryOwn) {
    super($imclassname.$javaclazznameSWIGUpcast(cPtr), false);
    swigCPtr = cPtr;
    swigCMemOwn = cMemoryOwn;
    if (cMemoryOwn)
      swigArm();
  }

  private void swigArm() {
    if (swigCleanable == null) {
      swigCell = new long[] { swigCPtr };
      swigCleanable = $imclassname.swigRegister(this, swigCell, $jnidestructor);
    } else {
      swigCell[0] = swigCPtr;
    }
  }

  private void swigDisarm() {
    if (swigCell != null)
      swigCell[0] = 0;
  }

  CPTR_VISIBILITY static long getCPtr($javaclassname obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }

  CPTR_VISIBILITY static long swigRelease($javaclassname obj) {
    long ptr = 0;
    if (obj != null) {
      if (!obj.swigCMemOwn)
        throw new RuntimeException("Cannot release ownership as memory is not owned");
      ptr = obj.swigCPtr;
      obj.swigCMemOwn = false;
      obj.delete();
    }
    return ptr;
  }
%}
%enddef

SWIG_JAVACLEANER_BODY(protected, protected, SWIGTYPE)

%typemap(javadestruct, methodname="delete", methodmodifiers="public synchronized", parameters="") SWIGTYPE {
    if (swigCleanable != null) {
      if (!swigCMemOwn)
        swigDisarm();
      swigCleanable.clean();
      swigCleanable = null;
    }
    swigCMemOwn = false;
    swigCPtr = 0;
  }

%typemap(javadestruct_derived, methodname="delete", methodmodifiers="public synchronized", parameters="") SWIGTYPE {
    if (swigCleanable != null) {
      if (!swigCMemOwn)
        swigDisarm();
      swigCleanable.clean();
      swigCleanable = null;
    }
    swigCPtr = 0;
    super.delete();
  }

%typemap(directorowner_release, methodname="swigReleaseOwnership") SWIGTYPE %{
  public void $methodname() {
    swigCMemOwn = false;
    swigDisarm();
    $jnicall;
  }
%}

%typemap(directorowner_take, methodname="swigTakeOwnership") SWIGTYPE %{
  public void $methodname() {
    swigCMemOwn = true;
    swigArm();
    $jnicall;
  }
%}
