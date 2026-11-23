/* captures in generic lambdas of member functions and class templates:
   this, *this, references, init-captures, mutable, nesting */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

struct S {
    int v = 3;
    int by_this() { return [this](auto x) { return v + x; }(1); }
    int by_default() { return [&](auto x) { return v * x; }(2); }
    int by_copy() { return [*this](auto x) { return v - x; }(1); }
    int member_call() { return [=, this](auto x) { return helper(x); }(5); }
    int helper(int x) { return x + v; }
};

template <class T> struct Box {
    T value;
    T get_plus(T d) { return [this, d](auto extra) { return value + d + extra; }(T(1)); }
    T twice() { return [&](auto k) { return value * k; }(2); }
};

int zero = 0;

int main()
{
    S s;
    check(s.by_this() == 4 && s.by_default() == 6 && s.by_copy() == 2);
    check(s.member_call() == 8);
    Box<int> b{10};
    check(b.get_plus(5) == 16 && b.twice() == 20);
    int a = 1;
    int &r = a;
    auto by_ref = [&r](auto x) { r += x; return r; };
    check(by_ref(2) == 3 && a == 3);
    auto init = [k = a + 1](auto x) mutable { k += x; return k; };
    check(init(1) == 5 && init(1) == 6);
    auto nested = [a](auto x) { return [a, x](auto y) { return a + x + y; }; };
    check(nested(10)(100) == 113);
    auto nested_default = [=](auto x) { return [=](auto y) { return a + x + y; }(2); };
    check(nested_default(10 + zero) == 15);
    return 0;
}
