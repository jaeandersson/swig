// Maps

%include <matlabcontainer.swg>

%fragment("StdMapCommonTraits","header",fragment="StdSequenceTraits")
{
  namespace swig {
    template <class ValueType>
    struct from_key_oper 
    {
      typedef const ValueType& argument_type;
      typedef mxArray* result_type;
      result_type operator()(argument_type v) const
      {
	return swig::from(v.first);
      }
    };

    template <class ValueType>
    struct from_value_oper 
    {
      typedef const ValueType& argument_type;
      typedef mxArray* result_type;
      result_type operator()(argument_type v) const
      {
	return swig::from(v.second);
      }
    };

    template<class OutIterator, class FromOper, class ValueType = typename OutIterator::value_type>
    struct MatlabMapIterator_T : MatlabSwigIteratorClosed_T<OutIterator, ValueType, FromOper>
    {
      MatlabMapIterator_T(OutIterator curr, OutIterator first, OutIterator last, mxArray* seq)
	: MatlabSwigIteratorClosed_T<OutIterator,ValueType,FromOper>(curr, first, last, seq)
      {
      }
    };


    template<class OutIterator,
	     class FromOper = from_key_oper<typename OutIterator::value_type> >
    struct MatlabMapKeyIterator_T : MatlabMapIterator_T<OutIterator, FromOper>
    {
      MatlabMapKeyIterator_T(OutIterator curr, OutIterator first, OutIterator last, mxArray* seq)
	: MatlabMapIterator_T<OutIterator, FromOper>(curr, first, last, seq)
      {
      }
    };

    template<typename OutIter>
    inline MatlabSwigIterator*
    make_output_key_iterator(const OutIter& current, const OutIter& begin, const OutIter& end, mxArray* seq = 0)
    {
      return new MatlabMapKeyIterator_T<OutIter>(current, begin, end, seq);
    }

    template<class OutIterator,
	     class FromOper = from_value_oper<typename OutIterator::value_type> >
    struct MatlabMapValueIterator_T : MatlabMapIterator_T<OutIterator, FromOper>
    {
      MatlabMapValueIterator_T(OutIterator curr, OutIterator first, OutIterator last, mxArray* seq)
	: MatlabMapIterator_T<OutIterator, FromOper>(curr, first, last, seq)
      {
      }
    };
    

    template<typename OutIter>
    inline MatlabSwigIterator*
    make_output_value_iterator(const OutIter& current, const OutIter& begin, const OutIter& end, mxArray* seq = 0)
    {
      return new MatlabMapValueIterator_T<OutIter>(current, begin, end, seq);
    }
  }
}

%fragment("StdMapTraits","header",fragment="StdMapCommonTraits")
{
%#include <cctype>
  namespace swig {
    template <class MatlabSeq, class K, class T >
    inline void
    assign(const MatlabSeq& matlabseq, std::map<K,T > *map) {
      typedef typename std::map<K,T>::value_type value_type;
      typename MatlabSeq::const_iterator it = matlabseq.begin();
      for (;it != matlabseq.end(); ++it) {
	map->insert(value_type(it->first, it->second));
      }
    }

    inline bool matlab_is_field_name(const std::string& s) {
      if (s.empty() || s.size() > 63 || !std::isalpha(static_cast<unsigned char>(s[0]))) return false;
      for (std::string::const_iterator it = s.begin(); it != s.end(); ++it) {
	if (!std::isalnum(static_cast<unsigned char>(*it)) && *it != '_') return false;
      }
      return true;
    }

    // Keys without a MATLAB representation: wrapped maps only
    template <class K, class T, class Compare, class Alloc>
    struct traits_asptr<std::map<K,T,Compare,Alloc > >  {
      typedef std::map<K,T,Compare,Alloc > map_type;
      static int asptr(mxArray* obj, map_type **val) {
	map_type *p = 0;
	swig_type_info *descriptor = swig::type_info<map_type>();
	int res = obj && descriptor ? SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0) : SWIG_ERROR;
	if (SWIG_IsOK(res) && val) *val = p;
	return res;
      }
    };

    template <class K, class T, class Compare, class Alloc>
    struct traits_from<std::map<K,T,Compare,Alloc > >  {
      typedef std::map<K,T,Compare,Alloc > map_type;
      static mxArray* from(const map_type& map) {
	swig_type_info *desc = swig::type_info<map_type>();
	if (desc && desc->clientdata) {
	  return SWIG_NewPointerObj(new map_type(map), desc, SWIG_POINTER_OWN);
	}
	SWIG_Error(SWIG_TypeError, "map key type has no MATLAB representation");
	return 0;
      }
    };

    // String keys: 1x1 struct, one field per key
    template <class T, class Compare, class Alloc>
    struct traits_asptr<std::map<std::string,T,Compare,Alloc > >  {
      typedef std::map<std::string,T,Compare,Alloc > map_type;
      static int asptr(mxArray* obj, map_type **val) {
	if (obj && mxIsStruct(obj) && mxGetNumberOfElements(obj)==1) {
	  int n = mxGetNumberOfFields(obj);
	  if (!val) {
	    for (int k = 0; k < n; ++k) {
	      if (!swig::check<T>(mxGetFieldByNumber(obj, 0, k))) return SWIG_ERROR;
	    }
	    return SWIG_OK;
	  }
	  map_type *p = new map_type();
	  try {
	    for (int k = 0; k < n; ++k) {
	      p->insert(typename map_type::value_type(mxGetFieldNameByNumber(obj, k),
						      swig::as<T>(mxGetFieldByNumber(obj, 0, k))));
	    }
	  } catch (std::exception&) {
	    delete p;
	    return SWIG_ERROR;
	  }
	  *val = p;
	  return SWIG_NEWOBJ;
	}
	map_type *p = 0;
	swig_type_info *descriptor = swig::type_info<map_type>();
	int res = obj && descriptor ? SWIG_ConvertPtr(obj, (void **)&p, descriptor, 0) : SWIG_ERROR;
	if (SWIG_IsOK(res) && val) *val = p;
	return res;
      }
    };

    template <class T, class Compare, class Alloc>
    struct traits_from<std::map<std::string,T,Compare,Alloc > >  {
      typedef std::map<std::string,T,Compare,Alloc > map_type;
      typedef typename map_type::const_iterator const_iterator;
      static mxArray* from(const map_type& map) {
%#ifdef SWIG_MATLAB_EXTRA_NATIVE_CONTAINERS
	swig_type_info *desc = swig::type_info<map_type>();
	if (desc && desc->clientdata) {
	  return SWIG_NewPointerObj(new map_type(map), desc, SWIG_POINTER_OWN);
	}
%#endif
	mxArray* r = mxCreateStructMatrix(1, 1, 0, 0);
	for (const_iterator it = map.begin(); it != map.end(); ++it) {
	  int k = matlab_is_field_name(it->first) ? mxAddField(r, it->first.c_str()) : -1;
	  mxArray* e = k < 0 ? 0 : swig::from<T>(it->second);
	  if (!e) {
	    if (k < 0) SWIG_Error(SWIG_ValueError, "map key is not a valid MATLAB field name");
	    mxDestroyArray(r);
	    return 0;
	  }
	  mxSetFieldByNumber(r, 0, k, e);
	}
	return r;
      }
    };
  }
}

%define %swig_map_common(Map...)
  %swig_sequence_iterator(Map);
  %swig_container_methods(Map);
%enddef

%define %swig_map_methods(Map...)
     %swig_map_common(Map)
%enddef


%include <std/std_map.i>
