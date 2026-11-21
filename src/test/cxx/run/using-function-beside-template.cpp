// [dcl.link]/6: two declarations of a function with C language linkage in
// different namespace scopes refer to the same function, also when the
// global namespace holds its name through a using-declaration beside a
// function template.
namespace impl {
extern "C" int twice(int x) { return x * 2; }
template <class T = int> T twice(T);
}
using impl::twice;
extern "C" int twice(int x);

int main() {
    return twice(2) != 4;
}
