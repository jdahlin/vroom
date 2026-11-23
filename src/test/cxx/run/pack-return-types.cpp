/* [temp.variadic]: a function template whose return type expands its
   own pack, directly or through an alias template forwarding it whole
   (libc++'s __invoke_result_t<_Args...> __invoke(_Args&&...)); mangled
   as clang does (N3resIJDpT_EE4typeE) */
template <class... A> struct res { typedef int type; };
template <class... A> struct res<char, A...> { typedef long type; };
template <class... A> using res_t = typename res<A...>::type;

int add(int a, int b) { return a + b; }
long add(char c, int b) { return c * 100L + b; }

template <class... A> res_t<A...> call(A &&...a) { return add(a...); }
template <class... A> typename res<A...>::type direct(A &&...a)
{
    return add(a...);
}

int main()
{
    if (call(1, 2) != 3 || direct(3, 4) != 7)
        return 1;
    if (sizeof(call('a', 1)) != 8 || call('\1', 2) != 102)
        return 2;
    return 0;
}
