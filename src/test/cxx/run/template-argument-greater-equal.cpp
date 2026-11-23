// [temp.names]/3: a >= after an expression in a template-argument-list is
// the operator; only > (and the first > of >> or >>=) ends the list.
template <bool B> struct Flag { static const int value = B; };
template <class... T> struct Pack { static const int value = Flag<sizeof...(T) >= 1>::value; };
template <int N> struct Num { static const int value = Flag<N >= 3>::value; };

int main() {
  if (Pack<>::value != 0 || Pack<int, char>::value != 1)
    return 1;
  if (Num<2>::value != 0 || Num<3>::value != 1)
    return 2;
  return 0;
}
