/* [over.match.oper]/4: a compound assignment to a class without an operator
   function is ill-formed, so in a substitution it fails ([temp.deduct]/8),
   and a quiet instantiation picks the other overload (json's concat_into
   with a string class that has append but no operator+=) */
template <class T> T &&declval();
template <class...> using void_t = void;
struct appender {
    int n;
    appender &append(char)
    {
        n += 1;
        return *this;
    }
};
struct adder {
    int n;
    adder &operator+=(char)
    {
        n += 10;
        return *this;
    }
};
struct converts {
    operator int() const { return 1; }
};

template <class S, class A, class = void> struct has_add {
    static constexpr bool value = false;
};
template <class S, class A>
struct has_add<S, A, void_t<decltype(declval<S &>() += declval<A &&>())>> {
    static constexpr bool value = true;
};
static_assert(!has_add<appender, char>::value, "");
static_assert(!has_add<converts, int>::value, "");
static_assert(has_add<adder, char>::value, "");
static_assert(has_add<int, converts>::value, "");

template <bool B> struct when {};
template <> struct when<true> {
    typedef int type;
};

template <class S, typename when<has_add<S, char>::value>::type = 0>
void add(S &s)
{
    s += 'a';
}
template <class S, typename when<!has_add<S, char>::value>::type = 0>
void add(S &s)
{
    s.append('a');
}

template <class S> struct builder {
    S s;
    int build()
    {
        s.n = 0;
        add(s);
        return s.n;
    }
};

int main()
{
    builder<appender> a;
    builder<adder> b;
    if (a.build() != 1)
        return 1;
    if (b.build() != 10)
        return 2;
    return 0;
}
