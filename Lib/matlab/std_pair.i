// Pairs

%include <matlabstdcommon.swg>

//#define SWIG_STD_PAIR_ASVAL

%fragment("StdPairTraits","header",fragment="StdTraits") {
  namespace swig {
    // A pair is a 1x2 cell
    inline bool matlab_is_pair_cell(mxArray* obj) {
      return obj && mxIsCell(obj) && mxGetNumberOfDimensions(obj)==2
	&& mxGetM(obj)==1 && mxGetN(obj)==2;
    }

#ifdef SWIG_STD_PAIR_ASVAL
    template <class T, class U >
    struct traits_asval<std::pair<T,U> >  {
      typedef std::pair<T,U> value_type;

      static int get_pair(mxArray* first, mxArray* second,
			  std::pair<T,U> *val)
      {
	if (val) {
	  T *pfirst = &(val->first);
	  int res1 = swig::asval(first, pfirst);
	  if (!SWIG_IsOK(res1))
	    return res1;
	  U *psecond = &(val->second);
	  int res2 = swig::asval(second, psecond);
	  if (!SWIG_IsOK(res2))
	    return res2;
	  return res1 > res2 ? res1 : res2;
	} else {
	  T *pfirst = 0;
	  int res1 = swig::asval(first, pfirst);
	  if (!SWIG_IsOK(res1))
	    return res1;
	  U *psecond = 0;
	  int res2 = swig::asval(second, psecond);
	  if (!SWIG_IsOK(res2))
	    return res2;
	  return res1 > res2 ? res1 : res2;
	}
      }

      static int asval(mxArray* obj, std::pair<T,U> *val) {
	if (matlab_is_pair_cell(obj)) {
	  mxArray* c0 = mxGetCell(obj,0);
	  mxArray* c1 = mxGetCell(obj,1);
	  if (c0==0 || c1==0) return SWIG_ERROR;
	  return get_pair(c0,c1,val);
	} else {
	  value_type *p = 0;
	  swig_type_info *descriptor = swig::type_info<value_type>();
	  int res = obj && descriptor ? SWIG_ConvertPtr(obj,(void**)&p,descriptor,0) : SWIG_ERROR;
	  if (SWIG_IsOK(res) && val)
	    *val = *p;
	  return res;
	}
      }
    };

#else
    template <class T, class U >
    struct traits_asptr<std::pair<T,U> >  {
      typedef std::pair<T,U> value_type;

      static int get_pair(mxArray* first, mxArray* second,
			  std::pair<T,U> **val) 
      {
	if (val) {
	  value_type *vp = %new_instance(std::pair<T,U>);
	  T *pfirst = &(vp->first);
	  int res1 = swig::asval(first, pfirst);
	  if (!SWIG_IsOK(res1)) {
	    %delete(vp);
	    return res1;
	  }
	  U *psecond = &(vp->second);
	  int res2 = swig::asval(second, psecond);
	  if (!SWIG_IsOK(res2)) {
	    %delete(vp);
	    return res2;
	  }
	  *val = vp;
	  return SWIG_AddNewMask(res1 > res2 ? res1 : res2);
	} else {
	  T *pfirst = 0;
	  int res1 = swig::asval(first, pfirst);
	  if (!SWIG_IsOK(res1))
	    return res1;
	  U *psecond = 0;
	  int res2 = swig::asval(second, psecond);
	  if (!SWIG_IsOK(res2))
	    return res2;
	  return res1 > res2 ? res1 : res2;
	}
      }

      static int asptr(mxArray* obj, std::pair<T,U> **val) {
	if (matlab_is_pair_cell(obj)) {
	  mxArray* c0 = mxGetCell(obj,0);
	  mxArray* c1 = mxGetCell(obj,1);
	  if (c0==0 || c1==0) return SWIG_ERROR;
	  return get_pair(c0,c1,val);
	} else {
	  value_type *p = 0;
	  swig_type_info *descriptor = swig::type_info<value_type>();
	  int res = obj && descriptor ? SWIG_ConvertPtr(obj,(void**)&p,descriptor,0) : SWIG_ERROR;
	  if (SWIG_IsOK(res) && val)
	    *val = p;
	  return res;
	}
      }
    };

#endif
    template <class T, class U >
    struct traits_from<std::pair<T,U> >   {
      static mxArray* from(const std::pair<T,U>& val) {
	mxArray* first = swig::from(val.first);
	if (!first) return 0;
	mxArray* second = swig::from(val.second);
	if (!second) {
	  mxDestroyArray(first);
	  return 0;
	}
	mxArray* c = mxCreateCellMatrix(1,2);
	mxSetCell(c,0,first);
	mxSetCell(c,1,second);
	return c;
      }
    };
  }
}

%define %swig_pair_methods(pair...)
%enddef

%include <std/std_pair.i>

