/* [temp.names]: the template disambiguator on a dependent member template */
struct S { template <class T> static int g() { return sizeof(T); } };
template <class X> int f() { return X::template g<int>(); }
int main() { return f<S>() - 4; }
