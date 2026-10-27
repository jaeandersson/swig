import java_guest_object.*;
import java.util.List;

public class java_guest_object_runme {
  static {
    try {
      System.loadLibrary("java_guest_object");
    } catch (UnsatisfiedLinkError e) {
      System.err.println("Native code library failed to load. See the chapter on Dynamic Linking Problems in the SWIG Java documentation for help.\n" + e);
      System.exit(1);
    }
  }
  static void check(boolean ok, String what) {
    if (!ok) throw new RuntimeException("FAIL " + what);
  }
  @SuppressWarnings({"unchecked", "rawtypes"})
  static void noMatch() { java_guest_object.kind((List) List.of("x")); }
  public static void main(String[] argv) {
    check(java_guest_object.init_ran() == 1, "%init ran inside swig_module_init");
    Vec v = java_guest_object.twice(new Vec(1.5));
    check(v.get(0) == 3.0, "proxy in, NewPointerObj out: " + v.get(0));
    check(java_guest_object.total(List.of(new Vec(1), new Vec(2.5))) == 3.5, "List<Vec> in");
    check(java_guest_object.tag_of(new Derived()) == 42, "Derived proxy as const Base& (base offset)");
    check(java_guest_object.greet().equals("héllo 😀"), "UTF-8 string incl. non-BMP");
    try { java_guest_object.boom(); check(false, "boom"); }
    catch (RuntimeException e) { check(e.getMessage().equals("boom from C++"), "C++ exception -> RuntimeException"); }

    // widening
    check(java_guest_object.twice(2.0).get(0) == 4.0, "twice(double) via guestwiden");
    check(java_guest_object.join(new Vec(1), new Vec(2)).equals("vec"), "join(Vec,Vec)");
    check(java_guest_object.join(new Pt(1, 2), new Pt(3, 4)).equals("pt"), "join(Pt,Pt)");
    check(java_guest_object.join(1.0, 2.0).equals("vec"), "join(double,double): lowest precedence family wins");
    check(java_guest_object.join(new Pt(1, 2), new Vec(3)).equals("pt"), "join(Pt,Vec): widened Pt family");

    // merged erasure collisions
    check(java_guest_object.kind(List.of(new Vec(1))).equals("vec"), "kind(List<Vec>) dispatched in C++");
    check(java_guest_object.kind(List.of(new Pt(1, 2))).equals("pt"), "kind(List<Pt>) dispatched in C++");
    Vec f = java_guest_object.first(List.of(new Vec(5)));
    Pt g = java_guest_object.first(List.of(new Pt(6, 7)));
    check(f.get(0) == 5 && g.getX() == 6, "<T extends Shape> T first(List<T>) typed returns");
    check(new Bag(List.of(new Pt(1, 2))).getWhat().equals("pt") && new Bag(List.of(new Vec(1))).getWhat().equals("vec"), "merged constructor");
    check(new Bag(List.of(new Vec(1))).count(List.of(new Pt(1, 2), new Pt(3, 4))).equals("pt2"), "merged member method");
    java.util.List<Object> mixed = new java.util.ArrayList<>(List.of(new Pt(1, 2)));
    check(java_guest_object.kind(mixed).equals("pt"), "parameter-only type variable is a wildcard");
    check(new Bag(List.of(new Vec(1))).which(new Vec(1)).equals("nonconst"), "non-const twin wins");
    try { noMatch(); check(false, "no match"); }
    catch (IllegalArgumentException e) { check(e.getMessage().startsWith("No matching overload of 'kind'"), "no match -> IllegalArgumentException: " + e.getMessage().replace('\n', '|')); }
    // output arguments
    check(java_guest_object.halve(new Vec(8)).get(0) == 4, "void + 1 output -> Vec");
    List<Vec> sp = java_guest_object.split(new Vec(5));
    check(sp.size() == 2 && sp.get(0).get(0) == 4 && sp.get(1).get(0) == 6, "void + 2 outputs -> List<Vec>");
    List<Vec> ng = java_guest_object.negate(new Vec(3));
    check(ng.size() == 2 && ng.get(0).get(0) == -3 && ng.get(1).get(0) == 4, "result + output -> List<Vec>");

    // directors with guest types, from the main thread and C++ threads
    class Doubler extends Visitor {
      @Override public Vec visit(List<Vec> xs) {
        byte[] junk = new byte[1 << 14];
        return new Vec(2 * xs.get(0).get(0) + junk.length * 0);
      }
    }
    Doubler dv = new Doubler();
    check(java_guest_object.run_visitor(dv, List.of(new Vec(21))).get(0) == 42, "director: List<Vec> in, Vec out");
    Thread gc = new Thread(() -> { for (int i = 0; i < 50; ++i) System.gc(); });
    gc.start();
    double tot = java_guest_object.run_threaded(dv, 4, 500);
    try { gc.join(); } catch (InterruptedException e) {}
    check(tot == 2 * 500 * (1 + 2 + 3 + 4), "director upcalls from 4 C++ threads + concurrent GC: " + tot);
    Visitor thrower = new Visitor() {
      @Override public Vec visit(List<Vec> xs) { throw new IllegalStateException("visit failed"); }
    };
    try { java_guest_object.run_visitor(thrower, List.of(new Vec(1))); check(false, "director exception"); }
    catch (RuntimeException e) { check(String.valueOf(e.getMessage()).contains("visit failed"), "Java exception in upcall reaches caller: " + e); }
    check(java_guest_object.run_visitor(new Visitor(), List.of()).get(0) == -1, "unoverridden director method uses C++ base");

    // Cleaner: many short-lived owning proxies, close() idempotent
    for (int i = 0; i < 300000; ++i) java_guest_object.twice(i);
    System.gc();
    try (Vec c = new Vec(3)) { check(c.get(0) == 3, "try-with-resources"); }
    Vec c2 = new Vec(4); c2.close(); c2.close();
    check(true, "double close is a no-op");
    try { c2.size(); check(false, "call on a closed proxy throws"); }
    catch (IllegalStateException e) { check(e.getMessage().contains("closed"), "call on a closed proxy throws: " + e); }
  }
}
