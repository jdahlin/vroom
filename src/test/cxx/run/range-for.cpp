/* [stmt.ranged]: range-based for over arrays, classes with member begin
   and end, classes whose free begin and end argument-dependent lookup
   finds, begin and end of different types; auto, auto &, const auto &
   and auto && loop variables; the C++20 init-statement; break and
   continue, nested loops; the range a temporary, its lifetime extended
   (one of a class with a destructor is not supported: [class.temporary]/6
   in reference_binding) */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

struct Vec {
    int data[4];
    int n;
    int *begin() { return data; }
    int *end() { return data + n; }
    const int *begin() const { return data; }
    const int *end() const { return data + n; }
};

/* an iterator class and a sentinel of another type (C++17) */
struct Sentinel {
    int last;
};

struct Counter {
    int i;
    int operator*() const { return i; }
    Counter &operator++()
    {
        ++i;
        return *this;
    }
    bool operator!=(Sentinel s) const { return i != s.last; }
};

struct Range {
    int from, to;
    Counter begin() const { return Counter{from}; }
    Sentinel end() const { return Sentinel{to}; }
};

namespace lib {
struct Bag {
    int items[3];
};
int *begin(Bag &b) { return b.items; }
int *end(Bag &b) { return b.items + 3; }
} // namespace lib

int live = 0;

struct Item {
    int v;
    Item() : v(0) { live++; }
    Item(const Item &o) : v(o.v) { live++; }
    ~Item() { live--; }
};

template <class R> int total(R &r)
{
    int s = 0;
    for (auto &&x : r)
        s += x;
    return s;
}

Vec make_vec()
{
    Vec v = {{1, 2, 3, 4}, 3};
    return v;
}

int sum_const(const Vec &v)
{
    int s = 0;
    for (const auto &x : v)
        s += x;
    return s;
}

/* a range of enumeration type: begin(range) and end(range) found by
   argument-dependent lookup ([stmt.ranged]/1.3) */
namespace digits {
enum Set { low, high };
int values[2][3] = {{1, 2, 3}, {7, 8, 9}};
int *begin(Set s) { return values[s]; }
int *end(Set s) { return values[s] + 3; }
} // namespace digits

int main()
{
    int a[5] = {1, 2, 3, 4, 5};
    int s = 0;
    for (int x : a)
        s += x;
    check(s == 15);
    for (auto &x : a)
        x *= 2;
    check(a[0] == 2 && a[4] == 10);
    s = 0;
    for (const auto &x : a)
        s += x;
    check(s == 30);
    s = 0;
    for (auto &&x : a) {
        if (x == 4)
            continue;
        if (x == 8)
            break;
        s += x;
    }
    check(s == 2 + 6);

    Vec v = {{10, 20, 30, 40}, 3};
    s = 0;
    for (auto x : v)
        s += x;
    check(s == 60);
    for (int &x : v)
        x++;
    check(sum_const(v) == 63);

    s = 0;
    Range r = {3, 7};
    for (int i : r)
        s += i;
    check(s == 3 + 4 + 5 + 6);

    lib::Bag bag = {{7, 8, 9}};
    s = 0;
    for (int x : bag)
        s += x;
    check(s == 24);

    /* the init-statement (C++20) */
    s = 0;
    for (int k = 100; int x : a)
        s += k + x;
    check(s == 500 + 30);

    /* nested loops */
    int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
    s = 0;
    for (auto &row : grid)
        for (int x : row) {
            if (x == 2)
                continue;
            s += x;
        }
    check(s == 19);

    /* the range a temporary: the reference range extends its lifetime */
    s = 0;
    for (int x : make_vec())
        s += x;
    check(s == 6);

    /* a loop variable with a destructor: destroyed at the end of each
       iteration, by continue and break too */
    {
        Item items[4];
        for (int i = 0; i < 4; i++)
            items[i].v = i + 1;
        check(live == 4);
        s = 0;
        for (Item it : items) {
            check(live == 5);
            if (it.v == 2)
                continue;
            if (it.v == 4)
                break;
            s += it.v;
        }
        check(live == 4);
        check(s == 4);
    }
    check(live == 0);

    s = 0;
    for (int x : digits::high)
        s += x;
    for (const int &x : digits::low)
        s += x;
    check(s == 24 + 6);

    /* in a function template's specializations */
    check(total(a) == 30);
    check(total(v) == 63);
    return 0;
}
