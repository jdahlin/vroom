// a function template whose return type would be an array (or a function) is not viable: no such type
// ([temp.deduct]/11.10; json's get<char[8]>() in is_getable)
template <class T> T g();
template <class T> decltype(g<T>()) h(int);
template <class T> long h(...);
int main() { return sizeof(h<char[8]>(0)) == sizeof(long) ? 0 : 1; }
