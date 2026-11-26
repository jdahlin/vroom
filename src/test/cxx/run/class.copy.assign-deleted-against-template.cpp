/* [class.copy.assign]/6: a declared move constructor deletes the implicit
   copy assignment, which still takes part in overload resolution: it is
   selected over a template operator= that binds the right operand no
   better (a non-template is preferred, [over.match.best]/2), and loses to
   one that does ([over.ics.rank]/3.2) */
struct Forward {
    int n = 0;
    Forward() = default;
    Forward(Forward &&) = default;
    template <class T> Forward &operator=(T &&)
    {
        n++;
        return *this;
    }
};
static_assert(!__is_assignable(Forward &, const Forward &), "");
static_assert(__is_assignable(Forward &, Forward &), "");
static_assert(__is_assignable(Forward &, Forward), "");
static_assert(!__is_assignable(const Forward &, Forward &), "");

struct ByValue {
    ByValue() = default;
    ByValue(ByValue &&) = default;
    template <class T> ByValue &operator=(T)
    {
        return *this;
    }
};
static_assert(!__is_assignable(ByValue &, ByValue &), "");
static_assert(!__is_assignable(ByValue &, const ByValue &), "");
static_assert(!__is_assignable(ByValue &, ByValue), "");
static_assert(__is_assignable(ByValue &, int), "");

struct ConstRef {
    ConstRef() = default;
    ConstRef(ConstRef &&) = default;
    template <class T> ConstRef &operator=(const T &)
    {
        return *this;
    }
};
static_assert(!__is_assignable(ConstRef &, ConstRef &), "");
static_assert(!__is_assignable(ConstRef &, const ConstRef &), "");

int main()
{
    Forward a, b;
    a = b;
    a = static_cast<Forward &&>(b);
    return a.n != 2;
}
