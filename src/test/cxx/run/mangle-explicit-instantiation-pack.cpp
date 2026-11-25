// [abi:mangle.template-arg] an explicit instantiation of a function template whose last parameter
// is a pack: the arguments written from the pack's position on are its elements, J ... E
// (clang's CodeGenCXX/mangle-variadic-templates.cpp: template void f1<int>())
template <typename... Types> int f1() { return sizeof...(Types); }
template int f1<int>();
template int f1<int, float>();
template int f1<>();
template <typename... Types> int f2(Types...) { return sizeof...(Types); }
template int f2<int>(int);
template int f2<int, float>(int, float);
template int f2(char, char);
template <class A, typename... Types> int f3(A, Types...) { return 10 + sizeof...(Types); }
template int f3<int, int>(int, int);
template int f3<int>(int, char);
int main() { return f1<int>() == 1 && f1<int, float>() == 2 && f2(1, 2.0f) == 2 && f3(1, 'c') == 11 ? 0 : 1; }
