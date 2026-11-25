// __is_assignable of a class whose assignment operators take no right
// operand of another class (libc++'s basic_istream: a deleted copy and a
// protected move assignment; an aggregate with a member of a class with a
// user-provided operator=): no viable function, so false
// ([over.match.viable]/4); json's from_json traits ask it of std::string.
extern "C" int printf(const char *, ...);
struct Str { Str(const char *) {} Str(const Str &) {} Str &operator=(const Str &) { return *this; } };
struct Base { virtual ~Base() {} };
template <class C> struct is : Base {
  protected:
    is(is &&) {}
    is &operator=(is &&) { return *this; }
  public:
    is() {}
    is(const is &) = delete;
    is &operator=(const is &) = delete;
};
struct Other { Other() {} Other(const Other &) {} ~Other() {} };
struct Conv { Conv(const Str &) {} Conv(const Conv &) {} ~Conv() {} };
struct Plain { int x; Str s; };
static_assert(!__is_assignable(is<char> &, const Str &), "");
static_assert(!__is_assignable(Other &, const Str &), "");
static_assert(__is_assignable(Conv &, const Str &), "");
static_assert(!__is_assignable(Plain &, const Str &), "");
static_assert(__is_assignable(Plain &, const Plain &), "");
int main() {
    Plain a{1, "x"}, b{2, "y"};
    a = b;
    printf("%d\n", a.x);
    return a.x - 2;
}
