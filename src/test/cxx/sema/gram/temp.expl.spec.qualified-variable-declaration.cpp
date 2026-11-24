// [temp.expl.spec]/1, [temp.expl.spec]/13: an explicit specialization of a variable template named through its namespace, a declaration without an initializer
namespace N1 {
template <typename T> int x2;
}
template <> int N1::x2<int>;
template <> int N1::x2<char> = 3;
