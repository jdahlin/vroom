// operator<<(os, j) by name: the stream's namespace has overloads j reaches only through its conversion
// function; the class's hidden friend, an exact match, wins (one candidate set: json's operator<< by name,
// doctest's has_insertion_operator)
namespace io {
struct OS { int n = 0; };
OS &operator<<(OS &o, int v) { o.n = 100 + v; return o; }
OS &operator<<(OS &o, const char *) { o.n = 200; return o; }
}
namespace lib {
template <class T> struct J {
  T v;
  template <class U> operator U() const { return U(); }
  friend io::OS &operator<<(io::OS &o, const J &j) { o.n = (int)j.v; return o; }
};
}
int main() { io::OS o; lib::J<int> j{5}; operator<<(o, j); int a = o.n; o << j; return a == 5 && o.n == 5 ? 0 : 1; }
