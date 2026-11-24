// [temp.class.spec]/9: a partial specialization whose arguments are its parameters in another order is not more specialized than the primary template
template <typename T, typename U> struct S;
template <typename T, typename U> struct S<U, T> {}; // error
template <typename T, typename U> int a;
template <typename T, typename U> int a<U, T>; // error
template <typename T, typename U> struct R;
template <typename T> struct R<T, T> {};
template <typename T, typename U> struct R<T *, U> {};
