// [temp.expl.spec]/13: an explicit specialization of a variable template without an initializer is a declaration
template <class T> int x0 = 1;
template <> int x0<int>;
template <> int x0<char> = 3;
int y = x0<int> + x0<char> + x0<long>;
