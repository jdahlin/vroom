// [abi:mangle.decltype], [abi:expressions] decltype(sizeof(...)) and decltype(alignof(...)) in a
// function template's parameter type: a decltype of the expression as written (DT st/sz/at/az ... E,
// clang's u11__alignof__ for __alignof__), not std::size_t; a functional cast T(e) is cv <type> <e>
// (clang's CodeGenCXX/mangle-alignof.cpp, mangle.cpp test34)
template <class T> int f1(decltype(alignof(T))) { return 1; }
template int f1<int>(__SIZE_TYPE__);
template <class T> int f2(decltype(__alignof__(T))) { return 2; }
template int f2<int>(__SIZE_TYPE__);
template <class T> int f3(decltype(alignof(T(0)))) { return 3; }
template int f3<int>(__SIZE_TYPE__);
template <class T> int f4(decltype(__alignof__(T(0)))) { return 4; }
template int f4<int>(__SIZE_TYPE__);
template <typename T> int f(decltype(sizeof(decltype(T() + T())))) { return 5; }
template int f<int>(decltype(sizeof(1)));
template <typename T> int g(decltype(sizeof(T))) { return 6; }
template int g<int>(unsigned long);
template <typename T> int h(decltype(sizeof(T *)), T) { return 7; }
template <typename T> int k(decltype(sizeof(T(1, 2)))) { return 8; }
struct P {
  P(int, int) {}
};
int main() { return f1<int>(1) + f2<int>(1) + f3<int>(1) + f4<int>(1) + f<int>(1) + g<char>(1) + h(1ul, 2) + k<P>(1) == 36 ? 0 : 1; }
