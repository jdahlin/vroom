/* [temp.names]: non-type template arguments: > inside parentheses, conversion, sizeof */
template <bool B> struct Flag { static const int v = B; };
template <int N> struct Num { static const int v = N; };
int main() { return Flag<(2 > 1)>::v + Num<sizeof(int)>::v - 5 + Num<(int)3.0>::v - 3; }
