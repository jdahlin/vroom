/* [class.temporary]/2, /4: the prvalue a built-in (trivial) class
   assignment takes as its right operand is a temporary, destroyed at the end
   of the full-expression. */
extern "C" int printf(const char *, ...);
static int destroyed;
struct A {
    ~A() { destroyed++; }
};
struct Str {
    A member;
    int value = 0;
    ~Str() = default;
};
struct P {
    int v;
    ~P() { destroyed += 100; }
};
#define SHOW printf("%d %d\n", __LINE__, destroyed)
template <class T> void braced(Str &s)
{
    s = {};
}
int main(int argc, char **)
{
    {
        Str s;
        P p{1};
        s = {};
        SHOW;
        s = Str();
        SHOW;
        s = Str{};
        SHOW;
        braced<int>(s);
        SHOW;
        p = {2};
        SHOW;
        p = P{3};
        SHOW;
        p = P();
        SHOW;
        s = argc > 5 ? Str() : Str{};
        SHOW;
        s = (argc++, Str());
        SHOW;
        (s = Str()).value = 3;
        SHOW;
        printf("%d %d\n", p.v, s.value);
    }
    SHOW;
}
