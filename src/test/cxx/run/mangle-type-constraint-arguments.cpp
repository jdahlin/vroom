/* cxx-run-flags: -std=c++20 */
// [abi:mangle.template-arg] [impl.clang] a type-parameter whose type-constraint has template
// arguments: Tk, the concept's name and its arguments after the first (template<D<int>>: Tk1DIiE)
// (clang's CodeGenCXX/mangle-concept.cpp test4)
template <typename T> concept C = sizeof(T) > 0;
template <typename T, typename U> concept D = sizeof(T) >= sizeof(char) && sizeof(U) > 0;
template <C> int f() { return 1; }
template int f<int>();
template <D<int>> int g() { return 2; }
template int g<int>();
template <D<char *>> int h() { return 3; }
template <class X, D<X> Y> int k() { return 4; }
int main() { return f<int>() + g<int>() + h<int>() + k<int, long>() == 10 ? 0 : 1; }
