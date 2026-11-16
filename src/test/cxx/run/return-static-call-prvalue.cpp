// std: c++23
// return X()(b, n); of a class returned by address, through a static operator() on a temporary (libc++'s
// __specialized_algorithm, vector<bool>'s fill_n): the result built in the caller's object
struct It { int k; It(int k) : k(k) {} It(const It &o) : k(o.k) {} It operator+(int n) const { return It(k + n); } };
struct F { template <class S> static It operator()(It b, S n) { return b + n; } };
template <class O> O g(O o) { return F()(o, 2); }
int main() { return g(It(5)).k == 7 ? 0 : 1; }
