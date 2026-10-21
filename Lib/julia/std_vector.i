// std::vector <-> Julia Array, via the sequence traits of juliacontainer.swg

%include <std_common.i>
%include <juliacontainer.swg>

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
      static int asptr(jl_value_t* obj, std::vector<T, A> **vec) {
        return traits_asptr_stdseq<std::vector<T, A> >::asptr(obj, vec);
      }
    };

    template <class T, class A>
    struct traits_from<std::vector<T, A> > {
      static jl_value_t* from(const std::vector<T, A>& vec) {
        return traits_from_stdseq<std::vector<T, A> >::from(vec);
      }
    };
  }
%}

// Template declaration only: values cross through the traits above
namespace std {
  template <typename T> class vector {
  public:
    typedef T value_type;
    typedef T const_reference;
    vector();
    unsigned long size() const;
    bool empty() const;
    void clear();
    void push_back(T x);
    T at(unsigned long i) const;
  };
}
