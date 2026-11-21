// [abi:mangling-compression]: Sb abbreviates ::std::basic_string only, not a
// specialization of a class in an inline namespace of std (libc++'s __1).
namespace std {
inline namespace __1 {
template <class C, class T = int, class A = int> struct basic_string { C c; };
}
}
int take(std::basic_string<char> s) { return s.c; }

int main() {
    std::basic_string<char> s{3};
    return take(s) != 3;
}
