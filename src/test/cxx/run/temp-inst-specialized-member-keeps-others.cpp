// [temp.expl.spec]/7: an explicit specialization of a class template's
// static data member after the class is instantiated replaces that member's
// definition only; the operator's out-of-class definition is still made
// ([temp.inst]/2-4, defined when needed)
template<class T> struct C { int operator+(int); static int y; };
template<class T> int C<T>::operator+(int x) { return x; }
template<class T> int C<T>::y = 1;
C<int> c;
template<> int C<int>::y = 2;
int main() { return (c + 1) + C<int>::y; }
