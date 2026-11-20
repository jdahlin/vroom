// [dcl.init]/1, [temp.names]/1: a declaration analysis keeps as
// tokens holds the names it declares, not the names its initializer uses
// (the template-ids with commas of the initializer are no declarators):
// the static_assert after it stays well-formed (-fcxx-dump=analysis; before,
// unsupported for [temp.names] since same and rcv were held).
template <class T, class U> struct same { static const bool value = false; };
template <class T> struct same<T, T> { static const bool value = true; };
template <class T> using rcv = __remove_cv(T);
template <class T> struct box { using type = T; };
template <class T>
inline const bool w = same<T, rcv<T> >::value && box<T>::type;
int a = 1, b = same<int, rcv<const int> >::value;
static_assert(same<int, rcv<const int> >::value);
