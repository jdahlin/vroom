// [abi:mangle.array-type] an array bound that is an expression of a non-type template parameter in
// a function template's parameter type: A <expression> _ <element type>, the expression as clang
// mangles it (parentheses dropped, literals with their type); clang's CodeGenCXX/mangle.cpp
// namespace Expressions
template <int i> int f1(int (*)[(-i) + 2]) { return 1; }
template int f1<1>(int (*)[1]);
template <int i> int f2(int (*)[+i]) { return 2; }
template int f2<1>(int (*)[1]);
template <int i> int f3(int (*)[i + i]) { return 3; }
template int f3<1>(int (*)[2]);
template <int i> int f4(int (*)[2 + i + i]) { return 4; }
template int f4<1>(int (*)[4]);
template <bool b> int f7(int (*)[b ? 1 : 2]) { return 7; }
template int f7<true>(int (*)[1]);
template <unsigned N> int f5(int (&)[N + 1u]) { return 5; }
template <int N, int M> int f6(int (&)[N << M | 'a']) { return 6; }
template <int N> int f8(int (&)[N * 2 - 1 > 0 && N != 3]) { return 8; }
int main() {
  int a1[1], a2[2], a4[4], b[99], c[1];
  return f1<1>(&a1) + f2<1>(&a1) + f3<1>(&a2) + f4<1>(&a4) + f7<true>(&a1) + f5<3>(a4) + f6<1, 1>(b) +
                     f8<2>(c) ==
                 36
             ? 0
             : 1;
}
