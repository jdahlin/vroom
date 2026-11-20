/* [dcl.attr.nouniqueaddr]/2: [[no_unique_address]] on a non-static data
   member of a type that is not a class, or of an array, changes nothing: the
   members are laid out as C's and the objects run as clang's.  (A member of
   class type, laid out otherwise, is no-unique-address-layout.cpp.) */
struct S {
    [[no_unique_address]] int x;
    [[no_unique_address]] char c[3];
    [[no_unique_address]] long y;
};
static_assert(sizeof(S) == 16 && __builtin_offsetof(S, c) == 4 &&
              __builtin_offsetof(S, y) == 8);
template <class T> struct W {
    [[no_unique_address]] T v;
    char c;
};

int main()
{
    S s;
    s.x = 1;
    s.c[2] = 2;
    s.y = 3;
    W<int> w;
    w.v = 4;
    w.c = 5;
    return s.x + s.c[2] + static_cast<int>(s.y) + w.v + w.c - 15;
}
