/* -------------------------------------------------------------------------
 *  Special user directives
 * ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */

/* shadow code */
#define %shadow      %insert("shadow")
#define %pythoncode  %insert("python")
#define %pythonbegin %insert("pythonbegin")
#define %stubcode    %insert("stubs")


/* -------------------------------------------------------------------------
 *  PEP-484 stub (.pyi) emission primitives.
 *
 *  Only active under `swig -stubs`, which defines SWIG_STUBS_ENABLED.
 *  Under any other build (stock SWIG, -stubs off) every macro below
 *  expands to nothing, so call sites in interface files require no
 *  conditional wrapping.
 *
 *  Usage is designed to read like Python signatures.  The trailing
 *  variadic `ARGS...` absorbs the rest of the arguments verbatim,
 *  preserving internal commas -- so tuple[int, int] etc. work inline.
 *  For the non-variadic RET / TYPE parameter, wrap any top-level
 *  comma in %arg(...)  (see swigmacros.swg).
 *
 *    %stub_method(__add__, Self, other: int | float | Self)
 *    %stub_method0(__len__, int)
 *    %stub_overload_method(__call__, %arg(Mapping[str, int]), **kw: int)
 *    %stub_overload_func(sin, float, x: float)
 *    %stub_attr(shape, tuple[int, int])
 *    %stub_class_begin(Foo)   # use for pure-%pythoncode classes
 * ------------------------------------------------------------------------- */

#ifdef SWIG_STUBS_ENABLED

/* Declare an input-position stub type alias.  Two effects:
 *
 *   %stub_alias_in(DM, bool | int | float | DM | Sequence[float] | ...)
 *
 *   - defines a SWIG macro token `DM_IN` that expands to the same
 *     union, for use inside hand-written %stub_* invocations;
 *   - registers the alias with the stub emitter, widening every
 *     auto-generated `x: DM` input-position annotation the same way.
 *
 * Aliases may reference each other's tokens:
 *   %stub_alias_in(SX, SX | DM_IN)
 *
 * The variadic arg absorbs internal commas — tuple/Mapping/Sequence
 * types need no %arg() wrapping inside a %stub_alias_in call.
 */
/* %stub_alias_in(NAME, TYPES...)
 *
 * Emits a PEP-484 `_NAME: TypeAlias = TYPES` in the stub preamble and
 * registers `NAME → _NAME` so pystub tokens get substituted at emit
 * time.  Reference aliases in hand-written stubs by the emitted name:
 *
 *   SWIG source:   %stub_method(..., arg: _DM)
 *   Emitted .pyi:  def ..(self, arg: _DM) -> ...
 *   Preamble:      _DM = bool | int | float | DM | ...
 *
 * Aliases may compose freely — write `_DM` inside another alias body
 * and it will resolve through the same preamble:
 *
 *   %stub_alias_in(SX, SX | _SupportsSX | _DM)
 */
%define %stub_alias_in(NAME, TYPES...)
%insert("stubs_preamble") %{_##NAME: TypeAlias = TYPES
%}
%insert("stubs_alias_in_table") %{NAME=_##NAME
%}
%enddef

/* Output-position counterpart.  Pairs with %stub_alias_in(NAME, ...) —
 * reuses the same `_NAME` preamble TypeAlias, doesn't emit again. */
%define %stub_alias_out(NAME, TYPES...)
%insert("stubs_alias_out_table") %{NAME=_##NAME
%}
%enddef

/* Variants for alias keys that can't be SWIG identifier names —
 * typemap doc strings like `[float]` or `dict:X`.  No `_NAME`
 * TypeAlias is emitted; the widening body is inlined at each use. */
%define %stub_alias_in_key(KEY, TYPES...)
%insert("stubs_alias_in_table") %{KEY=TYPES
%}
%enddef

%define %stub_alias_out_key(KEY, TYPES...)
%insert("stubs_alias_out_table") %{KEY=TYPES
%}
%enddef

%define %stub_method(NAME, RET, ARGS...)
%stubcode %{    def NAME(self, ARGS) -> RET: ...
%}
%enddef

%define %stub_method0(NAME, RET)
%stubcode %{    def NAME(self) -> RET: ...
%}
%enddef

%define %stub_overload_method(NAME, RET, ARGS...)
%stubcode %{    @overload
    def NAME(self, ARGS) -> RET: ...
%}
%enddef

%define %stub_overload_method0(NAME, RET)
%stubcode %{    @overload
    def NAME(self) -> RET: ...
%}
%enddef

/* Self-narrowed overload: `SELFTYPE` is the annotation for `self`
 * (typically a `Generic` specialisation like `"NZproxy[MX]"`).
 * Pyright only selects this overload when the inferred self-type
 * matches SELFTYPE, letting one generic class carry type-specific
 * behaviour without widening the base overloads. */
%define %stub_overload_method_selftyped(NAME, RET, SELFTYPE, ARGS...)
%stubcode %{    @overload
    def NAME(self: SELFTYPE, ARGS) -> RET: ...
%}
%enddef

%define %stub_func(NAME, RET, ARGS...)
%stubcode %{def NAME(ARGS) -> RET: ...
%}
%enddef

%define %stub_overload_func(NAME, RET, ARGS...)
%stubcode %{@overload
def NAME(ARGS) -> RET: ...
%}
%enddef

%define %stub_attr(NAME, TYPE...)
%stubcode %{    NAME: TYPE
%}
%enddef

/* Module-level constant / variable annotation, `NAME: TYPE = ...` at
 * column 0 (not class body).  The `= ...` ellipsis is the stub-file
 * placeholder and avoids pyright treating a bare module-level
 * annotation as a Final declaration (which blocks the common
 * `from X import *; from numpy import *` overlay pattern on names
 * like `inf`, `pi`). */
%define %stub_const(NAME, TYPE...)
%stubcode %{NAME: TYPE = ...
%}
%enddef

%define %stub_class_begin(NAME)
%stubcode %{class NAME:
%}
%enddef

#else

%define %stub_alias_in(NAME, TYPES...) %enddef
%define %stub_alias_out(NAME, TYPES...) %enddef
%define %stub_alias_in_key(KEY, TYPES...) %enddef
%define %stub_alias_out_key(KEY, TYPES...) %enddef
%define %stub_method(NAME, RET, ARGS...) %enddef
%define %stub_method0(NAME, RET) %enddef
%define %stub_overload_method(NAME, RET, ARGS...) %enddef
%define %stub_overload_method0(NAME, RET) %enddef
%define %stub_overload_method_selftyped(NAME, RET, SELFTYPE, ARGS...) %enddef
%define %stub_func(NAME, RET, ARGS...) %enddef
%define %stub_overload_func(NAME, RET, ARGS...) %enddef
%define %stub_attr(NAME, TYPE...) %enddef
%define %stub_const(NAME, TYPE...) %enddef
%define %stub_class_begin(NAME) %enddef

#endif


/* ------------------------------------------------------------------------- */
/* 
Use the "nondynamic" feature to make a wrapped class behave as a "nondynamic"
one, ie, a python class that doesn't dynamically add new attributes.

For example, for the class

%pythonnondynamic A;
struct A
{
  int a;
  int b;
};

you will get:

 aa = A()
 aa.a = 1  # Ok
 aa.b = 1  # Ok
 aa.c = 3  # error

Since nondynamic is a feature, if you use it like

 %pythonnondynamic;

it will make all the wrapped classes nondynamic ones.

The implementation is based on this recipe:

   http://aspn.activestate.com/ASPN/Cookbook/Python/Recipe/252158

*/

#define %pythonnondynamic      %feature("python:nondynamic", "1")
#define %nopythonnondynamic    %feature("python:nondynamic", "0")
#define %clearpythonnondynamic %feature("python:nondynamic", "")
#define %pythondynamic         %nopythonnondynamic


/* ------------------------------------------------------------------------- */
/*

Use %pythonmaybecall to flag a method like __add__ or __radd__.  These
don't produce an error when called, they just return NotImplemented.

These methods "may be called" if needed.

*/

#define %pythonmaybecall      %feature("python:maybecall", "1") 
#define %nopythonmaybecall    %feature("python:maybecall", "0")
#define %clearpythonmaybecall %feature("python:maybecall", "")

/* ------------------------------------------------------------------------- */
/*
  The %pythoncallback feature produce a more natural callback wrapper
  than the %callback mechanism, ie, it uses the original name for
  the callback and callable objects. 

  Just use it as

    %pythoncallback(1) foo;
    int foo(int a);

    %pythoncallback(1) A::foo;
    struct A {
     static int foo(int a);
    };

    int bar(int, int (*pf)(int));

  then, you can use it as:

   a = foo(1)
   b = bar(2, foo)

   c = A.foo(3)
   d = bar(4, A.foo)


   If you use it with a member method
   %pythoncallback(1) A::foom;
   struct A {
      int foom(int a);
   };

   then you can use it as

     r = a.foom(3)             # eval the method
     mptr = A.foom_cb_ptr      # returns the callback pointer

   where the '_cb_ptr' suffix is added for the callback pointer.

*/

#define %pythoncallback      %feature("python:callback")
#define %nopythoncallback    %feature("python:callback","0")
#define %clearpythoncallback %feature("python:callback","")

/* ------------------------------------------------------------------------- */
/*
  Support for the old %callback directive name
*/
#ifdef %callback
#undef %callback
#endif

#ifdef %nocallback
#undef %nocallback
#endif

#ifdef %clearcallback
#undef %clearcallback
#endif

#define %callback(x)     %feature("python:callback",`x`)
#define %nocallback      %nopythoncallback
#define %clearcallback   %clearpythoncallback

/* ------------------------------------------------------------------------- */
/*
  Thread support - Advance control
  
*/

#define %nothread           %feature("nothread")
#define %thread             %feature("nothread","0")
#define %clearnothread      %feature("nothread","")

#define %nothreadblock      %feature("nothreadblock")
#define %threadblock        %feature("nothreadblock","0")
#define %clearnothreadblock %feature("nothreadblock","")

#define %nothreadallow      %feature("nothreadallow")
#define %threadallow        %feature("nothreadallow","0")
#define %clearnothreadallow %feature("nothreadallow","")


/* ------------------------------------------------------------------------- */
/*
  Implicit Conversion using the C++ constructor mechanism
*/

#define %implicitconv      %feature("implicitconv") 
#define %noimplicitconv    %feature("implicitconv", "0")
#define %clearimplicitconv %feature("implicitconv", "")


/* ------------------------------------------------------------------------- */
/*
  Enable keywords parameters
*/

#define %kwargs      %feature("kwargs") 
#define %nokwargs    %feature("kwargs", "0")
#define %clearkwargs %feature("kwargs", "")

/* ------------------------------------------------------------------------- */
/*
  Add python code to the proxy/shadow code 
  
   %pythonprepend   - Add code before the C++ function is called
   %pythonappend    - Add code after the C++ function is called
*/

#define %pythonprepend       %feature("pythonprepend") 
#define %clearpythonprepend  %feature("pythonprepend","")

#define %pythonappend         %feature("pythonappend") 
#define %clearpythonappend    %feature("pythonappend","")


/* ------------------------------------------------------------------------- */
/* 
   %extend_smart_pointer extend the smart pointer support.

   For example, if you have a smart pointer as:
	    
     template <class Type> class RCPtr {
     public:
       ...
       RCPtr(Type *p);
   	Type * operator->() const;
   	...
     };
     
   you use the %extend_smart_pointer directive as:
   
     %extend_smart_pointer(RCPtr<A>);
     %template(RCPtr_A)  RCPtr<A>;
   
   then, if you have something like:

     RCPtr<A> make_ptr();
     int foo(A *);

   you can do the following:

     a = make_ptr();
     b = foo(a);

   ie, swig will accept a RCPtr<A> object where a 'A *' is
   expected.

   Also, when using vectors
   
     %extend_smart_pointer(RCPtr<A>);
     %template(RCPtr_A) RCPtr<A>;
     %template(vector_A) std::vector<RCPtr<A> >;
   	
   you can type

     a = A();
     v = vector_A(2)
     v[0] = a

   ie, an 'A *' object is accepted, via implicit conversion, 
   where a RCPtr<A> object is expected. Additionally

     x = v[0]

   returns (and sets 'x' as) a copy of v[0], making reference
   counting possible and consistent.
*/

%define %extend_smart_pointer(Type...)
%implicitconv Type;
%apply const SWIGTYPE& SMARTPOINTER { const Type& };
%apply SWIGTYPE SMARTPOINTER { Type };
%enddef
