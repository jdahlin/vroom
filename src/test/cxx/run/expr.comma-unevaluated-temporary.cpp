/* [dcl.type.decltype]: a comma in decltype destroys no temporary */
struct S { ~S() {} };
struct V { S reserve(int); };
template<class T> auto f(T &a) -> decltype(a.reserve(1), 1, void()) {}
int main() { V v; f(v); return 0; }
