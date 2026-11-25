// [abi:mangle.template-arg] [impl.clang] an array as the argument of a pointer template parameter
// (its first element, [conv.array]): X ad so <element type> L _Z <encoding> E E E, the
// sub-object expression; an object's address X ad L _Z <encoding> E E (clang's
// SemaCXX/warn-bool-conversion.cpp namespace Template)
namespace Template {
template <int *p> int f() { return 1; }
int a = 1, b[3] = {2, 3, 4};
template int f<&a>();
template int f<b>();
} // namespace Template
int main() { return Template::f<&Template::a>() + Template::f<Template::b>() == 2 ? 0 : 1; }
