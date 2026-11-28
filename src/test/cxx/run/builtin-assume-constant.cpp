// [ext:gnu_library_builtins] __builtin_assume in a constant expression: V8's
// V8_ASSUME in a constexpr factory of a constexpr register object.  The
// condition is not evaluated, whatever it says, at compile time or run time.
template <typename Sub, int kAfterLast> struct RegisterBase {
    static constexpr Sub from_code(int code)
    {
        __builtin_assume(code >= 0 && code < kAfterLast);
        return Sub{code};
    }
    constexpr int code() const
    {
        __builtin_assume(code_ >= 0);
        return code_;
    }

protected:
    explicit constexpr RegisterBase(int code) : code_(code) {}

private:
    int code_;
};

struct Register : RegisterBase<Register, 4> {
    friend struct RegisterBase<Register, 4>;

private:
    explicit constexpr Register(int code) : RegisterBase(code) {}
};

constexpr Register rdx = Register::from_code(2);
static_assert(rdx.code() == 2);

int global = 0;

constexpr int unchecked(int x)
{
    __builtin_assume(x > 0);
    __builtin_assume(global >= 0);
    __builtin_assume(++x);
    return x;
}

constexpr int folded = unchecked(-1);
static_assert(folded == -1);
static_assert((__builtin_assume(false), true));

int main()
{
    volatile int in = 3;
    int x = in;
    if (unchecked(x) != 3)
        return 1;
    __builtin_assume(++x);
    if (x != 3)
        return 2;
    if (Register::from_code(in).code() != 3)
        return 3;
    return folded + 1 + rdx.code() - 2;
}
