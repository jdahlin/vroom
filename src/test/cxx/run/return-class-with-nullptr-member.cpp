extern "C" int printf(const char*, ...);
typedef decltype(nullptr) nullptr_t;
template<class L> struct Lhs { L lhs; int at; explicit Lhs(L&& in, int a) : lhs(static_cast<L&&>(in)), at(a) {} };
struct Dec { int at; template<class L> Lhs<L> operator<<(L&& l) { return Lhs<L>(static_cast<L&&>(l), at); } };
int main() { auto e = Dec{3} << nullptr; printf("%d %d\n", e.at, e.lhs == nullptr); }
