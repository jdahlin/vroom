/* [temp.names]: template-argument-list closed by >> and nested */
template <class T> struct V { T v; };
template <int N> struct I { static const int n = N; };
int main() { V<V<int>> x{{2}}; return x.v.v - 2 + I<(8 >> 1)>::n - 4; }
