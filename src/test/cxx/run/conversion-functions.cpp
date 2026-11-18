/* [class.conv.fct]: conversion functions, called by [dcl.init]/17.7 in a
   copy-initialization, a function argument, a return, a condition
   (contextually converted to bool, where an explicit one counts) */
struct Int {
    int v;
    operator int() const { return v; }
};
struct Flag {
    int on;
    explicit operator bool() const { return on != 0; }
};
static const char text[] = "xyz";
struct Text {
    int at;
    operator const char *() const { return text + at; }
    operator long() { return at * 100L; }
};
/* constexpr before it */
struct Constant {
    constexpr operator unsigned() const
    {
        return 7;
    }
};
struct Ref {
    int v;
    operator int &() { return v; }
};
/* explicit, to a type a qualification conversion makes const int *
   ([over.match.conv]/1) */
static int seven = 7;
struct Pointer {
    explicit operator int *() { return &seven; }
};
/* an rvalue reference result: an xvalue ([expr.call]/14) */
struct Moved {
    int v;
    operator int &&() { return static_cast<int &&>(v); }
};

static int twice(int x) { return 2 * x; }
/* [over.ics.rank]/3.3: one conversion function, the better second
   standard conversion sequence (int to int over int to long) */
static int which(int) { return 1; }
static int which(long) { return 2; }
static int which(const char *) { return 3; }
static long as_long(const Int &i) { return i; }

int main()
{
    Int i = {20};
    int a = i;
    Flag f = {1}, g = {0};
    int checks = 0;
    if (f)
        checks++;
    if (!g)
        checks++;
    while (g)
        return 90;
    Text t = {1};
    const char *p = t;
    const Text &ct = t;
    const char *q = ct;
    long l = t;
    Constant k;
    unsigned u = k;
    Ref r = {3};
    int &rv = r;
    rv = 5;
    Pointer pointer;
    const int *seen(pointer);
    Moved moved = {9};
    int mi = moved;
    long ml = moved;
    if (*seen != 7 || mi != 9 || ml != 9)
        return 3;
    if (which(i) != 1)
        return 2;
    if (a != 20 || twice(i) != 40 || as_long(i) != 20 || *p != 'y' ||
        *q != 'y' || l != 100 || u != 7 || r.v != 5 || checks != 2)
        return 1;
    return 0;
}
