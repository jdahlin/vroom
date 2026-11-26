/* explicit X() = default makes the default constructor explicit ([class.conv.ctor]/1), not the defaulted
   move constructor beside it: copy-initialization from an xvalue moves ([over.match.ctor]/1). */
extern "C" int printf(const char *, ...);

static int copies, moves;

struct counted {
    counted() = default;
    counted(counted const &) { ++copies; }
    counted(counted &&) { ++moves; }
};

struct X {
    counted c;
    explicit X() = default;
    X(int) {}
    X(X const &) = default;
    X(X &&) = default;
};

template <class T> struct Y {
    T c;
    explicit Y() = default;
    Y(int) {}
    Y(Y const &) = default;
    Y(Y &&) noexcept = default;
};

template <class T> static void run()
{
    T a(1);
    T b = static_cast<T &&>(a);
    T d = b;

    (void)d;
    printf("%d %d\n", copies, moves);
}

int main()
{
    run<X>();
    run<Y<counted>>();
    return copies != 2 || moves != 2;
}
