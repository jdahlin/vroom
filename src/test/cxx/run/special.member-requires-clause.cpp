/* [special]/6, [temp.constr.constr]: defaulted and deleted member functions
   with a requires-clause (libc++'s take_view and movable-box). */

/* standing for <concepts> */
template <class T>
concept default_initializable = requires { T(); };
template <class T>
concept copyable = requires(T &a, const T &b) { a = b; T(b); };
template <class T>
concept movable = requires(T &a, T b) { a = static_cast<T &&>(b); };
template <class T>
concept integral = __is_integral(T);

static int copies, moves;

template <class T>
struct Box {
    T value;
    Box() requires default_initializable<T> = default;
    explicit Box(T v) : value(static_cast<T &&>(v)) {}
    Box(const Box &) = default;
    Box(Box &&) = default;
    Box &operator=(const Box &) requires copyable<T> = default;
    Box &operator=(Box &&) requires movable<T> = default;
    Box &operator=(const Box &other) {
        copies++;
        value = other.value;
        return *this;
    }
    Box &operator=(Box &&other) {
        moves++;
        value = static_cast<T &&>(other.value);
        return *this;
    }
};

struct NoDefault {
    NoDefault(int) {}
};

struct NoAssign {
    int n;
    NoAssign &operator=(const NoAssign &) = delete;
};

template <class T>
struct Pick {
    int n;
    Pick &operator=(const Pick &) requires copyable<T> = default;
    Pick &operator=(const Pick &other) {
        copies++;
        n = other.n + 1;
        return *this;
    }
};

template <class T>
struct Deleting {
    int f() requires integral<T> = delete;
    int f() requires (!integral<T>) { return 2; }
};

static_assert(__is_constructible(Box<int>));
static_assert(!__is_constructible(Box<NoDefault>));
static_assert(__is_trivially_assignable(Box<int> &, const Box<int> &));
static_assert(__is_trivially_assignable(Box<int> &, Box<int> &&));

int main()
{
    Box<int> a(1), b(2);
    a = b;
    if (a.value != 2 || copies || moves)
        return 1;
    a = Box<int>(3);
    if (a.value != 3 || copies || moves)
        return 2;
    Pick<int> c{1}, d{2};
    c = d;
    if (c.n != 2 || copies)
        return 3;
    Pick<NoAssign> e{4}, f{5};
    e = f;
    if (e.n != 6 || copies != 1)
        return 4;
    Deleting<double> g;
    if (g.f() != 2)
        return 5;
    return 0;
}
