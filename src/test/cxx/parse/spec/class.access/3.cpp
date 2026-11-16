// [class.access] Example 3 (paragraph 9), docs/cxx-spec/class/class.access.md
// needs: P7
// status: annotated

class B { };
template <class T> class C {
protected:
  typedef T TT;
};

template <class U, class V = typename U::TT>
class D : public U { };

D <C<B> >* d;       // access error, C::TT is protected // semantic
