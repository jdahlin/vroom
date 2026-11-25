// [temp.class.spec.match]/1.2: a competing partial specialization must not
// be ignored when ordering a template-template parameter is unresolved.
template<class T> struct id { using type = T; };
template<class, class, template<class> class> struct D;
template<class T, template<class> class F>
struct D<T, typename id<T>::type, F> {};
template<class T, template<class> class F>
struct D<typename id<T>::type, T, F> {};
D<int, int, id> d; // expected-error {{ambiguous partial specializations}}
