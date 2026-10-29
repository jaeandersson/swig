/* -----------------------------------------------------------------------------
 * director.swg
 *
 * Director classes for Rust: virtual methods call a Rust closure (a SWIG_RV_FUN
 * value) with the method name and the converted arguments. The closure returns
 * the result, SWIG_RV_BASE to run the C++ implementation, or SWIG_RV_ERR.
 * ----------------------------------------------------------------------------- */

#include <string>
#include <exception>

#define SWIG_DIRECTOR_CAST(ARG) dynamic_cast<Swig::Director *>(ARG)
#define SWIG_DIRECTOR_RGTR(ARG1, ARG2)

namespace Swig {
  class DirectorException : public std::exception {
  protected:
    std::string swig_msg;
  public:
    DirectorException(const std::string &msg) : swig_msg(msg) {}
    virtual ~DirectorException() throw() {}
    const char *what() const throw() { return swig_msg.c_str(); }
    static void raise(const char *msg) { throw DirectorException(msg); }
  };

  class DirectorMethodException : public DirectorException {
  public:
    DirectorMethodException(const std::string &msg) : DirectorException(msg) {}
    static void raise(const char *msg) { throw DirectorMethodException(msg); }
  };

  class DirectorTypeMismatchException : public DirectorException {
  public:
    DirectorTypeMismatchException(const std::string &msg) : DirectorException(msg) {}
    static void raise(const char *msg) { throw DirectorTypeMismatchException(msg); }
    static void raise(const char *type, const char *msg) { throw DirectorTypeMismatchException(std::string(type) + ": " + msg); }
  };

  class DirectorPureVirtualException : public DirectorException {
  public:
    DirectorPureVirtualException(const std::string &msg) : DirectorException(msg) {}
    static void raise(const char *msg) { throw DirectorPureVirtualException(msg); }
  };

  class Director {
    SwigRV *swig_self;
  public:
    /* Takes over the closure of self; the argument itself stays with the caller */
    Director(SwigRV *self) : swig_self(new SwigRV()) {
      swig_self->kind = SWIG_RV_FUN;
      swig_self->ctx = self->ctx;
      swig_self->invoke = self->invoke;
      swig_self->drop = self->drop;
      self->drop = 0;
    }
    virtual ~Director() { delete swig_self; }
    SwigRV *swig_get_self() const { return swig_self; }
    void swig_disown() {}
  };

  /* Lets the closure release what it keeps alive for the result of an upcall */
  struct UpcallGuard {
    SwigRV *self;
    UpcallGuard(SwigRV *s) : self(s) {}
    ~UpcallGuard() { self->invoke(self->ctx, 0, 0); }
  };

  /* Calls method on the closure; the arguments are consumed */
  inline SwigRV *upcall(SwigRV *self, const char *method, int argc, SwigRV **argv) {
    SwigRV **a = new SwigRV*[argc + 1];
    a[0] = SWIG_Rust_NewStr(method, strlen(method));
    for (int i = 0; i < argc; ++i) a[i + 1] = argv[i];
    SwigRV *r = self->invoke(self->ctx, argc + 1, a);
    for (int i = 0; i <= argc; ++i) delete a[i];
    delete[] a;
    if (!r) r = new SwigRV();
    return r;
  }
}

#define SWIG_DirOutFail(code, msg) Swig::DirectorTypeMismatchException::raise(SWIG_ErrorType(code), msg)
