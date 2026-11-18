/* [temp.variadic]/1: template parameter packs of alias templates, named
   and unnamed (libc++'s template <class...> using __void_t = void) */
template <class...> using void_t = void;
template <class... Ts> using int_t = int;
template <class T, class... Rest> using first_t = T;

void_t<int, long> f()
{
}
int_t<> zero = 0;
int_t<char, short, long> one = 1;
first_t<long, char, int> two = 2;
static_assert(sizeof(first_t<char, long>) == 1, "");
int main()
{
    f();
    if (zero != 0 || one != 1 || two != 2)
        return 1;
    return 0;
}
