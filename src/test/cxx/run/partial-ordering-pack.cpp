// [temp.class.order]/1, [temp.deduct.type]/9: a fixed argument list
// specializes a trailing expansion, including when the pack is empty.
template<class...> struct C;
template<class T, class... U> struct C<T, U...> { static constexpr int value = 2; };
template<class T> struct C<T> { static constexpr int value = 1; };
static_assert(C<int>::value == 1);
static_assert(C<int, int>::value == 2);
static_assert(C<int, char>::value == 2);
static_assert(C<char>::value == 1);
static_assert(C<char, int>::value == 2);
int main() { return C<int>::value != 1 || C<int, int>::value != 2; }
