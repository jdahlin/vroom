// [abi:mangling-compression] the abbreviations of ::std's char string and streams as a whole (Ss,
// Si, So, Sd), Sa and Sb with arguments for another allocator or basic_string, as a type and as
// the prefix of a member (clang's CodeGenCXX/mangle-subst-std.cpp)
namespace std {
template <typename> struct allocator {
  int allocate(int n) { return n; }
};
template <typename> struct char_traits {};
template <typename C, typename T = char_traits<C>, typename A = allocator<C> > struct basic_string {
  int size() const { return 3; }
};
typedef basic_string<char> string;
template <typename C, typename T = char_traits<C> > struct basic_istream {
  int get() { return 4; }
};
template <typename C, typename T = char_traits<C> > struct basic_ostream {
  int put() { return 5; }
};
template <typename C, typename T = char_traits<C> > struct basic_iostream {
  int both() { return 6; }
};
} // namespace std
int f(std::allocator<char>, std::allocator<int>) { return 1; }
int f(std::basic_string<char, char, int>) { return 2; }
int f(std::string s) { return s.size(); }
int f(std::basic_string<wchar_t> s) { return s.size() + 1; }
int f(std::basic_istream<char> s) { return s.get(); }
int f(std::basic_ostream<char> s) { return s.put(); }
int f(std::basic_iostream<char> s) { return s.both(); }
int f(std::basic_istream<wchar_t> s) { return s.get() + 1; }
int main() {
  std::allocator<char> a;
  return f(a, std::allocator<int>()) + f(std::basic_string<char, char, int>()) + f(std::string()) +
                     f(std::basic_string<wchar_t>()) + f(std::basic_istream<char>()) +
                     f(std::basic_ostream<char>()) + f(std::basic_iostream<char>()) +
                     f(std::basic_istream<wchar_t>()) + a.allocate(1) ==
                 31
             ? 0
             : 1;
}
