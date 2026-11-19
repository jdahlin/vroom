/* [temp.alias]/2, [temp.variadic]/5: an alias template's own pack
   expanded after other arguments into another alias's pack (libc++'s
   invoke_result_t = __invoke_result_t<_Fn, _Args...>): dependent until
   the alias is used with concrete arguments */
template <class... Ts> struct count {
    static const int value = sizeof...(Ts);
    typedef long type;
};
template <class... Ts> using count_t = typename count<Ts...>::type;
template <class F, class... Args> using call_t = count_t<F, Args...>;
template <class F, class... Args> struct size_of {
    static const int value = count<F, Args...>::value + sizeof(call_t<F, Args...>);
};
call_t<int, char, short> l = 40;
int main()
{
    if (sizeof(call_t<int>) != sizeof(long) || l != 40)
        return 1;
    return size_of<int, char>::value - 2 - (int)sizeof(long);
}
