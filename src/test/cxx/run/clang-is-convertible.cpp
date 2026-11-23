/* [meta.rel] is_convertible, clang's __is_convertible (and its spelling
   __is_convertible_to): the return expression of
   To test() { return declval<From>(); } is well-formed */
#include <stddef.h>
struct Base {};
struct Derived : Base {};
struct Priv : private Base {};
struct Conv { Conv(int) {} };
struct Expl { explicit Expl(int) {} };
struct ToInt { operator int() const { return 1; } };
struct ToIntExplicit { explicit operator int() const { return 1; } };
struct NoCopy { NoCopy(const NoCopy &) = delete; NoCopy(int) {} };
enum E { e0 };
enum class SE { s0 };
void fn();

#define YES(F, T) static_assert(__is_convertible(F, T), #F " -> " #T)
#define NO(F, T) static_assert(!__is_convertible(F, T), #F " -> " #T)

YES(int, long);
YES(long, double);
YES(double, int);
YES(int, int);
YES(const int, int);
YES(int, const int);
YES(int &, int);
YES(int, const int &);
NO(const int &, int &);
NO(int, int &);
YES(int, int &&);
NO(int &, int &&);
YES(int *, const int *);
NO(const int *, int *);
YES(int *, void *);
NO(void *, int *);
YES(int *, bool);
YES(Derived *, Base *);
NO(Base *, Derived *);
NO(Priv *, Base *);
YES(Derived &, Base &);
YES(Derived, Base);
NO(Base, Derived);
YES(Derived, const Base &);
YES(decltype(nullptr), int *);
NO(decltype(nullptr), int);
YES(E, int);
NO(int, E);
NO(SE, int);
NO(int, SE);
YES(int, Conv);
NO(int, Expl);
YES(ToInt, int);
YES(ToInt, double);
NO(ToIntExplicit, int);
YES(int, NoCopy);
NO(NoCopy, NoCopy);
YES(void, void);
YES(const void, void);
NO(void, int);
NO(int, void);
NO(int[3], int[3]);
YES(int[3], int *);
YES(int (&)[3], int *);
YES(int[3], int(&&)[3]);
NO(int, int[3]);
NO(int, void());
YES(void (), void (*)());
YES(void (&)(), void (*)());
NO(void (*)(), void ());
YES(void (), void (&)());
YES(const char *, bool);
NO(const char *, char *);
YES(int, unsigned);
NO(int, int *);
YES(int Base::*, int Base::*);
NO(int, int Base::*);

struct MoveOnly { MoveOnly(MoveOnly &&) {} };
YES(MoveOnly, MoveOnly);
NO(MoveOnly &, MoveOnly);
struct Plain {};
YES(Plain, Plain);
YES(const Plain &, Plain);
NO(const Plain, volatile Plain &);
YES(Plain &, Plain);

int main() { return 0; }
