/* [temp.explicit]/10: an explicit instantiation of a class defines its
   static data members too */
template <class T> struct X0 { static int value; void f() {} };
template <class T> int X0<T>::value = 3;
struct X1 {}; struct X3 {};
template struct X0<X1>;
template struct X0<X3>;
int main() { return 0; }
