/* -----------------------------------------------------------------------------
 * matlabcontainer.swg
 *
 * MATLAB cell <-> C++ container wrapper
 *
 * This wrapper, and its iterator, allows a general use (and reuse) of
 * the mapping between C++ and MATLAB, thanks to the C++ templates.
 *
 * Of course, it needs the C++ compiler to support templates, but
 * since we will use this wrapper with the STL containers, that should
 * be the case.
 * ----------------------------------------------------------------------------- */

%{
#include <iostream>
%}


#if !defined(SWIG_NO_EXPORT_ITERATOR_METHODS)
# if !defined(SWIG_EXPORT_ITERATOR_METHODS)
#  define SWIG_EXPORT_ITERATOR_METHODS SWIG_EXPORT_ITERATOR_METHODS
# endif
#endif

%include <matlabiterators.swg>

// The Matlab C++ Wrap

%fragment("<stdexcept>");

%include <std_except.i>

%fragment(SWIG_Traits_frag(mxArray),"header",fragment="StdTraits") {
namespace swig {
  template <>  struct traits<mxArray* > {
    typedef value_category category;
    static const char* type_name() { return  "mxArray"; }
  };

  template <>  struct traits_from<mxArray*> {
    typedef mxArray* value_type;
    static mxArray* from(const value_type& val) {
      return val;
    }
  };

  template <>
  struct traits_check<mxArray*, value_category> {
    static bool check(mxArray*) {
      return true;
    }
  };

  template <>  struct traits_asval<mxArray* > {
    typedef mxArray* value_type;
    static int asval(mxArray* obj, value_type *val) {
      if (val) *val = obj;
      return SWIG_OK;
    }
  };
}
}

%fragment("MatlabSequence_Base","header",fragment="<stddef.h>")
{
%#include <functional>

namespace std {
#if 0
KT do we need this?
  template <>
  struct less <mxArray*>: public binary_function<mxArray*, mxArray*, bool>
  {
    bool
    operator()(mxArray* v, mxArray* w) const
    {
      mxArray* res = do_binary_op(mxArray*::op_le,v,w);
      return res.is_true();
    }
  };
#endif
}

namespace swig {
  inline size_t
  check_index(ptrdiff_t i, size_t size, bool insert = false) {
    if ( i < 0 ) {
      if ((size_t) (-i) <= size)
	return (size_t) (i + size);
    } else if ( (size_t) i < size ) {
      return (size_t) i;
    } else if (insert && ((size_t) i == size)) {
      return size;
    }

    throw std::out_of_range("index out of range");
  }

  inline size_t
  slice_index(ptrdiff_t i, size_t size) {
    if ( i < 0 ) {
      if ((size_t) (-i) <= size) {
	return (size_t) (i + size);
      } else {
	throw std::out_of_range("index out of range");
      }
    } else {
      return ( (size_t) i < size ) ? ((size_t) i) : size;
    }
  }

  template <class Sequence, class Difference>
  inline typename Sequence::iterator
  getpos(Sequence* self, Difference i)  {
    typename Sequence::iterator pos = self->begin();
    std::advance(pos, check_index(i,self->size()));
    return pos;
  }

  template <class Sequence, class Difference>
  inline typename Sequence::const_iterator
  cgetpos(const Sequence* self, Difference i)  {
    typename Sequence::const_iterator pos = self->begin();
    std::advance(pos, check_index(i,self->size()));
    return pos;
  }

  template <class Sequence, class Difference>
  inline Sequence*
  getslice(const Sequence* self, Difference i, Difference j) {
    typename Sequence::size_type size = self->size();
    typename Sequence::size_type ii = swig::check_index(i, size);
    typename Sequence::size_type jj = swig::slice_index(j, size);

    if (jj > ii) {
      typename Sequence::const_iterator vb = self->begin();
      typename Sequence::const_iterator ve = self->begin();
      std::advance(vb,ii);
      std::advance(ve,jj);
      return new Sequence(vb, ve);
    } else {
      return new Sequence();
    }
  }

  template <class Sequence, class Difference, class InputSeq>
  inline void
  setslice(Sequence* self, Difference i, Difference j, const InputSeq& v) {
    typename Sequence::size_type size = self->size();
    typename Sequence::size_type ii = swig::check_index(i, size, true);
    typename Sequence::size_type jj = swig::slice_index(j, size);
    if (jj < ii) jj = ii;
    size_t ssize = jj - ii;
    if (ssize <= v.size()) {
      typename Sequence::iterator sb = self->begin();
      typename InputSeq::const_iterator vmid = v.begin();
      std::advance(sb,ii);
      std::advance(vmid, jj - ii);
      self->insert(std::copy(v.begin(), vmid, sb), vmid, v.end());
    } else {
      typename Sequence::iterator sb = self->begin();
      typename Sequence::iterator se = self->begin();
      std::advance(sb,ii);
      std::advance(se,jj);
      self->erase(sb,se);
      sb = self->begin();
      std::advance(sb,ii);
      self->insert(sb, v.begin(), v.end());
    }
  }

  template <class Sequence, class Difference>
  inline void
  delslice(Sequence* self, Difference i, Difference j) {
    typename Sequence::size_type size = self->size();
    typename Sequence::size_type ii = swig::check_index(i, size, true);
    typename Sequence::size_type jj = swig::slice_index(j, size);
    if (jj > ii) {
      typename Sequence::iterator sb = self->begin();
      typename Sequence::iterator se = self->begin();
      std::advance(sb,ii);
      std::advance(se,jj);
      self->erase(sb,se);
    }
  }
}
}

%fragment("MatlabSequence_Cont","header",
	  fragment="StdTraits",
	  fragment="MatlabSequence_Base",
	  fragment="MatlabSwigIterator_T")
{
namespace swig
{
  template <class T>
    struct MatlabSequence_Ref // * matlab can't support these, because of how assignment works
  {
    MatlabSequence_Ref(mxArray* const seq, int index)
      : _seq(seq), _index(index)
    {
    }

    operator T () const
    {
      return swig::as<T>(mxGetCell(_seq, _index), true);
    }

    MatlabSequence_Ref& operator=(const T&)
    {
      throw std::invalid_argument("cannot assign to a MATLAB cell element");
    }

  private:
    mxArray* _seq;
    int _index;
  };

  template <class T>
  struct MatlabSequence_ArrowProxy
  {
    MatlabSequence_ArrowProxy(const T& x): m_value(x) {}
    const T* operator->() const { return &m_value; }
    operator const T*() const { return &m_value; }
    T m_value;
  };

  template <class T, class Reference >
  struct MatlabSequence_InputIterator
  {
    typedef MatlabSequence_InputIterator<T, Reference > self;

    typedef std::random_access_iterator_tag iterator_category;
    typedef Reference reference;
    typedef T value_type;
    typedef T* pointer;
    typedef int difference_type;

    MatlabSequence_InputIterator()
    {
    }

    MatlabSequence_InputIterator(mxArray* const seq, int index)
      : _seq(seq), _index(index)
    {
    }

    reference operator*() const
    {
      return reference(_seq, _index);
    }

    MatlabSequence_ArrowProxy<T>
    operator->() const {
      return MatlabSequence_ArrowProxy<T>(operator*());
    }

    bool operator==(const self& ri) const
    {
      return (_index == ri._index);
    }

    bool operator!=(const self& ri) const
    {
      return !(operator==(ri));
    }

    self& operator ++ ()
    {
      ++_index;
      return *this;
    }

    self& operator -- ()
    {
      --_index;
      return *this;
    }

    self& operator += (difference_type n)
    {
      _index += n;
      return *this;
    }

    self operator +(difference_type n) const
    {
      return self(_seq, _index + n);
    }

    self& operator -= (difference_type n)
    {
      _index -= n;
      return *this;
    }

    self operator -(difference_type n) const
    {
      return self(_seq, _index - n);
    }

    difference_type operator - (const self& ri) const
    {
      return _index - ri._index;
    }

    bool operator < (const self& ri) const
    {
      return _index < ri._index;
    }

    reference
    operator[](difference_type n) const
    {
      return reference(_seq, _index + n);
    }

  private:
    mxArray* _seq;
    difference_type _index;
  };

  template <class T>
  struct MatlabSequence_Cont
  {
    typedef MatlabSequence_Ref<T> reference;
    typedef const MatlabSequence_Ref<T> const_reference;
    typedef T value_type;
    typedef T* pointer;
    typedef int difference_type;
    typedef int size_type;
    typedef const pointer const_pointer;
    typedef MatlabSequence_InputIterator<T, reference> iterator;
    typedef MatlabSequence_InputIterator<T, const_reference> const_iterator;

    MatlabSequence_Cont(mxArray* const seq) : _seq(seq)
    {
      if (!seq || !mxIsCell(seq)) {
	throw std::invalid_argument("a cell is expected");
      }
    }

    ~MatlabSequence_Cont()
    {
    }

    size_type size() const
    {
      return static_cast<size_type>(mxGetNumberOfElements(_seq));
    }

    bool empty() const
    {
      return size() == 0;
    }

    iterator begin()
    {
      return iterator(_seq, 0);
    }

    const_iterator begin() const
    {
      return const_iterator(_seq, 0);
    }

    iterator end()
    {
      return iterator(_seq, size());
    }

    const_iterator end() const
    {
      return const_iterator(_seq, size());
    }

    reference operator[](difference_type n)
    {
      return reference(_seq, n);
    }

    const_reference operator[](difference_type n)  const
    {
      return const_reference(_seq, n);
    }

    bool check(bool set_err = true) const
    {
      int s = size();
      for (int i = 0; i < s; ++i) {
	if (!swig::check<value_type>(mxGetCell(_seq, i))) {
	  if (set_err) {
	    SWIG_Error_Format(SWIG_TypeError, "in sequence element %d", i);
	  }
	  return false;
	}
      }
      return true;
    }

  private:
    mxArray* _seq;
  };

}
}

%define %swig_sequence_iterator(Sequence...)
#if defined(SWIG_EXPORT_ITERATOR_METHODS)
  class iterator;
  class reverse_iterator;
  class const_iterator;
  class const_reverse_iterator;

  %typemap(out,noblock=1,fragment="MatlabSequence_Cont")
    iterator, reverse_iterator, const_iterator, const_reverse_iterator {
    $result = SWIG_NewPointerObj(swig::make_output_iterator(%static_cast($1,const $type &)),
				 swig::MatlabSwigIterator::descriptor(),SWIG_POINTER_OWN);
  }
  %typemap(out,fragment="MatlabSequence_Cont")
    std::pair<iterator, iterator>, std::pair<const_iterator, const_iterator> {
    mxArray_list tmpc;
    tmpc.append(SWIG_NewPointerObj(swig::make_output_iterator(%static_cast($1,const $type &).first),
				   swig::MatlabSwigIterator::descriptor(),SWIG_POINTER_OWN));
    tmpc.append(SWIG_NewPointerObj(swig::make_output_iterator(%static_cast($1,const $type &).second),
				   swig::MatlabSwigIterator::descriptor(),SWIG_POINTER_OWN));
    $result = Cell(tmpc);
  }

  %fragment("SwigPyPairBoolOutputIterator","header",fragment=SWIG_From_frag(bool),fragment="MatlabSequence_Cont") {}

  %typemap(out,fragment="MatlabPairBoolOutputIterator")
    std::pair<iterator, bool>, std::pair<const_iterator, bool> {
    mxArray_list tmpc;
    tmpc.append(SWIG_NewPointerObj(swig::make_output_iterator(%static_cast($1,const $type &).first),
				   swig::MatlabSwigIterator::descriptor(),SWIG_POINTER_OWN));
    tmpc.append(SWIG_From(bool)(%static_cast($1,const $type &).second));
    $result = Cell(tmpc);
  }

  %typemap(in,noblock=1,fragment="MatlabSequence_Cont")
    iterator(swig::MatlabSwigIterator *iter = 0, int res),
    reverse_iterator(swig::MatlabSwigIterator *iter = 0, int res),
    const_iterator(swig::MatlabSwigIterator *iter = 0, int res),
    const_reverse_iterator(swig::MatlabSwigIterator *iter = 0, int res) {
    res = SWIG_ConvertPtr($input, %as_voidptrptr(&iter), swig::MatlabSwigIterator::descriptor(), 0);
    if (!SWIG_IsOK(res) || !iter) {
      %argument_fail(SWIG_TypeError, "$type", $symname, $argnum);
    } else {
      swig::MatlabSwigIterator_T<$type > *iter_t = dynamic_cast<swig::MatlabSwigIterator_T<$type > *>(iter);
      if (iter_t) {
	$1 = iter_t->get_current();
      } else {
	%argument_fail(SWIG_TypeError, "$type", $symname, $argnum);
      }
    }
  }

  %typecheck(%checkcode(ITERATOR),noblock=1,fragment="MatlabSequence_Cont")
    iterator, reverse_iterator, const_iterator, const_reverse_iterator {
    swig::MatlabSwigIterator *iter = 0;
    int res = SWIG_ConvertPtr($input, %as_voidptrptr(&iter), swig::MatlabSwigIterator::descriptor(), 0);
    $1 = (SWIG_IsOK(res) && iter && (dynamic_cast<swig::MatlabSwigIterator_T<$type > *>(iter) != 0));
  }

  %fragment("MatlabSequence_Cont");
#endif //SWIG_EXPORT_ITERATOR_METHODS
%enddef

// The matlab container methods

%define %swig_container_methods(Container...)
%enddef

%define %swig_sequence_methods_common(Sequence...)
  %swig_sequence_iterator(%arg(Sequence))
  %swig_container_methods(%arg(Sequence))

  %fragment("MatlabSequence_Base");

  %extend {
    value_type pop() throw (std::out_of_range) {
      if (self->size() == 0)
	throw std::out_of_range("pop from empty container");
      Sequence::value_type x = self->back();
      self->pop_back();
      return x;
    }

    // Cell access in Matlab conventions
    value_type brace(difference_type i) throw (std::out_of_range) {
      return *(swig::cgetpos(self, i));
    }

    // Cell assign in Matlab conventions
    void setbrace(value_type x, difference_type i) throw (std::out_of_range) {
      *(swig::getpos(self,i)) = x;
    }

    void append(value_type x) {
      self->push_back(x);
    }
  }

%enddef

%define %swig_sequence_methods(Sequence...)
  %swig_sequence_methods_common(%arg(Sequence))
%enddef

%define %swig_sequence_methods_val(Sequence...)
  %swig_sequence_methods_common(%arg(Sequence))
%enddef

//
// Common fragments
//

/* Native MATLAB arrays accepted for, and returned from, sequences of T besides
   cells.  Default: none (cells only).  Specializations: numeric and logical
   vectors for arithmetic T, char matrices (one string per row) and string
   arrays for std::string. */

%fragment("MatlabArrayTraits","header",fragment="StdTraits")
{
%#include <limits>
%#include <cmath>
namespace swig {
  struct matlab_array_traits_none {
    // Owned replacement to convert instead of obj, or 0
    static mxArray* prepare(mxArray*) { return 0; }
    // obj is a native array of T
    static bool check(mxArray*) { return false; }
    static size_t size(mxArray*) { return 0; }
    // Convert element i and append it to seq (seq==0: check only)
    template <class Seq>
    static bool append(mxArray*, size_t, Seq*) { return false; }
    // Native array for n elements, or 0 to return a cell
    static mxArray* create(size_t) { return 0; }
    template <class U>
    static void set(mxArray*, size_t, const U&) {}
  };

  template <class T>
  struct matlab_array_traits : matlab_array_traits_none {};

  inline bool matlab_is_vector_shape(mxArray* obj) {
    return mxGetNumberOfDimensions(obj)==2 && (mxGetM(obj)<=1 || mxGetN(obj)<=1);
  }

  // Real, non-sparse numeric or logical vector
  inline bool matlab_is_numeric_vector(mxArray* obj) {
    return obj && (mxIsNumeric(obj) || mxIsLogical(obj)) && !mxIsComplex(obj)
      && !mxIsSparse(obj) && matlab_is_vector_shape(obj);
  }

  inline double matlab_double_element(mxArray* obj, size_t i) {
    const void* d = mxGetData(obj);
    switch (mxGetClassID(obj)) {
    case mxDOUBLE_CLASS: return static_cast<const double*>(d)[i];
    case mxSINGLE_CLASS: return static_cast<const float*>(d)[i];
    case mxINT8_CLASS: return static_cast<const int8_T*>(d)[i];
    case mxUINT8_CLASS: return static_cast<const uint8_T*>(d)[i];
    case mxINT16_CLASS: return static_cast<const int16_T*>(d)[i];
    case mxUINT16_CLASS: return static_cast<const uint16_T*>(d)[i];
    case mxINT32_CLASS: return static_cast<const int32_T*>(d)[i];
    case mxUINT32_CLASS: return static_cast<const uint32_T*>(d)[i];
    case mxINT64_CLASS: return static_cast<double>(static_cast<const int64_T*>(d)[i]);
    case mxUINT64_CLASS: return static_cast<double>(static_cast<const uint64_T*>(d)[i]);
    case mxLOGICAL_CLASS: return static_cast<const mxLogical*>(d)[i] ? 1 : 0;
    default: return 0;
    }
  }

  template <class I, class S>
  inline bool matlab_integer_cast(S x, I* v) {
    I t = static_cast<I>(x);
    if (static_cast<S>(t) != x || (x < 0) != (t < 0)) return false;
    if (v) *v = t;
    return true;
  }

  template <class I>
  inline bool matlab_integer_element(mxArray* obj, size_t i, I* v) {
    typedef std::numeric_limits<I> limits;
    switch (mxGetClassID(obj)) {
    case mxINT64_CLASS:
      return matlab_integer_cast(static_cast<const int64_T*>(mxGetData(obj))[i], v);
    case mxUINT64_CLASS:
      return matlab_integer_cast(static_cast<const uint64_T*>(mxGetData(obj))[i], v);
    default:
      {
	double x = matlab_double_element(obj, i);
	if (x != std::floor(x) || x < static_cast<double>(limits::min())
	    || x >= static_cast<double>(limits::max()) + 1.0) return false;
	if (v) *v = static_cast<I>(x);
	return true;
      }
    }
  }

  // Integers: integral elements of any numeric or logical class; returned as double
  template <class T>
  struct matlab_integer_array_traits : matlab_array_traits_none {
    static bool check(mxArray* obj) { return matlab_is_numeric_vector(obj); }
    static size_t size(mxArray* obj) { return mxGetNumberOfElements(obj); }
    template <class Seq>
    static bool append(mxArray* obj, size_t i, Seq* seq) {
      T v;
      if (!matlab_integer_element(obj, i, seq ? &v : (T*)0)) return false;
      if (seq) seq->insert(seq->end(), v);
      return true;
    }
    static mxArray* create(size_t n) { return mxCreateDoubleMatrix(1, n, mxREAL); }
    static void set(mxArray* obj, size_t i, const T& v) {
      static_cast<double*>(mxGetData(obj))[i] = static_cast<double>(v);
    }
  };

  // Floating point: any numeric or logical class; returned as double
  template <class T>
  struct matlab_floating_array_traits : matlab_array_traits_none {
    static bool check(mxArray* obj) { return matlab_is_numeric_vector(obj); }
    static size_t size(mxArray* obj) { return mxGetNumberOfElements(obj); }
    template <class Seq>
    static bool append(mxArray* obj, size_t i, Seq* seq) {
      if (seq) seq->insert(seq->end(), static_cast<T>(matlab_double_element(obj, i)));
      return true;
    }
    static mxArray* create(size_t n) { return mxCreateDoubleMatrix(1, n, mxREAL); }
    static void set(mxArray* obj, size_t i, const T& v) {
      static_cast<double*>(mxGetData(obj))[i] = static_cast<double>(v);
    }
  };

  template <> struct matlab_array_traits<short> : matlab_integer_array_traits<short> {};
  template <> struct matlab_array_traits<unsigned short> : matlab_integer_array_traits<unsigned short> {};
  template <> struct matlab_array_traits<int> : matlab_integer_array_traits<int> {};
  template <> struct matlab_array_traits<unsigned int> : matlab_integer_array_traits<unsigned int> {};
  template <> struct matlab_array_traits<long> : matlab_integer_array_traits<long> {};
  template <> struct matlab_array_traits<unsigned long> : matlab_integer_array_traits<unsigned long> {};
  template <> struct matlab_array_traits<long long> : matlab_integer_array_traits<long long> {};
  template <> struct matlab_array_traits<unsigned long long> : matlab_integer_array_traits<unsigned long long> {};
  template <> struct matlab_array_traits<float> : matlab_floating_array_traits<float> {};
  template <> struct matlab_array_traits<double> : matlab_floating_array_traits<double> {};

  // bool: logical vectors only (like the bool scalar); returned as logical
  template <>
  struct matlab_array_traits<bool> : matlab_array_traits_none {
    static bool check(mxArray* obj) { return matlab_is_numeric_vector(obj) && mxIsLogical(obj); }
    static size_t size(mxArray* obj) { return mxGetNumberOfElements(obj); }
    template <class Seq>
    static bool append(mxArray* obj, size_t i, Seq* seq) {
      if (seq) seq->insert(seq->end(), mxGetLogicals(obj)[i] != 0);
      return true;
    }
    static mxArray* create(size_t n) { return mxCreateLogicalMatrix(1, n); }
    static void set(mxArray* obj, size_t i, const bool& v) { mxGetLogicals(obj)[i] = v; }
  };

  // std::string: string arrays (via cellstr) and char matrices, one string per
  // row with trailing blanks removed; returned as a cell (cellstr)
  template <>
  struct matlab_array_traits<std::string> : matlab_array_traits_none {
    static mxArray* prepare(mxArray* obj) {
      mxArray* c = 0;
      if (!mxIsClass(obj, "string")) return 0;
      if (SWIG_Matlab_CallInterp(1, &c, 1, &obj, "cellstr")) return 0;
      return c;
    }
    static bool check(mxArray* obj) { return mxIsChar(obj) && mxGetNumberOfDimensions(obj)==2; }
    static size_t size(mxArray* obj) { return mxGetM(obj); }
    template <class Seq>
    static bool append(mxArray* obj, size_t i, Seq* seq) {
      if (seq) {
	size_t nrow = mxGetM(obj), len = mxGetN(obj);
	const mxChar* d = mxGetChars(obj);
	while (len > 0 && (d[i + nrow*(len - 1)]==' ' || d[i + nrow*(len - 1)]==0)) --len;
	std::string s(len, ' ');
	for (size_t k = 0; k < len; ++k) s[k] = static_cast<char>(d[i + nrow*k]);
	seq->insert(seq->end(), s);
      }
      return true;
    }
  };
}
}

%fragment("StdSequenceTraits","header",
	  fragment="StdTraits",
	  fragment="MatlabSequence_Cont",
	  fragment="MatlabArrayTraits")
{
namespace swig {
  template <class Matlabseq, class Seq>
  inline void
  assign(const Matlabseq& matlabseq, Seq* seq) {
%#ifdef SWIG_STD_NOASSIGN_STL
    typedef typename Matlabseq::value_type value_type;
    typename Matlabseq::const_iterator it = matlabseq.begin();
    for (;it != matlabseq.end(); ++it) {
      seq->insert(seq->end(),(value_type)(*it));
    }
%#else
    seq->assign(matlabseq.begin(), matlabseq.end());
%#endif
  }

  // Row, column or empty cell, a native array (matlab_array_traits) or a
  // wrapped sequence.  Every element is checked, also when seq==0.
  template <class Seq, class T = typename Seq::value_type >
  struct traits_asptr_stdseq {
    typedef Seq sequence;
    typedef T value_type;
    typedef matlab_array_traits<T> array_traits;

    static bool is_vector_cell(mxArray* obj) {
      return mxIsCell(obj) && matlab_is_vector_shape(obj);
    }

    static int asptr_native(mxArray* obj, sequence **seq) {
      size_t n;
      bool cell = is_vector_cell(obj);
      if (cell) {
	n = mxGetNumberOfElements(obj);
      } else if (array_traits::check(obj)) {
	n = array_traits::size(obj);
      } else {
	return SWIG_ERROR;
      }
      if (!seq) {
	for (size_t i = 0; i < n; ++i) {
	  if (cell ? !swig::check<value_type>(mxGetCell(obj, i))
	      : !array_traits::append(obj, i, (sequence*)0)) return SWIG_ERROR;
	}
	return SWIG_OK;
      }
      sequence *p = new sequence();
      try {
	for (size_t i = 0; i < n; ++i) {
	  if (cell) {
	    p->insert(p->end(), swig::as<value_type>(mxGetCell(obj, i)));
	  } else if (!array_traits::append(obj, i, p)) {
	    throw std::invalid_argument("bad type");
	  }
	}
      } catch (std::exception&) {
	delete p;
	return SWIG_ERROR;
      }
      *seq = p;
      return SWIG_NEWOBJ;
    }

    static int asptr(mxArray* obj, sequence **seq) {
      if (!obj) return SWIG_ERROR;
      mxArray* tmp = array_traits::prepare(obj);
      int res = asptr_native(tmp ? tmp : obj, seq);
      if (tmp) mxDestroyArray(tmp);
      if (SWIG_IsOK(res)) return res;
      sequence *p = 0;
      swig_type_info *descriptor = swig::type_info<sequence>();
      if (descriptor && SWIG_IsOK(SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0))) {
	if (seq) *seq = p;
	return SWIG_OLDOBJ;
      }
      return SWIG_ERROR;
    }
  };

  // Native array (matlab_array_traits) or 1xN cell; 0 if an element fails
  template <class Seq, class T = typename Seq::value_type >
  struct traits_from_stdseq {
    typedef Seq sequence;
    typedef T value_type;
    typedef typename sequence::const_iterator const_iterator;
    typedef matlab_array_traits<T> array_traits;

    static mxArray* from(const sequence& seq) {
%#ifdef SWIG_MATLAB_EXTRA_NATIVE_CONTAINERS
      swig_type_info *desc = swig::type_info<sequence>();
      if (desc && desc->clientdata) {
	return SWIG_NewPointerObj(new sequence(seq), desc, SWIG_POINTER_OWN);
      }
%#endif
      size_t i = 0;
      mxArray* r = array_traits::create(seq.size());
      if (r) {
	for (const_iterator it = seq.begin(); it != seq.end(); ++it, ++i) {
	  array_traits::set(r, i, *it);
	}
	return r;
      }
      r = mxCreateCellMatrix(1, seq.size());
      for (const_iterator it = seq.begin(); it != seq.end(); ++it, ++i) {
	mxArray* e = swig::from<value_type>(*it);
	if (!e) {
	  mxDestroyArray(r);
	  return 0;
	}
	mxSetCell(r, i, e);
      }
      return r;
    }
  };
}
}
