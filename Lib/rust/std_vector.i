// std::vector <-> list or numeric vector, via the sequence traits of rustcontainer.swg

%include <std_common.i>
%include <rustcontainer.swg>

%fragment("StdVectorTraits","header",fragment="StdSequenceTraits")
%{
  namespace swig {
    template <class T, class A>
    struct traits<std::vector<T, A> > {
      typedef pointer_category category;
      static const char* type_name() {
        static std::string name = "std::vector<" + std::string(swig::type_name<T>()) + " >";
        return name.c_str();
      }
    };

    template <class T, class A>
    struct traits_asptr<std::vector<T, A> > {
      static int asptr(SwigRV* obj, std::vector<T, A> **vec) {
        return traits_asptr_stdseq<std::vector<T, A> >::asptr(obj, vec);
      }
    };

    template <class T, class A>
    struct traits_from<std::vector<T, A> > {
      static SwigRV* from(const std::vector<T, A>& vec) {
        return traits_from_stdseq<std::vector<T, A> >::from(vec);
      }
    };
  }
%}

/* Declaration only: values cross through the traits above */
namespace std {
  template <class T> class vector {};
}
