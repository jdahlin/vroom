/* [namespace.udir]/6: functions of one name that using-directives make visible
   from several namespaces are one overload set of a call, by identifier or
   operator-function-id, beside what argument-dependent lookup finds
   (doctest's decltype(operator<<(...))) */
extern "C" int printf(const char *, ...);

namespace a
{
struct A {
};
int f(A)
{
    return 1;
}
int operator<<(int x, A)
{
    return x + 5;
}
} // namespace a
namespace b
{
struct B {
};
int f(B)
{
    return 2;
}
int f(B, B)
{
    return 6;
}
int operator<<(int x, B)
{
    return x + 7;
}
} // namespace b
namespace c
{
struct C {
};
template <class T> int f(C, T)
{
    return 3;
}
int f(C)
{
    return 4;
}
template <class T> int operator<<(T x, C)
{
    return x + 9;
}
} // namespace c

namespace d
{
using namespace a;
using namespace b;
void run()
{
    printf("%d %d %d %d %d\n", f(A{}), f(B{}), f(B{}, B{}), f(c::C{}, 1),
           f(c::C{}));
    printf("%d %d %d\n", operator<<(10, A{}), operator<<(20, B{}),
           operator<<(30, c::C{}));
}
} // namespace d

int main()
{
    d::run();
}
