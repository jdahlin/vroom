// RUN: %clang_cc1 -std=c++11 %s
// RUN: %clang_cc1 -std=c++20 %s
// [temp.explicit]/1 with [ext:gnu_attributes]: a GNU attribute is a
// decl-specifier of an explicit instantiation, before the declaration or
// after the class-key (libc++: extern template _LIBCPP_EXPORTED_FROM_ABI
// void __sort<...>(...)).
template <class T> void f(T) {}
template <class T> struct C { void m() {} };
template __attribute__((visibility("default"))) void f<int>(int);
extern template __attribute__((visibility("default"))) void f<long>(long);
template class __attribute__((visibility("default"))) C<int>;
extern template class __attribute__((visibility("default"))) C<long>;
template __attribute__((visibility("default"))) void C<char>::m();
