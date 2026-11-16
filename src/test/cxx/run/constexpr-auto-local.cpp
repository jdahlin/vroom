// constexpr auto x = ...; in a function body: an automatic variable typed by its initializer
template <class T> int lim() { constexpr auto m = (T)-1 < 0 ? 1 : 2; return m; }
int main() { constexpr auto f_ = false; constexpr auto n = 40; return !f_ && n + lim<int>() + lim<unsigned>() == 43 ? 0 : 1; }
