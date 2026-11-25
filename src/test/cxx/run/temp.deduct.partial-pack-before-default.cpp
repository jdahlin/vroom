/* [temp.deduct.partial]: a template parameter pack followed by a defaulted
   parameter is still the pack the function parameter pack expands */
template <class... T> struct Tu {
    int w;
    template <int = 0> Tu(const T &...) : w(1) {}
    template <class... U, int = 0> Tu(U &&...) : w(2) {}
};
int f(const int &a, const long &b) { Tu<int, long> t(a, b); return t.w; }
int main() { return f(1, 2) - 1; }
