// a pack preceded by a defaulted template parameter (json's concat<OutStringType = std::string>(args...)):
// its default when it is neither deduced nor given
struct Str { int n = 0; };
template <class O = Str, class... A> O concat(A &&...a) { O o; o.n = (int)sizeof...(A); return o; }
int main() { return concat(1, 2, 3).n == 3 && concat<Str>(1).n == 1 ? 0 : 1; }
