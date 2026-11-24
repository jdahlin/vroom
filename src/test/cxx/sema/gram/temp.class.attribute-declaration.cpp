// [temp.class]/1, [dcl.type.elab]/1: a class template declared with an attribute that changes nothing
template <class T> class [[deprecated]] X;
template <class T> class [[deprecated]] X {};
template <class T> struct [[gnu::unused]] Y;
template <class T> struct Y {};
template <class T> struct [[deprecated]] Z;
template <class T> struct Z;
Y<int> y;
Z<char> *z;
