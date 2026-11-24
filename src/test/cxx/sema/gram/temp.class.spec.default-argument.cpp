// [temp.class.spec]/9: the template parameter list of a partial specialization shall not contain default template arguments
template <typename T> struct S;
template <typename T, typename U = void> struct S<T *> {}; // error
template <typename T> int n;
template <typename T, typename U = void> int n<T *> = 1; // error
